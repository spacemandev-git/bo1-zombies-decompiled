// lobby.ts - rooms, slots, signaling and the packet relay (web/shared/protocol.ts), as a pure state machine.
//
// No network or runtime APIs: the caller (server/app.ts) feeds it connection events and gives it a clock, a random
// source and send/disconnect callbacks, so tests can drive it directly.
//
//   open(conn)            a WebSocket connected
//   message(conn, data)   a text frame (JSON ClientMessage) or a binary frame (relay)
//   close(conn)           the WebSocket closed
//   tick()                call about once a second: expires dropped peers and idle rooms
//
// A connection is a transport; a peer is a player identity (peerId + resumeToken) created by hello. A peer whose
// connection drops keeps its slot (connected: false) for RESUME_WINDOW_MS and gets it back by sending hello with its
// resumeToken on a new connection; after that it is removed as if it had left.

import { buildLaunch } from "../shared/launch";
import {
  PROTOCOL_VERSION,
  RELAY_FRAME,
  type ClientMessage,
  type ErrorCode,
  type IceServer,
  type RoomSettings,
  type RoomState,
  type RoomSummary,
  type ServerMessage,
  type SlotState,
} from "../shared/protocol";
import { characterName, findMap, findMod, MAX_PLAYERS_ORIGINAL, ORIGINAL_CHARACTER_COUNT } from "../shared/roster";
import { TokenBucket } from "./ratelimit";
import { isRoomCode, ROOM_CODE_ALPHABET, ROOM_CODE_LENGTH, utf8Length, validateClientMessage } from "./validate";

export type ConnId = number;
export type Outgoing = ServerMessage | Uint8Array;
export type LobbyLogLevel = "debug" | "info" | "warn" | "error";

export interface LobbyLimits {
  resumeWindowMs: number;
  lobbyIdleMs: number;
  maxRooms: number;
  maxJsonBytes: number;
  maxRelayPayload: number;
  /** JSON messages per second per connection, and the burst allowed above it. */
  jsonPerSecond: number;
  jsonBurst: number;
  /** Over-budget JSON messages tolerated (burst, refill per second) before the connection is closed. */
  abuseBurst: number;
  abusePerSecond: number;
  relayFramesPerSecond: number;
  relayFramesBurst: number;
  relayBytesPerSecond: number;
  relayBytesBurst: number;
  maxListedRooms: number;
}

export const DEFAULT_LIMITS: LobbyLimits = {
  resumeWindowMs: 30_000,
  lobbyIdleMs: 2 * 60 * 60 * 1000,
  maxRooms: 500,
  maxJsonBytes: 32 * 1024,
  maxRelayPayload: 1400,
  jsonPerSecond: 30,
  jsonBurst: 60,
  abuseBurst: 40,
  abusePerSecond: 5,
  relayFramesPerSecond: 400,
  relayFramesBurst: 800,
  relayBytesPerSecond: 256 * 1024,
  relayBytesBurst: 512 * 1024,
  maxListedRooms: 100,
};

/** WebSocket close codes the lobby asks for. */
export const CLOSE_RATE_LIMITED = 1008;
export const CLOSE_REPLACED = 4000;

export const DEFAULT_SETTINGS: RoomSettings = {
  map: "zombie_theater",
  mods: [],
  maxPlayers: 4,
  allowDuplicates: false,
  visibility: "private",
};

export interface LobbyOptions {
  now(): number;
  randomBytes(n: number): Uint8Array;
  send(conn: ConnId, msg: Outgoing): void;
  disconnect(conn: ConnId, code: number, reason: string): void;
  iceServers: IceServer[];
  limits?: Partial<LobbyLimits>;
  log?(level: LobbyLogLevel, event: string, fields: Record<string, unknown>): void;
}

interface Conn {
  id: ConnId;
  peer: Peer | null;
  closing: boolean;
  json: TokenBucket;
  abuse: TokenBucket;
  relayFrames: TokenBucket;
  relayBytes: TokenBucket;
  lastRateError: number;
}

interface Peer {
  id: string;
  token: string;
  name: string;
  conn: Conn | null;
  room: Room | null;
  droppedAt: number | null;
  /** Set when the peer was kicked or its room closed while it was disconnected; told on resume. */
  pendingLeft: "kicked" | "closed" | null;
}

