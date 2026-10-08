// peer.ts - WebRTC links in a star: every non-host player keeps one RTCPeerConnection to the host.
//
// - Links exist as soon as both players are in the same room (not only during a game).
// - The non-host creates the offer; signaling goes through the lobby server (rtc.signal), trickle ICE.
// - Two pre-negotiated data channels: "game" (id 0, unordered, no retransmits) for engine packets and "ctl"
//   (id 1, reliable) for RTT pings every 2 s.
// - If "game" is not open after 8 s, or the connection fails or disconnects, packets go through the server relay
//   (transport.ts) and the offerer keeps retrying in the background: ICE restarts, every third try a new
//   connection. The host replaces its connection when an offer comes with a different DTLS fingerprint.
// - Links follow the room: they are closed when a peer leaves or drops and rebuilt when the host changes.

import type { RoomState, RtcSignal } from "../../../shared/protocol";
import { ProbeType, type Probe } from "./probe";

export const OPEN_TIMEOUT_MS = 8000;
export const PING_INTERVAL_MS = 2000;
const MAX_RETRY_MS = 30_000;

export interface PeerHost {
  iceServers(): RTCIceServer[];
  signal(to: string, signal: RtcSignal): void;
  onGameData(link: PeerLink, data: Uint8Array<ArrayBuffer>): void;
  sendRelayProbe(slot: number, probe: Probe): void;
  changed(): void;
  log(msg: string): void;
}

function fingerprint(sdp: string | undefined | null): string | null {
  if (!sdp) return null;
  const m = /^a=fingerprint:(.+)$/im.exec(sdp);
  return m?.[1]?.trim().toLowerCase() ?? null;
}

let pingSeq = 1;

export class PeerLink {
  pc: RTCPeerConnection | null = null;
  game: RTCDataChannel | null = null;
  ctl: RTCDataChannel | null = null;
  rttMs: number | null = null;
  relayRttMs: number | null = null;
  closed = false;
  connectedOnce = false;
  private pending: RTCIceCandidateInit[] = [];
  private openTimer: ReturnType<typeof setTimeout> | null = null;
  private retryTimer: ReturnType<typeof setTimeout> | null = null;
  private pingTimer: ReturnType<typeof setInterval> | null = null;
  private attempts = 0;
  private relayPings = new Map<number, number>();

  constructor(
    readonly peerId: string,
    public slot: number,
    readonly offerer: boolean,
    private readonly host: PeerHost,
  ) {
    this.pingTimer = setInterval(() => this.ping(), PING_INTERVAL_MS);
  }

  /** The game channel is open and ICE is not disconnected or failed. */
  get p2p(): boolean {
    const st = this.pc?.connectionState;
    return this.game?.readyState === "open" && st !== "disconnected" && st !== "failed" && st !== "closed";
  }

  get mode(): "p2p" | "relay" {
    return this.p2p ? "p2p" : "relay";
  }

  get currentRtt(): number | null {
    return this.p2p ? this.rttMs : this.relayRttMs;
  }

  get connectionState(): string {
    if (this.pc) return this.pc.connectionState;
    return this.offerer || this.closed ? "relay only" : "waiting for offer";
  }

  start(): void {
    if (!this.offerer) return;
    if (this.newConnection()) void this.offer(false);
  }

  private newConnection(): boolean {
    this.teardownPc();
    let pc: RTCPeerConnection;
    try {
      if (typeof RTCPeerConnection !== "function") throw new Error("WebRTC is not available");
      pc = new RTCPeerConnection({ iceServers: this.host.iceServers() });
    } catch (e) {
      this.host.log(`peer ${this.slot}: ${String(e)}; using the relay only`);
      this.host.changed();
      return false;
    }
    this.pc = pc;
    this.pending = [];
    const game = pc.createDataChannel("game", { negotiated: true, id: 0, ordered: false, maxRetransmits: 0 });
    const ctl = pc.createDataChannel("ctl", { negotiated: true, id: 1, ordered: true });
    game.binaryType = "arraybuffer";
    ctl.binaryType = "arraybuffer";
    this.game = game;
    this.ctl = ctl;
    pc.onicecandidate = (ev) => {
      if (this.pc !== pc) return;
      this.host.signal(this.peerId, { type: "candidate", candidate: ev.candidate ? ev.candidate.toJSON() : null });
    };
    pc.onconnectionstatechange = () => {
      if (this.pc !== pc) return;
      const st = pc.connectionState;
      this.host.log(`peer ${this.slot}: ${st}`);
      if (st === "connected") {
        this.attempts = 0;
        this.connectedOnce = true;
        this.clearRetry();
      } else if (st === "failed") {
        this.scheduleRetry(500);
      } else if (st === "disconnected") {
        this.scheduleRetry(3000);
      }
      this.host.changed();
    };
    game.onopen = () => {
      this.clearOpenTimer();
      this.host.changed();
      this.ping();
    };
    game.onclose = () => {
      this.host.changed();
      if (!this.closed && this.pc === pc) this.scheduleRetry(1000);
    };
    game.onmessage = (ev) => {
      if (ev.data instanceof ArrayBuffer) this.host.onGameData(this, new Uint8Array(ev.data));
    };
    ctl.onmessage = (ev) => this.onCtl(ev.data);
    this.armOpenTimer();
    return true;
  }

