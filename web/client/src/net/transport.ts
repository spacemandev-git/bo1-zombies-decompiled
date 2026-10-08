// transport.ts - GameTransport: engine packets between lobby slots over WebRTC, or the server relay as fallback.
//
// send(toSlot, data) uses the peer's "game" data channel when it is open, otherwise a relay frame through the lobby
// WebSocket ([RELAY_FRAME][slot][payload]). Incoming packets from either path reach onPacket listeners, except the
// page's own probe packets (probe.ts), which are answered here: relay-mode RTT pings and the test burst.
// Non-host players report their link to the host to the server (net.status); the server broadcasts it.

import type { RoomState } from "../../../shared/protocol";
import { PeerManager, type PeerHost, type PeerLink } from "./peer";
import { decodeProbe, encodeProbe, ProbeType, probeReply, summarizeBurst, type BurstResult, type Probe } from "./probe";
import type { Signaling } from "./signaling";

export interface SlotNetStatus {
  slot: number;
  peerId: string | null;
  mode: "p2p" | "relay" | "self";
  rttMs: number | null;
  connection: string; // RTCPeerConnectionState, "waiting" (host waiting for an offer) or "relay-only"
  sent: number;
  received: number;
}

export interface GameTransport {
  send(toSlot: number, data: Uint8Array): void;
  onPacket(fn: (fromSlot: number, data: Uint8Array<ArrayBuffer>) => void): () => void;
  status(slot: number): SlotNetStatus | null;
  statuses(): SlotNetStatus[];
  /** Slots this player exchanges packets with (host: every other player; others: the host). */
  reachableSlots(): number[];
}

const STATUS_REPORT_MS = 5000;

export class NetTransport implements GameTransport {
  readonly peers: PeerManager;
  private room: RoomState | null = null;
  private myPeerId: string | null = null;
  private packetListeners = new Set<(fromSlot: number, data: Uint8Array<ArrayBuffer>) => void>();
  private changeListeners = new Set<() => void>();
  private counters = new Map<number, { sent: number; received: number }>();
  private burstWaiters = new Map<string, number>(); // "slot:id" -> send time
  private burstRtts = new Map<number, number[]>();
  private lastReport = { mode: "", rtt: -1, at: 0 };
  private reportTimer: ReturnType<typeof setInterval>;
  private changeQueued = false;
  readonly log: string[] = [];

  constructor(private readonly signaling: Signaling) {
    const host: PeerHost = {
      iceServers: () => signaling.iceServers as RTCIceServer[],
      signal: (to, signal) => signaling.send({ t: "rtc.signal", to, signal }),
      onGameData: (link, data) => this.receive(link.slot, data, "p2p", link),
      sendRelayProbe: (slot, probe) => this.sendProbe(slot, probe, "relay"),
      changed: () => this.changed(),
      log: (m) => this.addLog(m),
    };
    this.peers = new PeerManager(host);
    signaling.on("rtc.signal", (m) => this.peers.handleSignal(m.from, m.signal));
    signaling.onRelay((fromSlot, data) => this.receive(fromSlot, data, "relay", null));
    this.reportTimer = setInterval(() => this.report(true), STATUS_REPORT_MS);
  }

  private addLog(m: string): void {
    const line = `${new Date().toLocaleTimeString()} ${m}`;
    this.log.push(line);
    if (this.log.length > 200) this.log.shift();
    console.debug(`[net] ${m}`);
  }

  setRoom(room: RoomState | null, myPeerId: string | null): void {
    this.room = room;
    this.myPeerId = myPeerId;
    this.peers.update(room, myPeerId);
    if (!room) this.counters.clear();
  }

  get mySlot(): number | null {
    return this.room?.slots.find((s) => s.peerId === this.myPeerId)?.index ?? null;
  }

  get isHost(): boolean {
    return !!this.room && this.room.hostPeerId === this.myPeerId;
  }

  onChange(fn: () => void): () => void {
    this.changeListeners.add(fn);
    return () => this.changeListeners.delete(fn);
  }

  private changed(): void {
    if (this.changeQueued) return;
    this.changeQueued = true;
    queueMicrotask(() => {
      this.changeQueued = false;
      this.report(false);
      for (const fn of this.changeListeners) fn();
    });
  }

  private count(slot: number): { sent: number; received: number } {
    let c = this.counters.get(slot);
    if (!c) {
      c = { sent: 0, received: 0 };
      this.counters.set(slot, c);
    }
    return c;
  }

  onPacket(fn: (fromSlot: number, data: Uint8Array<ArrayBuffer>) => void): () => void {
    this.packetListeners.add(fn);
    return () => this.packetListeners.delete(fn);
  }

  send(toSlot: number, data: Uint8Array): void {
    const copy: Uint8Array<ArrayBuffer> = data.buffer instanceof ArrayBuffer ? (data as Uint8Array<ArrayBuffer>) : new Uint8Array(data);
    this.count(toSlot).sent++;
    const link = this.peers.linkForSlot(toSlot);
    if (link && link.sendGame(copy)) return;
    this.signaling.sendRelay(toSlot, copy);
  }

