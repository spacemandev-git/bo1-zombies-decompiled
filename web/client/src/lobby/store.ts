// store.ts - the lobby state as the server reports it (room, launch, public rooms, peers' network status).

import type { LaunchConfig, RoomState, RoomSummary, SlotState } from "../../../shared/protocol";
import type { Signaling } from "../net/signaling";
import type { NetTransport } from "../net/transport";
import { toast } from "../ui/dom";

export type LobbyEvent =
  | { type: "welcome"; newSession: boolean }
  | { type: "room"; prev: RoomState | null }
  | { type: "left"; reason: "left" | "kicked" | "closed" | "lost" }
  | { type: "started"; launch: LaunchConfig }
  | { type: "list" }
  | { type: "net" };

const ERROR_TEXT: Record<string, string> = {
  "room-not-found": "No game with that code.",
  "room-full": "That game is full.",
  "server-full": "The server is full right now. Try again in a few minutes.",
  "room-started": "That game has already started.",
  "not-host": "Only the host can do that.",
  "character-taken": "That character is taken.",
  "not-ready": "Not everyone is ready yet.",
  "rate-limited": "Slow down a little.",
  version: "The server was updated. Reload the page.",
  "not-in-room": "You are not in a game.",
  "bad-message": "The server did not understand a request.",
};

export class LobbyStore {
  room: RoomState | null = null;
  myPeerId: string | null = null;
  launch: LaunchConfig | null = null;
  netStatus = new Map<string, { mode: "p2p" | "relay"; rttMs: number | null }>();
  publicRooms: RoomSummary[] = [];
  listLoaded = false;
  private listeners = new Set<(e: LobbyEvent) => void>();

  constructor(
    private readonly sig: Signaling,
    private readonly transport: NetTransport,
  ) {
    sig.on("welcome", (m) => {
      const newSession = this.myPeerId !== null && this.myPeerId !== m.peerId;
      this.myPeerId = m.peerId;
      if (newSession && this.room) {
        // the server did not resume our session: the old room is gone for us
        this.setRoom(null);
        this.emit({ type: "left", reason: "lost" });
      }
      this.emit({ type: "welcome", newSession });
    });
    sig.on("room.state", (m) => {
      const prev = this.room;
      this.myPeerId = m.yourPeerId;
      this.setRoom(m.room);
      if (prev && prev.code === m.room.code && prev.hostPeerId !== m.room.hostPeerId) {
        const host = m.room.slots.find((s) => s.peerId === m.room.hostPeerId);
        toast(m.room.hostPeerId === this.myPeerId ? "You are now the host." : `${host?.name ?? "Someone"} is now the host.`, "info");
      }
      if (m.room.phase === "lobby") this.launch = null;
      this.emit({ type: "room", prev });
    });
    sig.on("room.left", (m) => {
      this.setRoom(null);
      this.launch = null;
      if (m.reason === "kicked") toast("The host removed you from the game.", "warn");
      else if (m.reason === "closed") toast("The game was closed.", "warn");
      this.emit({ type: "left", reason: m.reason });
    });
    sig.on("room.started", (m) => {
      this.launch = m.launch;
      this.emit({ type: "started", launch: m.launch });
    });
    sig.on("room.list", (m) => {
      this.publicRooms = m.rooms;
      this.listLoaded = true;
      this.emit({ type: "list" });
    });
    sig.on("net.status", (m) => {
      this.netStatus.set(m.peerId, { mode: m.mode, rttMs: m.rttMs });
      this.emit({ type: "net" });
    });
    sig.on("error", (m) => {
      // signaling to a peer that just left: harmless, the peer links follow the next room.state
      if (m.code === "not-in-room" && /^peer /.test(m.message)) {
        console.debug(`[lobby] ${m.message}`);
        return;
      }
      const known = ERROR_TEXT[m.code];
      // the server's own text is more specific for these
      const detailed = m.code === "bad-message" || m.code === "not-ready" || m.code === "character-taken" || m.code === "room-full";
      const text = detailed && m.message ? m.message : (known ?? m.message ?? m.code);
      toast(text.charAt(0).toUpperCase() + text.slice(1), "error");
    });
    transport.onChange(() => this.emit({ type: "net" }));
  }

  private setRoom(room: RoomState | null): void {
    this.room = room;
    if (!room) this.netStatus.clear();
    this.transport.setRoom(room, this.myPeerId);
  }

  on(fn: (e: LobbyEvent) => void): () => void {
    this.listeners.add(fn);
    return () => this.listeners.delete(fn);
  }

  private emit(e: LobbyEvent): void {
    for (const fn of [...this.listeners]) fn(e);
  }

  get mySlot(): SlotState | null {
    return this.room?.slots.find((s) => s.peerId === this.myPeerId) ?? null;
  }

  get isHost(): boolean {
    return !!this.room && this.room.hostPeerId === this.myPeerId;
  }

  get hostSlot(): SlotState | null {
    return this.room?.slots.find((s) => s.peerId === this.room?.hostPeerId) ?? null;
  }

  /** Mode and RTT to show for a seated player: the local link when we have one, else what the server relayed. */
  netFor(slot: SlotState): { mode: "p2p" | "relay" | "host" | "self" | "unknown"; rttMs: number | null } {
    if (!slot.peerId) return { mode: "unknown", rttMs: null };
    if (slot.peerId === this.myPeerId) return { mode: this.isHost ? "host" : "self", rttMs: null };
    if (slot.isHost) {
      const st = this.transport.status(slot.index);
      return { mode: "host", rttMs: st?.rttMs ?? null };
    }
    const local = this.isHost ? this.transport.status(slot.index) : null;
    const reported = this.netStatus.get(slot.peerId);
    if (local && local.mode !== "self") return { mode: local.mode, rttMs: local.rttMs ?? reported?.rttMs ?? null };
    return reported ? { mode: reported.mode, rttMs: reported.rttMs } : { mode: "unknown", rttMs: null };
  }
}