interface Room {
  code: string;
  hostPeerId: string;
  phase: RoomState["phase"];
  settings: RoomSettings;
  slots: SlotState[];
  createdAt: number;
  lastActivity: number;
}

class LobbyError extends Error {
  constructor(
    readonly code: ErrorCode,
    message: string,
  ) {
    super(message);
  }
}

function emptySlot(index: number): SlotState {
  return { index, peerId: null, name: "", character: null, ready: false, isHost: false, connected: false, hasGameFiles: false, maps: [] };
}

function hex(bytes: Uint8Array): string {
  let s = "";
  for (const b of bytes) s += b.toString(16).padStart(2, "0");
  return s;
}

function sameSettings(a: RoomSettings, b: RoomSettings): boolean {
  return (
    a.map === b.map &&
    a.maxPlayers === b.maxPlayers &&
    a.allowDuplicates === b.allowDuplicates &&
    a.visibility === b.visibility &&
    a.mods.length === b.mods.length &&
    a.mods.every((m, i) => m === b.mods[i])
  );
}

export class Lobby {
  private readonly limits: LobbyLimits;
  private readonly conns = new Map<ConnId, Conn>();
  private readonly peers = new Map<string, Peer>();
  private readonly peersByToken = new Map<string, Peer>();
  private readonly rooms = new Map<string, Room>();

  constructor(private readonly opts: LobbyOptions) {
    this.limits = { ...DEFAULT_LIMITS, ...opts.limits };
  }

  // ----- connection events -----

  open(id: ConnId): void {
    const now = this.opts.now();
    const l = this.limits;
    this.conns.set(id, {
      id,
      peer: null,
      closing: false,
      json: new TokenBucket(l.jsonBurst, l.jsonPerSecond, now),
      abuse: new TokenBucket(l.abuseBurst, l.abusePerSecond, now),
      relayFrames: new TokenBucket(l.relayFramesBurst, l.relayFramesPerSecond, now),
      relayBytes: new TokenBucket(l.relayBytesBurst, l.relayBytesPerSecond, now),
      lastRateError: -Infinity,
    });
  }

  message(id: ConnId, data: string | Uint8Array): void {
    const c = this.conns.get(id);
    if (!c || c.closing) return;
    if (typeof data !== "string") {
      this.relay(c, data);
      return;
    }
    const now = this.opts.now();
    if (!c.json.take(now)) {
      this.rateLimited(c, now);
      return;
    }
    if (data.length > this.limits.maxJsonBytes || utf8Length(data) > this.limits.maxJsonBytes) {
      this.error(c, "bad-message", `messages are limited to ${this.limits.maxJsonBytes} bytes`);
      return;
    }
    let raw: unknown;
    try {
      raw = JSON.parse(data);
    } catch {
      this.error(c, "bad-message", "the message is not valid JSON");
      return;
    }
    const parsed = validateClientMessage(raw);
    if (!parsed.ok) {
      this.error(c, "bad-message", parsed.message);
      return;
    }
    try {
      this.handle(c, parsed.value);
    } catch (e) {
      if (e instanceof LobbyError) this.error(c, e.code, e.message);
      else throw e;
    }
  }

  close(id: ConnId): void {
    const c = this.conns.get(id);
    if (!c) return;
    this.conns.delete(id);
    c.closing = true;
    const peer = c.peer;
    if (!peer || peer.conn !== c) return;
    peer.conn = null;
    peer.droppedAt = this.opts.now();
    const room = peer.room;
    if (room) {
      const slot = this.slotOf(room, peer);
      if (slot) slot.connected = false;
      this.broadcastState(room);
    }
  }

  tick(): void {
    const now = this.opts.now();
    for (const peer of this.peers.values()) {
      if (peer.conn === null && peer.droppedAt !== null && now - peer.droppedAt >= this.limits.resumeWindowMs) {
        if (peer.room) this.leaveRoom(peer, "left");
        this.peers.delete(peer.id);
        this.peersByToken.delete(peer.token);
      }
    }
    for (const room of this.rooms.values()) {
      if (room.phase === "lobby" && now - room.lastActivity > this.limits.lobbyIdleMs) this.closeRoom(room, "idle");
    }
  }