  private sendProbe(slot: number, probe: Probe, path: "p2p" | "relay" | "best"): void {
    const bytes = encodeProbe(probe);
    if (path === "relay") {
      this.signaling.sendRelay(slot, bytes);
      return;
    }
    const link = this.peers.linkForSlot(slot);
    if (path === "p2p" || path === "best") {
      if (link && link.sendGame(bytes)) return;
    }
    this.signaling.sendRelay(slot, bytes);
  }

  private receive(fromSlot: number, data: Uint8Array<ArrayBuffer>, path: "p2p" | "relay", link: PeerLink | null): void {
    const probe = decodeProbe(data);
    if (probe) {
      this.onProbe(fromSlot, probe, path, link);
      return;
    }
    this.count(fromSlot).received++;
    for (const fn of this.packetListeners) fn(fromSlot, data);
  }

  private onProbe(fromSlot: number, p: Probe, path: "p2p" | "relay", link: PeerLink | null): void {
    const reply = probeReply(p);
    if (reply) {
      this.sendProbe(fromSlot, reply, path);
      return;
    }
    if (p.type === ProbeType.Pong) {
      (link ?? this.peers.linkForSlot(fromSlot))?.onRelayPong(p);
    } else if (p.type === ProbeType.BurstEcho) {
      const key = `${fromSlot}:${p.id}`;
      const sentAt = this.burstWaiters.get(key);
      if (sentAt === undefined) return;
      this.burstWaiters.delete(key);
      const list = this.burstRtts.get(fromSlot) ?? [];
      list.push(performance.now() - sentAt);
      this.burstRtts.set(fromSlot, list);
    }
  }

  reachableSlots(): number[] {
    const room = this.room;
    const me = this.mySlot;
    if (!room || me === null) return [];
    if (this.isHost) {
      return room.slots.filter((s) => s.peerId && s.index !== me && s.connected).map((s) => s.index);
    }
    const host = room.slots.find((s) => s.peerId === room.hostPeerId);
    return host ? [host.index] : [];
  }

  status(slot: number): SlotNetStatus | null {
    const room = this.room;
    if (!room) return null;
    const s = room.slots.find((x) => x.index === slot);
    if (!s || !s.peerId) return null;
    const c = this.count(slot);
    if (s.peerId === this.myPeerId) {
      return { slot, peerId: s.peerId, mode: "self", rttMs: null, connection: "self", sent: c.sent, received: c.received };
    }
    const link = this.peers.links.get(s.peerId);
    return {
      slot,
      peerId: s.peerId,
      mode: link?.mode ?? "relay",
      rttMs: link?.currentRtt ?? null,
      connection: link?.connectionState ?? "relay-only",
      sent: c.sent,
      received: c.received,
    };
  }

  statuses(): SlotNetStatus[] {
    return (this.room?.slots ?? []).map((s) => this.status(s.index)).filter((x): x is SlotNetStatus => x !== null);
  }

  /** Non-host: report the link to the host (on change, at most every 5 s otherwise). */
  private report(periodic: boolean): void {
    if (!this.room || this.isHost || !this.signaling.open) return;
    const host = this.room.slots.find((s) => s.peerId === this.room!.hostPeerId);
    if (!host) return;
    const link = this.peers.links.get(host.peerId!);
    const mode = link?.mode ?? "relay";
    const rtt = link?.currentRtt ?? null;
    const rounded = rtt === null ? -1 : Math.round(rtt);
    const now = performance.now();
    const last = this.lastReport;
    const changedMuch =
      mode !== last.mode ||
      (rounded === -1) !== (last.rtt === -1) ||
      Math.abs(rounded - last.rtt) > Math.max(10, last.rtt * 0.25);
    if (!changedMuch && !(periodic && now - this.lastReport.at >= STATUS_REPORT_MS - 500)) return;
    if (!changedMuch && now - this.lastReport.at < 1000) return;
    this.lastReport = { mode, rtt: rounded, at: now };
    this.signaling.send({ t: "net.status", mode, rttMs: rtt === null ? null : Math.round(rtt) });
  }

  /**
   * Sends `count` probe packets to every reachable slot over the game path (data channel or relay) and collects
   * the echoes: loss and RTT per slot. Works without the engine.
   */
  async testBurst(count = 100, spacingMs = 5, waitMs = 2000): Promise<BurstResult[]> {
    const slots = this.reachableSlots();
    const base = (Math.random() * 0x7fffffff) >>> 0;
    for (const s of slots) this.burstRtts.set(s, []);
    for (let i = 0; i < count; i++) {
      for (const s of slots) {
        const id = (base + i) >>> 0;
        const now = performance.now();
        this.burstWaiters.set(`${s}:${id}`, now);
        this.sendProbe(s, { type: ProbeType.Burst, id, time: now }, "best");
      }
      if (spacingMs > 0) await new Promise((r) => setTimeout(r, spacingMs));
    }
    await new Promise((r) => setTimeout(r, waitMs));
    const results = slots.map((s) => summarizeBurst(s, count, this.burstRtts.get(s) ?? []));
    for (const s of slots) {
      this.burstRtts.delete(s);
      for (let i = 0; i < count; i++) this.burstWaiters.delete(`${s}:${(base + i) >>> 0}`);
    }
    return results;
  }

  dispose(): void {
    clearInterval(this.reportTimer);
    this.peers.closeAll();
  }
}