  private armOpenTimer(): void {
    this.clearOpenTimer();
    this.openTimer = setTimeout(() => {
      this.openTimer = null;
      if (this.p2p || this.closed) return;
      this.host.log(`peer ${this.slot}: no data channel after ${OPEN_TIMEOUT_MS / 1000} s, using the relay`);
      this.host.changed();
      this.scheduleRetry(0);
    }, OPEN_TIMEOUT_MS);
  }

  private clearOpenTimer(): void {
    if (this.openTimer) clearTimeout(this.openTimer);
    this.openTimer = null;
  }

  private clearRetry(): void {
    if (this.retryTimer) clearTimeout(this.retryTimer);
    this.retryTimer = null;
  }

  /** Offerer only: ICE restart (or a fresh connection every third try) until the channel opens. */
  private scheduleRetry(delay?: number): void {
    if (!this.offerer || this.closed || this.retryTimer) return;
    const wait = delay ?? Math.min(MAX_RETRY_MS, OPEN_TIMEOUT_MS * 2 ** Math.min(this.attempts, 3));
    this.retryTimer = setTimeout(() => {
      this.retryTimer = null;
      if (this.closed || this.p2p) return;
      this.attempts++;
      const fresh = !this.pc || this.attempts % 3 === 0 || this.pc.connectionState === "closed";
      this.host.log(`peer ${this.slot}: ${fresh ? "new connection" : "ICE restart"} (try ${this.attempts})`);
      if (fresh) {
        if (!this.newConnection()) return; // no WebRTC here: stay on the relay
        void this.offer(false);
      } else {
        this.armOpenTimer();
        void this.offer(true);
      }
      // keep trying in the background while the relay carries the traffic
      this.scheduleRetry();
    }, wait);
  }

  private async offer(iceRestart: boolean): Promise<void> {
    const pc = this.pc;
    if (!pc) return;
    try {
      if (iceRestart) pc.restartIce();
      const offer = await pc.createOffer(iceRestart ? { iceRestart: true } : undefined);
      if (this.pc !== pc) return;
      await pc.setLocalDescription(offer);
      this.host.signal(this.peerId, { type: "offer", sdp: offer.sdp ?? "" });
    } catch (e) {
      this.host.log(`peer ${this.slot}: offer failed: ${String(e)}`);
    }
  }

  async handleSignal(sig: RtcSignal): Promise<void> {
    if (this.closed) return;
    try {
      if (sig.type === "offer") {
        if (this.offerer) return; // only the non-host offers
        const same = this.pc && fingerprint(this.pc.remoteDescription?.sdp) === fingerprint(sig.sdp);
        if ((!same || !this.pc || this.pc.signalingState === "closed") && !this.newConnection()) return;
        const pc = this.pc;
        if (!pc) return;
        await pc.setRemoteDescription({ type: "offer", sdp: sig.sdp });
        await this.flushCandidates(pc);
        const answer = await pc.createAnswer();
        if (this.pc !== pc) return;
        await pc.setLocalDescription(answer);
        this.host.signal(this.peerId, { type: "answer", sdp: answer.sdp ?? "" });
      } else if (sig.type === "answer") {
        const pc = this.pc;
        if (!pc || pc.signalingState !== "have-local-offer") return;
        await pc.setRemoteDescription({ type: "answer", sdp: sig.sdp });
        await this.flushCandidates(pc);
      } else if (sig.type === "candidate") {
        const pc = this.pc;
        if (!pc) return;
        if (!pc.remoteDescription) {
          if (sig.candidate) this.pending.push(sig.candidate);
          return;
        }
        await pc.addIceCandidate(sig.candidate ?? undefined).catch(() => undefined);
      }
    } catch (e) {
      this.host.log(`peer ${this.slot}: signal ${sig.type} failed: ${String(e)}`);
    }
  }

  private async flushCandidates(pc: RTCPeerConnection): Promise<void> {
    const list = this.pending;
    this.pending = [];
    for (const c of list) await pc.addIceCandidate(c).catch(() => undefined);
  }