  stats(): { rooms: number; peers: number; connections: number } {
    let peers = 0;
    for (const p of this.peers.values()) if (p.conn) peers++;
    return { rooms: this.rooms.size, peers, connections: this.conns.size };
  }

  /** A copy of a room's state (tests and diagnostics). */
  roomState(code: string): RoomState | undefined {
    const room = this.rooms.get(code);
    return room ? this.snapshot(room) : undefined;
  }

  // ----- dispatch -----

  private handle(c: Conn, msg: ClientMessage): void {
    if (msg.t === "ping") {
      this.send(c, { t: "pong", time: msg.time });
      return;
    }
    if (msg.t === "hello") {
      this.hello(c, msg);
      return;
    }
    const peer = c.peer;
    if (!peer) throw new LobbyError("bad-message", "send hello first");
    switch (msg.t) {
      case "rename":
        return this.rename(peer, msg.name);
      case "room.create":
        return this.createRoom(peer, msg.settings);
      case "room.join":
        return this.joinRoom(peer, msg.code);
      case "room.leave":
        if (!peer.room) throw new LobbyError("not-in-room", "you are not in a room");
        return this.leaveRoom(peer, "left");
      case "room.list":
        return this.send(c, { t: "room.list", rooms: this.listRooms() });
      case "room.settings":
        return this.changeSettings(peer, msg.settings);
      case "room.kick":
        return this.kick(peer, msg.peerId);
      case "room.start":
        return this.start(peer);
      case "room.ended":
        return this.ended(peer);
      case "slot.character":
        return this.setCharacter(peer, msg.character);
      case "slot.ready": {
        const { room, slot } = this.mySlot(peer);
        if (room.phase !== "lobby") throw new LobbyError("room-started", "the game has already started");
        slot.ready = msg.ready;
        return this.changed(room);
      }
      case "slot.files": {
        const { room, slot } = this.mySlot(peer);
        if (msg.maps) {
          slot.maps = msg.maps;
          slot.hasGameFiles = msg.maps.includes(room.settings.map);
        } else {
          slot.hasGameFiles = msg.hasGameFiles;
        }
        return this.changed(room);
      }
      case "rtc.signal":
        return this.signal(peer, msg.to, msg.signal);
      case "net.status": {
        const { room } = this.mySlot(peer);
        const out: ServerMessage = { t: "net.status", peerId: peer.id, mode: msg.mode, rttMs: msg.rttMs };
        for (const s of room.slots) if (s.peerId && s.peerId !== peer.id) this.sendToPeer(s.peerId, out);
        return;
      }
    }
  }

  // ----- identity -----

  private hello(c: Conn, msg: Extract<ClientMessage, { t: "hello" }>): void {
    if (c.peer) throw new LobbyError("bad-message", "hello was already received on this connection");
    if (msg.version !== PROTOCOL_VERSION) {
      throw new LobbyError("version", `the server speaks protocol ${PROTOCOL_VERSION}, this page ${msg.version}: reload the page`);
    }
    const resumed = msg.resumeToken ? this.peersByToken.get(msg.resumeToken) : undefined;
    if (resumed) {
      const old = resumed.conn;
      if (old) {
        // the same player on a newer connection (e.g. the old socket has not noticed it is dead): the new one wins
        old.peer = null;
        old.closing = true;
        this.opts.disconnect(old.id, CLOSE_REPLACED, "replaced by a newer connection");
      }
      resumed.conn = c;
      resumed.droppedAt = null;
      resumed.name = msg.name;
      c.peer = resumed;
      this.send(c, { t: "welcome", peerId: resumed.id, resumeToken: resumed.token, iceServers: this.opts.iceServers });
      this.log("debug", "peer.resume", { peer: resumed.id, room: resumed.room?.code ?? null });
      const room = resumed.room;
      if (room) {
        const slot = this.slotOf(room, resumed);
        if (slot) {
          slot.connected = true;
          slot.name = resumed.name;
        }
        this.changed(room);
      } else if (resumed.pendingLeft) {
        this.send(c, { t: "room.left", reason: resumed.pendingLeft });
        resumed.pendingLeft = null;
      }
      return;
    }
    let id = "";
    do id = hex(this.opts.randomBytes(8));
    while (this.peers.has(id));
    const peer: Peer = {
      id,
      token: hex(this.opts.randomBytes(24)),
      name: msg.name,
      conn: c,
      room: null,
      droppedAt: null,
      pendingLeft: null,
    };
    this.peers.set(peer.id, peer);
    this.peersByToken.set(peer.token, peer);
    c.peer = peer;
    this.send(c, { t: "welcome", peerId: peer.id, resumeToken: peer.token, iceServers: this.opts.iceServers });
  }

  private rename(peer: Peer, name: string): void {
    peer.name = name;
    const room = peer.room;
    if (!room) return;
    const slot = this.slotOf(room, peer);
    if (slot) slot.name = name;
    this.changed(room);
  }

  // ----- rooms -----

  private createRoom(peer: Peer, patch: Partial<RoomSettings>): void {
    const settings = this.resolveSettings(DEFAULT_SETTINGS, patch, null);
    if (this.rooms.size >= this.limits.maxRooms) {
      throw new LobbyError("server-full", "the server has as many rooms as it can hold; try again later");
    }
    if (peer.room) this.leaveRoom(peer, "left");
    const now = this.opts.now();
    const room: Room = {
      code: this.newRoomCode(),
      hostPeerId: peer.id,
      phase: "lobby",
      settings,
      slots: Array.from({ length: settings.maxPlayers }, (_, i) => emptySlot(i)),
      createdAt: now,
      lastActivity: now,
    };
    this.rooms.set(room.code, room);
    this.seat(room, peer, 0, true);
    this.log("info", "room.create", { room: room.code, map: settings.map, maxPlayers: settings.maxPlayers, rooms: this.rooms.size });
    this.changed(room);
  }

  private joinRoom(peer: Peer, code: string): void {
    const room = isRoomCode(code) ? this.rooms.get(code) : undefined;
    if (!room) throw new LobbyError("room-not-found", `there is no room ${code}`);
    if (peer.room === room) {
      this.sendState(room, peer);
      return;
    }
    if (room.phase !== "lobby") throw new LobbyError("room-started", `room ${code} is already in a game`);
    if (!room.slots.some((s) => s.peerId === null)) throw new LobbyError("room-full", `room ${code} is full`);
    if (peer.room) this.leaveRoom(peer, "left");
    const free = room.slots.find((s) => s.peerId === null)!;
    this.seat(room, peer, free.index, false);
    this.changed(room);
  }

  private seat(room: Room, peer: Peer, index: number, isHost: boolean): void {
    const slot = room.slots[index]!;
    const character = this.autoCharacter(room);
    Object.assign(slot, {
      peerId: peer.id,
      name: peer.name,
      character,
      ready: false,
      isHost,
      connected: peer.conn !== null,
      hasGameFiles: false,
      maps: [],
    } satisfies Omit<SlotState, "index">);
    peer.room = room;
    peer.pendingLeft = null;
  }

  /** Lowest unused character; with duplicates on, the least used one (lowest index on ties). */
  private autoCharacter(room: Room): number {
    const counts = new Array<number>(ORIGINAL_CHARACTER_COUNT).fill(0);
    for (const s of room.slots) if (s.peerId !== null && s.character !== null) counts[s.character]!++;
    let best = 0;
    for (let i = 1; i < counts.length; i++) if (counts[i]! < counts[best]!) best = i;
    return best;
  }

  /**
   * Removes the peer from its room. reason "left": it left (or its resume window ran out); "kicked": the host kicked
   * it. The peer is told with room.left if it is connected.
   */
  private leaveRoom(peer: Peer, reason: "left" | "kicked"): void {
    const room = peer.room;
    if (!room) return;
    const slot = this.slotOf(room, peer);
    if (slot) Object.assign(slot, emptySlot(slot.index));
    peer.room = null;
    if (peer.conn) this.send(peer.conn, { t: "room.left", reason });
    else peer.pendingLeft = reason === "kicked" ? "kicked" : null;

    const seated = room.slots.filter((s) => s.peerId !== null);
    if (seated.length === 0) {
      this.deleteRoom(room, "empty");
      return;
    }
    if (room.hostPeerId === peer.id) {
      if (room.phase !== "lobby") {
        // the host's tab runs the game server: without it the game is over for everyone
        this.closeRoom(room, "host-left");
        return;
      }
      const next = seated.find((s) => s.connected) ?? seated[0]!;
      next.isHost = true;
      room.hostPeerId = next.peerId!;
      this.log("info", "room.host", { room: room.code, host: room.hostPeerId });
    }
    this.changed(room);
  }