  /** Sends a packet on the game channel. Returns false when the channel is not usable. */
  sendGame(data: Uint8Array<ArrayBuffer>): boolean {
    const ch = this.game;
    if (!ch || !this.p2p) return false;
    if (ch.bufferedAmount > 256 * 1024) return true; // congested: drop (UDP semantics)
    try {
      ch.send(data);
      return true;
    } catch {
      return false;
    }
  }

  private ping(): void {
    if (this.closed) return;
    const now = performance.now();
    if (this.ctl?.readyState === "open" && this.p2p) {
      try {
        this.ctl.send(JSON.stringify({ t: "ping", ts: now }));
      } catch {
        /* closed in between */
      }
    } else {
      const id = pingSeq++ >>> 0;
      this.relayPings.set(id, now);
      if (this.relayPings.size > 16) this.relayPings.delete(this.relayPings.keys().next().value!);
      this.host.sendRelayProbe(this.slot, { type: ProbeType.Ping, id, time: now });
    }
  }

  /** A pong probe that came back through the relay. */
  onRelayPong(p: Probe): void {
    if (!this.relayPings.has(p.id)) return;
    this.relayPings.delete(p.id);
    this.relayRttMs = performance.now() - p.time;
    this.host.changed();
  }

  private onCtl(data: unknown): void {
    if (typeof data !== "string") return;
    let msg: { t?: string; ts?: number };
    try {
      msg = JSON.parse(data) as { t?: string; ts?: number };
    } catch {
      return;
    }
    if (msg.t === "ping" && typeof msg.ts === "number") {
      try {
        this.ctl?.send(JSON.stringify({ t: "pong", ts: msg.ts }));
      } catch {
        /* ignore */
      }
    } else if (msg.t === "pong" && typeof msg.ts === "number") {
      this.rttMs = Math.max(0, performance.now() - msg.ts);
      this.host.changed();
    }
  }

  private teardownPc(): void {
    const pc = this.pc;
    this.pc = null;
    try {
      this.game?.close();
      this.ctl?.close();
    } catch {
      /* ignore */
    }
    this.game = null;
    this.ctl = null;
    if (pc) {
      pc.onicecandidate = null;
      pc.onconnectionstatechange = null;
      try {
        pc.close();
      } catch {
        /* ignore */
      }
    }
  }

  close(): void {
    if (this.closed) return;
    this.closed = true;
    this.clearOpenTimer();
    this.clearRetry();
    if (this.pingTimer) clearInterval(this.pingTimer);
    this.pingTimer = null;
    this.teardownPc();
  }
}

/** Keeps the set of links matching the room. */
export class PeerManager {
  readonly links = new Map<string, PeerLink>();
  private room: RoomState | null = null;
  private myPeerId: string | null = null;

  constructor(private readonly host: PeerHost) {}

  get isHost(): boolean {
    return !!this.room && this.room.hostPeerId === this.myPeerId;
  }

  update(room: RoomState | null, myPeerId: string | null): void {
    this.room = room;
    this.myPeerId = myPeerId;
    const desired = new Map<string, { slot: number; offerer: boolean }>();
    if (room && myPeerId && room.phase !== undefined) {
      const iAmHost = room.hostPeerId === myPeerId;
      if (iAmHost) {
        for (const s of room.slots) {
          if (s.peerId && s.peerId !== myPeerId && s.connected) desired.set(s.peerId, { slot: s.index, offerer: false });
        }
      } else {
        const h = room.slots.find((s) => s.peerId === room.hostPeerId);
        if (h && h.peerId && h.connected) desired.set(h.peerId, { slot: h.index, offerer: true });
      }
    }
    for (const [id, link] of this.links) {
      const want = desired.get(id);
      if (!want || want.offerer !== link.offerer) {
        link.close();
        this.links.delete(id);
      } else {
        link.slot = want.slot;
      }
    }
    for (const [id, want] of desired) {
      if (this.links.has(id)) continue;
      const link = new PeerLink(id, want.slot, want.offerer, this.host);
      this.links.set(id, link);
      link.start();
    }
    this.host.changed();
  }

  handleSignal(from: string, sig: RtcSignal): void {
    let link = this.links.get(from);
    if (!link && sig.type === "offer" && this.isHost) {
      // the offer can arrive before our room.state lists the peer
      const slot = this.room?.slots.find((s) => s.peerId === from)?.index ?? -1;
      link = new PeerLink(from, slot, false, this.host);
      this.links.set(from, link);
    }
    void link?.handleSignal(sig);
  }

  linkForSlot(slot: number): PeerLink | undefined {
    for (const l of this.links.values()) if (l.slot === slot) return l;
    return undefined;
  }

  closeAll(): void {
    for (const l of this.links.values()) l.close();
    this.links.clear();
    this.host.changed();
  }
}