  private closeRoom(room: Room, why: string): void {
    for (const s of room.slots) {
      if (!s.peerId) continue;
      const p = this.peers.get(s.peerId);
      if (!p) continue;
      p.room = null;
      if (p.conn) this.send(p.conn, { t: "room.left", reason: "closed" });
      else p.pendingLeft = "closed";
    }
    this.deleteRoom(room, why);
  }

  private deleteRoom(room: Room, why: string): void {
    this.rooms.delete(room.code);
    this.log("info", "room.close", { room: room.code, why, rooms: this.rooms.size });
  }

  private listRooms(): RoomSummary[] {
    const out: RoomSummary[] = [];
    const rooms = [...this.rooms.values()].sort((a, b) => b.createdAt - a.createdAt);
    for (const r of rooms) {
      if (r.settings.visibility !== "public" || r.phase !== "lobby") continue;
      const players = r.slots.filter((s) => s.peerId !== null).length;
      if (players >= r.settings.maxPlayers) continue;
      const host = r.slots.find((s) => s.peerId === r.hostPeerId);
      out.push({
        code: r.code,
        hostName: host?.name ?? "",
        map: r.settings.map,
        mods: [...r.settings.mods],
        players,
        maxPlayers: r.settings.maxPlayers,
      });
      if (out.length >= this.limits.maxListedRooms) break;
    }
    return out;
  }

  // ----- host actions -----

  private hostRoom(peer: Peer): Room {
    const room = peer.room;
    if (!room) throw new LobbyError("not-in-room", "you are not in a room");
    if (room.hostPeerId !== peer.id) throw new LobbyError("not-host", "only the host can do that");
    return room;
  }

  /** Merges a settings patch into `base` and checks the rules that span fields (and, for a room, its players). */
  private resolveSettings(base: RoomSettings, patch: Partial<RoomSettings>, room: Room | null): RoomSettings {
    const next: RoomSettings = { ...base, ...patch, mods: [...(patch.mods ?? base.mods)] };
    // a new map without an explicit mod list drops the mods that do not work on it
    if (patch.map !== undefined && patch.mods === undefined && patch.map !== base.map) {
      next.mods = next.mods.filter((id) => {
        const maps = findMod(id)?.maps;
        return !maps || maps.includes(next.map);
      });
    }
    if (next.maxPlayers > MAX_PLAYERS_ORIGINAL) next.allowDuplicates = true;
    if (!findMap(next.map)) throw new LobbyError("bad-message", `unknown map ${next.map}`);
    let gameMods = 0;
    for (const id of next.mods) {
      const mod = findMod(id);
      if (!mod) throw new LobbyError("bad-message", `unknown mod ${id}`);
      if (mod.maps && !mod.maps.includes(next.map)) {
        throw new LobbyError("bad-message", `the mod ${mod.name} only works on ${mod.maps.join(", ")}`);
      }
      if (mod.kind === "game") gameMods++;
    }
    if (gameMods > 1) throw new LobbyError("bad-message", "at most one whole-game mod can be loaded");
    if (room) {
      const seated = room.slots.filter((s) => s.peerId !== null);
      if (next.maxPlayers < seated.length) {
        throw new LobbyError(
          "bad-message",
          `${seated.length} players are in the room: maxPlayers cannot be lower than ${seated.length} (kick someone first)`,
        );
      }
      if (!next.allowDuplicates && this.hasDuplicateCharacters(room)) {
        throw new LobbyError("character-taken", "some players play the same character: they must pick different ones before duplicates can be turned off");
      }
    }
    return next;
  }

  private hasDuplicateCharacters(room: Room): boolean {
    const seen = new Set<number>();
    for (const s of room.slots) {
      if (s.peerId === null || s.character === null) continue;
      if (seen.has(s.character)) return true;
      seen.add(s.character);
    }
    return false;
  }

  private changeSettings(peer: Peer, patch: Partial<RoomSettings>): void {
    const room = this.hostRoom(peer);
    if (room.phase !== "lobby") throw new LobbyError("room-started", "settings cannot change during a game");
    const next = this.resolveSettings(room.settings, patch, room);
    if (!sameSettings(room.settings, next)) {
      if (next.maxPlayers !== room.settings.maxPlayers) this.resizeSlots(room, next.maxPlayers);
      room.settings = next;
      for (const s of room.slots) {
        if (!s.isHost) s.ready = false;
        // players who reported their map list get the new map's files flag right away (slot.files with maps)
        if (s.peerId !== null && s.maps.length) s.hasGameFiles = s.maps.includes(next.map);
      }
    }
    this.changed(room);
  }

  /** Grows or shrinks the slot list; players above the new size move down into free slots. */
  private resizeSlots(room: Room, size: number): void {
    if (size >= room.slots.length) {
      for (let i = room.slots.length; i < size; i++) room.slots.push(emptySlot(i));
      return;
    }
    const slots = Array.from({ length: size }, (_, i) => emptySlot(i));
    const moved: SlotState[] = [];
    for (const s of room.slots) {
      if (s.peerId === null) continue;
      if (s.index < size) slots[s.index] = s;
      else moved.push(s);
    }
    for (const s of moved) {
      const free = slots.find((x) => x.peerId === null)!;
      Object.assign(free, s, { index: free.index });
    }
    room.slots = slots;
  }

  private kick(peer: Peer, targetId: string): void {
    const room = this.hostRoom(peer);
    if (targetId === peer.id) throw new LobbyError("bad-message", "the host cannot kick themselves (leave the room instead)");
    const target = this.peers.get(targetId);
    if (!target || target.room !== room) throw new LobbyError("not-in-room", `peer ${targetId} is not in this room`);
    this.log("info", "room.kick", { room: room.code, peer: targetId });
    this.leaveRoom(target, "kicked");
  }

  private start(peer: Peer): void {
    const room = this.hostRoom(peer);
    if (room.phase !== "lobby") throw new LobbyError("room-started", "the game has already started");
    const seated = room.slots.filter((s) => s.peerId !== null);
    const problems: string[] = [];
    for (const s of seated) {
      const why: string[] = [];
      if (!s.connected) why.push("disconnected");
      if (!s.ready) why.push("not ready");
      if (!s.hasGameFiles) why.push("missing game files");
      if (why.length) problems.push(`${s.name} (${why.join(", ")})`);
    }
    if (problems.length) throw new LobbyError("not-ready", `not everyone is ready: ${problems.join("; ")}`);
    // players who cleared their pick get one now, so the launch never invents a duplicate
    for (const s of seated) if (s.character === null) s.character = this.autoCharacter(room);
    room.phase = "in-game";
    this.changed(room);
    const state = this.snapshot(room);
    for (const s of seated) {
      const target = this.peers.get(s.peerId!);
      if (target?.conn) this.send(target.conn, { t: "room.started", launch: buildLaunch(state, s.index) });
    }
    this.log("info", "room.start", { room: room.code, map: room.settings.map, players: seated.length, mods: room.settings.mods.join(" ") });
  }

  private ended(peer: Peer): void {
    const room = this.hostRoom(peer);
    if (room.phase === "lobby") throw new LobbyError("bad-message", "the room is not in a game");
    room.phase = "lobby";
    for (const s of room.slots) s.ready = false;
    this.log("info", "room.ended", { room: room.code });
    this.changed(room);
  }

  // ----- player actions -----

  private mySlot(peer: Peer): { room: Room; slot: SlotState } {
    const room = peer.room;
    const slot = room ? this.slotOf(room, peer) : undefined;
    if (!room || !slot) throw new LobbyError("not-in-room", "you are not in a room");
    return { room, slot };
  }

  private setCharacter(peer: Peer, character: number | null): void {
    const { room, slot } = this.mySlot(peer);
    if (room.phase !== "lobby") throw new LobbyError("room-started", "characters cannot change during a game");
    if (character !== null && !room.settings.allowDuplicates) {
      const other = room.slots.find((s) => s.peerId !== null && s !== slot && s.character === character);
      if (other) {
        throw new LobbyError("character-taken", `${other.name} already plays ${characterName(room.settings.map, character)}`);
      }
    }
    slot.character = character;
    this.changed(room);
  }

  private signal(peer: Peer, to: string, signal: Extract<ClientMessage, { t: "rtc.signal" }>["signal"]): void {
    const { room } = this.mySlot(peer);
    const target = this.peers.get(to);
    if (!target || target === peer || target.room !== room) throw new LobbyError("not-in-room", `peer ${to} is not in your room`);
    if (target.conn) this.send(target.conn, { t: "rtc.signal", from: peer.id, signal });
  }

  // ----- relay -----

  private relay(c: Conn, data: Uint8Array): void {
    const peer = c.peer;
    const room = peer?.room;
    if (!peer || !room) return;
    if (data.length < 3 || data.length > 2 + this.limits.maxRelayPayload || data[0] !== RELAY_FRAME) return;
    const now = this.opts.now();
    if (!c.relayFrames.take(now) || !c.relayBytes.take(now, data.length)) return;
    const from = this.slotOf(room, peer);
    const to = room.slots[data[1]!];
    if (!from || !to || to.peerId === null || to === from) return;
    const target = this.peers.get(to.peerId);
    if (!target?.conn || target.conn.closing) return;
    const out = new Uint8Array(data);
    out[1] = from.index;
    this.opts.send(target.conn.id, out);
  }

  // ----- helpers -----

  private rateLimited(c: Conn, now: number): void {
    if (!c.abuse.take(now)) {
      c.closing = true;
      this.log("warn", "conn.rate-limited", { conn: c.id, peer: c.peer?.id ?? null });
      this.opts.disconnect(c.id, CLOSE_RATE_LIMITED, "rate-limited");
      return;
    }
    if (now - c.lastRateError >= 1000) {
      c.lastRateError = now;
      this.error(c, "rate-limited", "too many messages: slow down");
    }
  }

  private newRoomCode(): string {
    const n = ROOM_CODE_ALPHABET.length;
    const limit = 256 - (256 % n); // rejection sampling keeps every character equally likely
    for (let attempt = 0; attempt < 1000; attempt++) {
      let code = "";
      while (code.length < ROOM_CODE_LENGTH) {
        for (const b of this.opts.randomBytes(ROOM_CODE_LENGTH * 2)) {
          if (b < limit && code.length < ROOM_CODE_LENGTH) code += ROOM_CODE_ALPHABET[b % n];
        }
      }
      if (!this.rooms.has(code)) return code;
    }
    throw new Error("could not find a free room code");
  }

  private slotOf(room: Room, peer: Peer): SlotState | undefined {
    return room.slots.find((s) => s.peerId === peer.id);
  }

  private snapshot(room: Room): RoomState {
    return {
      code: room.code,
      hostPeerId: room.hostPeerId,
      phase: room.phase,
      settings: { ...room.settings, mods: [...room.settings.mods] },
      slots: room.slots.map((s) => ({ ...s })),
    };
  }

  /** Marks the room active and sends its state to every member. */
  private changed(room: Room): void {
    room.lastActivity = this.opts.now();
    this.broadcastState(room);
  }

  private broadcastState(room: Room): void {
    const state = this.snapshot(room);
    for (const s of room.slots) {
      if (!s.peerId) continue;
      const p = this.peers.get(s.peerId);
      if (p?.conn) this.send(p.conn, { t: "room.state", room: state, yourPeerId: p.id });
    }
  }

  private sendState(room: Room, peer: Peer): void {
    if (peer.conn) this.send(peer.conn, { t: "room.state", room: this.snapshot(room), yourPeerId: peer.id });
  }

  private sendToPeer(peerId: string, msg: ServerMessage): void {
    const p = this.peers.get(peerId);
    if (p?.conn) this.send(p.conn, msg);
  }

  private send(c: Conn, msg: ServerMessage): void {
    if (!c.closing) this.opts.send(c.id, msg);
  }

  private error(c: Conn, code: ErrorCode, message: string): void {
    this.send(c, { t: "error", code, message });
  }

  private log(level: LobbyLogLevel, event: string, fields: Record<string, unknown>): void {
    this.opts.log?.(level, event, fields);
  }
}
