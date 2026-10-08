// server.lobby.test.ts - the lobby state machine (web/server/lobby.ts), driven directly with a fake clock.

import { describe, expect, test } from "bun:test";
import { assignClientNums, buildLaunch } from "../shared/launch";
import { PROTOCOL_VERSION, RELAY_FRAME } from "../shared/protocol";
import { CLOSE_RATE_LIMITED, CLOSE_REPLACED, DEFAULT_LIMITS } from "../server/lobby";
import { ROOM_CODE_ALPHABET } from "../server/validate";
import { LobbyHarness } from "./helpers-server";

function frame(slot: number, payload: number[] | Uint8Array): Uint8Array {
  const out = new Uint8Array(2 + payload.length);
  out[0] = RELAY_FRAME;
  out[1] = slot;
  out.set(payload, 2);
  return out;
}

describe("hello", () => {
  test("welcome carries a peer id, a resume token and the ICE servers", () => {
    const h = new LobbyHarness();
    const c = h.connect();
    h.send(c, { t: "hello", version: PROTOCOL_VERSION, name: "  Alice  " });
    const w = h.last(c, "welcome")!;
    expect(w.peerId).toMatch(/^[0-9a-f]{16}$/);
    expect(w.resumeToken).toMatch(/^[0-9a-f]{48}$/);
    expect(w.iceServers).toEqual([{ urls: ["stun:stun.example:3478"] }]);
  });

  test("wrong protocol version is rejected with 'version'", () => {
    const h = new LobbyHarness();
    const c = h.connect();
    h.send(c, { t: "hello", version: PROTOCOL_VERSION + 1, name: "Alice" });
    expect(h.lastError(c)?.code).toBe("version");
    expect(h.last(c, "welcome")).toBeUndefined();
  });

  test("messages before hello are rejected, ping is answered", () => {
    const h = new LobbyHarness();
    const c = h.connect();
    h.send(c, { t: "room.list" });
    expect(h.lastError(c)?.code).toBe("bad-message");
    h.send(c, { t: "ping", time: 1234.5 });
    expect(h.last(c, "pong")).toEqual({ t: "pong", time: 1234.5 });
  });

  test("a second hello on the same connection is rejected", () => {
    const h = new LobbyHarness();
    const a = h.player("Alice");
    h.send(a.conn, { t: "hello", version: 1, name: "Alice" });
    expect(h.lastError(a.conn)?.code).toBe("bad-message");
  });
});

describe("rooms", () => {
  test("create seats the creator in slot 0 as host with the default settings", () => {
    const h = new LobbyHarness();
    const a = h.player("Alice");
    const code = h.create(a);
    expect(code).toHaveLength(5);
    for (const ch of code) expect(ROOM_CODE_ALPHABET).toContain(ch);
    const s = h.state(a.conn);
    expect(s.hostPeerId).toBe(a.peerId);
    expect(s.phase).toBe("lobby");
    expect(s.settings).toEqual({ map: "zombie_theater", mods: [], maxPlayers: 4, allowDuplicates: false, visibility: "private" });
    expect(s.slots).toHaveLength(4);
    expect(s.slots[0]).toMatchObject({ index: 0, peerId: a.peerId, name: "Alice", isHost: true, character: 0, connected: true, ready: false });
    expect(s.slots.slice(1).every((x) => x.peerId === null)).toBe(true);
  });

  test("create merges validated settings", () => {
    const h = new LobbyHarness();
    const a = h.player("Alice");
    h.create(a, { map: "zombie_pentagon", mods: ["zinfo", "sandbox"], maxPlayers: 2, visibility: "public" });
    const s = h.state(a.conn);
    expect(s.settings).toEqual({ map: "zombie_pentagon", mods: ["zinfo", "sandbox"], maxPlayers: 2, allowDuplicates: false, visibility: "public" });
    expect(s.slots).toHaveLength(2);
  });

  test("room codes are unique", () => {
    const h = new LobbyHarness();
    const codes = new Set<string>();
    for (let i = 0; i < 200; i++) codes.add(h.create(h.player(`P${i}`)));
    expect(codes.size).toBe(200);
  });

  test("join takes the lowest free slot and the lowest unused character; everyone gets room.state", () => {
    const h = new LobbyHarness();
    const a = h.player("Alice");
    const b = h.player("Bob");
    const code = h.create(a);
    h.join(b, code.toLowerCase());
    const sb = h.state(b.conn);
    expect(h.last(b.conn, "room.state")!.yourPeerId).toBe(b.peerId);
    expect(h.last(a.conn, "room.state")!.yourPeerId).toBe(a.peerId);
    expect(sb.slots[1]).toMatchObject({ peerId: b.peerId, name: "Bob", character: 1, isHost: false });
    expect(h.state(a.conn)).toEqual(sb);
  });

  test("unknown room code", () => {
    const h = new LobbyHarness();
    const a = h.player("Alice");
    h.join(a, "ZZZZZ");
    expect(h.lastError(a.conn)?.code).toBe("room-not-found");
    h.join(a, "O0I1L");
    expect(h.lastError(a.conn)?.code).toBe("room-not-found");
  });

  test("full room", () => {
    const h = new LobbyHarness();
    const a = h.player("Alice");
    const code = h.create(a, { maxPlayers: 2 });
    h.join(h.player("Bob"), code);
    const c = h.player("Carol");
    h.join(c, code);
    expect(h.lastError(c.conn)?.code).toBe("room-full");
  });

  test("joining another room leaves the first", () => {
    const h = new LobbyHarness();
    const a = h.player("Alice");
    const b = h.player("Bob");
    const c = h.player("Carol");
    const one = h.create(a);
    h.join(c, one);
    const two = h.create(b);
    h.join(c, two);
    expect(h.messages(c.conn).some((m) => m.t === "room.left" && m.reason === "left")).toBe(true);
    expect(h.state(c.conn).code).toBe(two);
    expect(h.state(a.conn).slots.filter((s) => s.peerId !== null)).toHaveLength(1);
  });

  test("kick: host only; the kicked player gets room.left kicked", () => {
    const h = new LobbyHarness();
    const a = h.player("Alice");
    const b = h.player("Bob");
    const c = h.player("Carol");
    const code = h.create(a);
    h.join(b, code);
    h.join(c, code);
    h.send(b.conn, { t: "room.kick", peerId: c.peerId });
    expect(h.lastError(b.conn)?.code).toBe("not-host");
    h.send(a.conn, { t: "room.kick", peerId: a.peerId });
    expect(h.lastError(a.conn)?.code).toBe("bad-message");
    h.send(a.conn, { t: "room.kick", peerId: c.peerId });
    expect(h.last(c.conn, "room.left")).toEqual({ t: "room.left", reason: "kicked" });
    expect(h.state(a.conn).slots.some((s) => s.peerId === c.peerId)).toBe(false);
    expect(h.state(b.conn).slots.some((s) => s.peerId === c.peerId)).toBe(false);
  });

  test("host leaving the lobby promotes the lowest-slot connected player", () => {
    const h = new LobbyHarness();
    const a = h.player("Alice");
    const b = h.player("Bob");
    const c = h.player("Carol");
    const code = h.create(a);
    h.join(b, code);
    h.join(c, code);
    h.lobby.close(b.conn); // Bob dropped (still seated, not connected)
    h.send(a.conn, { t: "room.leave" });
    expect(h.last(a.conn, "room.left")?.reason).toBe("left");
    const s = h.state(c.conn);
    expect(s.hostPeerId).toBe(c.peerId);
    expect(s.slots[2]!.isHost).toBe(true);
    expect(s.slots[1]!.isHost).toBe(false);
  });

  test("host leaving in-game closes the room for everyone", () => {
    const h = new LobbyHarness();
    const a = h.player("Alice");
    const b = h.player("Bob");
    const code = h.create(a);
    h.join(b, code);
    h.readyUp(a);
    h.readyUp(b);
    h.send(a.conn, { t: "room.start" });
    expect(h.state(b.conn).phase).toBe("in-game");
    h.send(a.conn, { t: "room.leave" });
    expect(h.last(b.conn, "room.left")).toEqual({ t: "room.left", reason: "closed" });
    expect(h.lobby.roomState(code)).toBeUndefined();
    const c = h.player("Carol");
    h.join(c, code);
    expect(h.lastError(c.conn)?.code).toBe("room-not-found");
  });

  test("the last player leaving deletes the room", () => {
    const h = new LobbyHarness();
    const a = h.player("Alice");
    const code = h.create(a);
    expect(h.lobby.stats().rooms).toBe(1);
    h.send(a.conn, { t: "room.leave" });
    expect(h.lobby.roomState(code)).toBeUndefined();
    expect(h.lobby.stats().rooms).toBe(0);
    h.send(a.conn, { t: "room.leave" });
    expect(h.lastError(a.conn)?.code).toBe("not-in-room");
  });

  test("joining a room that is in a game is refused", () => {
    const h = new LobbyHarness();
    const a = h.player("Alice");
    const code = h.create(a);
    h.readyUp(a);
    h.send(a.conn, { t: "room.start" });
    const b = h.player("Bob");
    h.join(b, code);
    expect(h.lastError(b.conn)?.code).toBe("room-started");
  });

  test("room.list shows public lobby rooms with free slots", () => {
    const h = new LobbyHarness();
    const pub = h.create(h.player("Pub"), { visibility: "public", map: "zombie_moon", mods: ["zinfo"] });
    h.create(h.player("Priv"));
    const full = h.player("Full");
    const fullCode = h.create(full, { visibility: "public", maxPlayers: 1 });
    const started = h.player("Started");
    h.create(started, { visibility: "public" });
    h.readyUp(started);
    h.send(started.conn, { t: "room.start" });
    const x = h.player("X");
    h.send(x.conn, { t: "room.list" });
    const list = h.last(x.conn, "room.list")!.rooms;
    expect(list).toEqual([{ code: pub, hostName: "Pub", map: "zombie_moon", mods: ["zinfo"], players: 1, maxPlayers: 4 }]);
    expect(list.some((r) => r.code === fullCode)).toBe(false);
  });

  test("MAX_ROOMS limits room creation", () => {
    const h = new LobbyHarness({ maxRooms: 2 });
    h.create(h.player("A"));
    h.create(h.player("B"));
    const c = h.player("C");
    h.send(c.conn, { t: "room.create", settings: {} });
    expect(h.lastError(c.conn)?.code).toBe("server-full");
    expect(h.lobby.stats().rooms).toBe(2);
  });

  test("lobby rooms idle for more than 2 hours are closed", () => {
    const h = new LobbyHarness();
    const a = h.player("Alice");
    const code = h.create(a);
    h.clock.advance(DEFAULT_LIMITS.lobbyIdleMs - 1000);
    h.lobby.tick();
    expect(h.lobby.roomState(code)).toBeDefined();
    h.clock.advance(2000);
    h.lobby.tick();
    expect(h.lobby.roomState(code)).toBeUndefined();
    expect(h.last(a.conn, "room.left")?.reason).toBe("closed");
  });

  test("rename updates the slot name", () => {
    const h = new LobbyHarness();
    const a = h.player("Alice");
    h.create(a);
    h.send(a.conn, { t: "rename", name: "Alicia" });
    expect(h.state(a.conn).slots[0]!.name).toBe("Alicia");
  });
});

describe("characters and settings", () => {
  test("characters are unique unless duplicates are allowed", () => {
    const h = new LobbyHarness();
    const a = h.player("Alice");
    const b = h.player("Bob");
    const code = h.create(a);
    h.join(b, code);
    h.send(b.conn, { t: "slot.character", character: 0 });
    expect(h.lastError(b.conn)?.code).toBe("character-taken");
    expect(h.lastError(b.conn)?.message).toContain("Alice");
    h.send(b.conn, { t: "slot.character", character: 3 });
    expect(h.state(b.conn).slots[1]!.character).toBe(3);
    h.send(b.conn, { t: "slot.character", character: null });
    expect(h.state(b.conn).slots[1]!.character).toBeNull();
    h.send(a.conn, { t: "room.settings", settings: { allowDuplicates: true } });
    h.send(b.conn, { t: "slot.character", character: 0 });
    expect(h.state(b.conn).slots[1]!.character).toBe(0);
  });

  test("with duplicates on, joiners get the least-used character", () => {
    const h = new LobbyHarness();
    const a = h.player("A");
    const code = h.create(a, { maxPlayers: 8 });
    const others = ["B", "C", "D", "E", "F"].map((n) => h.player(n));
    for (const p of others) h.join(p, code);
    expect(h.state(a.conn).slots.map((s) => s.character)).toEqual([0, 1, 2, 3, 0, 1, null, null]);
    // B switches from 1 to 0: counts become [3, 1, 1, 1], so the next joiner gets 1 (lowest of the least used)
    h.send(others[0]!.conn, { t: "slot.character", character: 0 });
    const g = h.player("G");
    h.join(g, code);
    expect(h.state(g.conn).slots[6]!.character).toBe(1);
  });

  test("maxPlayers above 4 forces duplicates on", () => {
    const h = new LobbyHarness();
    const a = h.player("Alice");
    h.create(a, { maxPlayers: 6, allowDuplicates: false });
    expect(h.state(a.conn).settings.allowDuplicates).toBe(true);
    h.send(a.conn, { t: "room.settings", settings: { maxPlayers: 4, allowDuplicates: false } });
    expect(h.state(a.conn).settings).toMatchObject({ maxPlayers: 4, allowDuplicates: false });
    h.send(a.conn, { t: "room.settings", settings: { maxPlayers: 8, allowDuplicates: false } });
    expect(h.state(a.conn).settings).toMatchObject({ maxPlayers: 8, allowDuplicates: true });
    expect(h.state(a.conn).slots).toHaveLength(8);
  });

  test("turning duplicates off while two players share a character is refused", () => {
    const h = new LobbyHarness();
    const a = h.player("Alice");
    const b = h.player("Bob");
    const code = h.create(a, { allowDuplicates: true });
    h.join(b, code);
    h.send(b.conn, { t: "slot.character", character: 0 });
    h.send(a.conn, { t: "room.settings", settings: { allowDuplicates: false } });
    expect(h.lastError(a.conn)?.code).toBe("character-taken");
    expect(h.lobby.roomState(code)!.settings.allowDuplicates).toBe(true);
  });

  test("maxPlayers cannot drop below the number of seated players; shrinking moves players down", () => {
    const h = new LobbyHarness();
    const a = h.player("A");
    const b = h.player("B");
    const c = h.player("C");
    const code = h.create(a);
    h.join(b, code);
    h.join(c, code);
    h.send(b.conn, { t: "room.leave" }); // slots: A, -, C, -
    h.send(a.conn, { t: "room.settings", settings: { maxPlayers: 1 } });
    const err = h.lastError(a.conn)!;
    expect(err.code).toBe("bad-message");
    expect(err.message).toContain("2 players");
    h.send(a.conn, { t: "room.settings", settings: { maxPlayers: 2 } });
    const s = h.state(c.conn);
    expect(s.slots).toHaveLength(2);
    expect(s.slots.map((x) => x.peerId)).toEqual([a.peerId, c.peerId]);
    expect(s.slots[1]!.index).toBe(1);
  });

  test("a settings change un-readies everyone but the host; a map change keeps characters", () => {
    const h = new LobbyHarness();
    const a = h.player("Alice");
    const b = h.player("Bob");
    const code = h.create(a);
    h.join(b, code);
    h.send(b.conn, { t: "slot.character", character: 3 });
    h.readyUp(a);
    h.readyUp(b);
    expect(h.state(a.conn).slots.map((s) => s.ready)).toEqual([true, true, false, false]);
    h.send(a.conn, { t: "room.settings", settings: { map: "zombie_coast" } });
    const s = h.state(b.conn);
    expect(s.settings.map).toBe("zombie_coast");
    expect(s.slots[0]).toMatchObject({ ready: true, character: 0 });
    expect(s.slots[1]).toMatchObject({ ready: false, character: 3, hasGameFiles: true });
  });

  test("an unchanged settings message does not un-ready anyone", () => {
    const h = new LobbyHarness();
    const a = h.player("Alice");
    const b = h.player("Bob");
    const code = h.create(a);
    h.join(b, code);
    h.readyUp(b);
    h.send(a.conn, { t: "room.settings", settings: { map: "zombie_theater" } });
    expect(h.state(b.conn).slots[1]!.ready).toBe(true);
  });

  test("settings are host-only and lobby-only", () => {
    const h = new LobbyHarness();
    const a = h.player("Alice");
    const b = h.player("Bob");
    const code = h.create(a);
    h.join(b, code);
    h.send(b.conn, { t: "room.settings", settings: { maxPlayers: 3 } });
    expect(h.lastError(b.conn)?.code).toBe("not-host");
    h.readyUp(a);
    h.readyUp(b);
    h.send(a.conn, { t: "room.start" });
    h.send(a.conn, { t: "room.settings", settings: { maxPlayers: 3 } });
    expect(h.lastError(a.conn)?.code).toBe("room-started");
    h.send(b.conn, { t: "slot.character", character: 2 });
    expect(h.lastError(b.conn)?.code).toBe("room-started");
  });

  test("mods must fit the map; a map change drops mods that do not fit it", () => {
    const h = new LobbyHarness();
    const a = h.player("Alice");
    const c = h.connect();
    h.send(c, { t: "hello", version: 1, name: "C" });
    h.send(c, { t: "room.create", settings: { map: "zombie_theater", mods: ["sandbox"] } });
    expect(h.lastError(c)?.code).toBe("bad-message");
    expect(h.lastError(c)?.message).toContain("zombie_pentagon");
    h.create(a, { map: "zombie_pentagon", mods: ["sandbox", "horde"] });
    h.send(a.conn, { t: "room.settings", settings: { map: "zombie_moon" } });
    expect(h.state(a.conn).settings).toMatchObject({ map: "zombie_moon", mods: ["horde"] });
    h.send(a.conn, { t: "room.settings", settings: { mods: ["sandbox"] } });
    expect(h.lastError(a.conn)?.code).toBe("bad-message");
  });
});

describe("start", () => {
  test("start needs everyone ready with game files, and names who is not", () => {
    const h = new LobbyHarness();
    const a = h.player("Alice");
    const b = h.player("Bob");
    const code = h.create(a);
    h.join(b, code);
    h.send(b.conn, { t: "room.start" });
    expect(h.lastError(b.conn)?.code).toBe("not-host");
    h.readyUp(a);
    h.send(b.conn, { t: "slot.ready", ready: true });
    h.send(a.conn, { t: "room.start" });
    const err = h.lastError(a.conn)!;
    expect(err.code).toBe("not-ready");
    expect(err.message).toContain("Bob (missing game files)");
    expect(err.message).not.toContain("Alice");
    h.send(b.conn, { t: "slot.files", hasGameFiles: true });
    h.send(b.conn, { t: "slot.ready", ready: false });
    h.send(a.conn, { t: "room.start" });
    expect(h.lastError(a.conn)!.message).toContain("Bob (not ready)");
    expect(h.lobby.roomState(code)!.phase).toBe("lobby");
  });

  test("a disconnected player blocks the start", () => {
    const h = new LobbyHarness();
    const a = h.player("Alice");
    const b = h.player("Bob");
    h.join(b, h.create(a));
    h.readyUp(a);
    h.readyUp(b);
    h.lobby.close(b.conn);
    h.send(a.conn, { t: "room.start" });
    expect(h.lastError(a.conn)?.message).toContain("Bob (disconnected)");
  });

  test("every player gets room.started with their own slot and the launch config", () => {
    const h = new LobbyHarness();
    const a = h.player("Alice");
    const b = h.player("Bob");
    const c = h.player("Carol");
    const code = h.create(a, { map: "zombie_pentagon", mods: ["zinfo"] });
    h.join(b, code);
    h.join(c, code);
    h.send(b.conn, { t: "slot.character", character: 3 });
    for (const p of [a, b, c]) h.readyUp(p);
    h.send(a.conn, { t: "room.start" });
    const state = h.lobby.roomState(code)!;
    expect(state.phase).toBe("in-game");
    for (const [p, slot] of [
      [a, 0],
      [b, 1],
      [c, 2],
    ] as const) {
      const launch = h.last(p.conn, "room.started")!.launch;
      expect(launch).toEqual(buildLaunch(state, slot));
      expect(launch.yourSlot).toBe(slot);
    }
    const launch = h.last(a.conn, "room.started")!.launch;
    expect(launch).toMatchObject({ roomCode: code, map: "zombie_pentagon", mods: ["zinfo"], hostSlot: 0, maxPlayers: 4 });
    expect(launch.players).toEqual([
      { slot: 0, clientNum: 0, name: "Alice", character: 0 },
      { slot: 1, clientNum: 3, name: "Bob", character: 3 },
      { slot: 2, clientNum: 2, name: "Carol", character: 2 },
    ]);
  });

  test("duplicates and more than four players get client numbers from 4 up", () => {
    const h = new LobbyHarness();
    const host = h.player("P0");
    const code = h.create(host, { maxPlayers: 6 });
    const ps = [host, ...["P1", "P2", "P3", "P4", "P5"].map((n) => h.player(n))];
    for (const p of ps.slice(1)) h.join(p, code);
    h.send(ps[2]!.conn, { t: "slot.character", character: 0 }); // P2 duplicates P0
    for (const p of ps) h.readyUp(p);
    h.send(host.conn, { t: "room.start" });
    const launch = h.last(ps[5]!.conn, "room.started")!.launch;
    expect(launch.yourSlot).toBe(5);
    const chars = launch.players.map((p) => p.character);
    expect(chars).toEqual([0, 1, 0, 3, 0, 1]);
    const nums = assignClientNums(launch.players.map((p) => ({ slot: p.slot, character: p.character })));
    expect(launch.players.map((p) => p.clientNum)).toEqual(launch.players.map((p) => nums.get(p.slot)!));
    expect(launch.players.map((p) => p.clientNum)).toEqual([0, 1, 4, 3, 5, 6]);
  });

  test("a cleared character is filled in at start without creating a duplicate", () => {
    const h = new LobbyHarness();
    const a = h.player("Alice");
    const b = h.player("Bob");
    h.join(b, h.create(a));
    h.send(b.conn, { t: "slot.character", character: null });
    h.readyUp(a);
    h.readyUp(b);
    h.send(a.conn, { t: "room.start" });
    const launch = h.last(b.conn, "room.started")!.launch;
    expect(launch.players.map((p) => p.character)).toEqual([0, 1]);
  });

  test("room.ended returns to the lobby and un-readies everyone", () => {
    const h = new LobbyHarness();
    const a = h.player("Alice");
    const b = h.player("Bob");
    const code = h.create(a);
    h.join(b, code);
    h.readyUp(a);
    h.readyUp(b);
    h.send(a.conn, { t: "room.start" });
    h.send(b.conn, { t: "room.ended" });
    expect(h.lastError(b.conn)?.code).toBe("not-host");
    h.send(a.conn, { t: "room.ended" });
    const s = h.state(b.conn);
    expect(s.phase).toBe("lobby");
    expect(s.slots.every((x) => !x.ready)).toBe(true);
    h.send(a.conn, { t: "room.ended" });
    expect(h.lastError(a.conn)?.code).toBe("bad-message");
  });
});

describe("resume", () => {
  test("a peer that reconnects within 30 s gets its peer id and slot back", () => {
    const h = new LobbyHarness();
    const a = h.player("Alice");
    const b = h.player("Bob");
    const code = h.create(a);
    h.join(b, code);
    h.send(b.conn, { t: "slot.character", character: 2 });
    h.lobby.close(b.conn);
    expect(h.state(a.conn).slots[1]).toMatchObject({ peerId: b.peerId, connected: false, character: 2 });
    h.clock.advance(29_000);
    h.lobby.tick();
    const b2 = h.player("Bob", b.token);
    expect(b2.peerId).toBe(b.peerId);
    expect(b2.token).toBe(b.token);
    expect(h.state(b2.conn).slots[1]).toMatchObject({ peerId: b.peerId, connected: true, character: 2 });
    expect(h.state(a.conn).slots[1]!.connected).toBe(true);
  });

  test("after 30 s the peer is removed as if it left, and its token no longer resumes", () => {
    const h = new LobbyHarness();
    const a = h.player("Alice");
    const b = h.player("Bob");
    const code = h.create(a);
    h.join(b, code);
    h.lobby.close(b.conn);
    h.clock.advance(30_000);
    h.lobby.tick();
    expect(h.state(a.conn).slots.some((s) => s.peerId === b.peerId)).toBe(false);
    const b2 = h.player("Bob", b.token);
    expect(b2.peerId).not.toBe(b.peerId);
    expect(h.last(b2.conn, "room.state")).toBeUndefined();
  });

  test("a host that does not come back in time hands the lobby to the next player", () => {
    const h = new LobbyHarness();
    const a = h.player("Alice");
    const b = h.player("Bob");
    const code = h.create(a);
    h.join(b, code);
    h.lobby.close(a.conn);
    h.clock.advance(31_000);
    h.lobby.tick();
    expect(h.state(b.conn).hostPeerId).toBe(b.peerId);
  });

  test("a host that drops in-game and does not come back closes the room", () => {
    const h = new LobbyHarness();
    const a = h.player("Alice");
    const b = h.player("Bob");
    const code = h.create(a);
    h.join(b, code);
    h.readyUp(a);
    h.readyUp(b);
    h.send(a.conn, { t: "room.start" });
    h.lobby.close(a.conn);
    h.clock.advance(31_000);
    h.lobby.tick();
    expect(h.last(b.conn, "room.left")?.reason).toBe("closed");
    expect(h.lobby.roomState(code)).toBeUndefined();
  });

  test("a player kicked while disconnected is told on resume", () => {
    const h = new LobbyHarness();
    const a = h.player("Alice");
    const b = h.player("Bob");
    h.join(b, h.create(a));
    h.lobby.close(b.conn);
    h.send(a.conn, { t: "room.kick", peerId: b.peerId });
    const b2 = h.player("Bob", b.token);
    expect(h.last(b2.conn, "room.left")?.reason).toBe("kicked");
  });

  test("resuming on a new connection while the old one is open replaces it", () => {
    const h = new LobbyHarness();
    const a = h.player("Alice");
    h.create(a);
    const a2 = h.player("Alice", a.token);
    expect(a2.peerId).toBe(a.peerId);
    expect(h.disconnects).toEqual([{ conn: a.conn, code: CLOSE_REPLACED, reason: "replaced by a newer connection" }]);
    h.lobby.close(a.conn); // the old socket's close event must not mark the peer disconnected
    expect(h.state(a2.conn).slots[0]!.connected).toBe(true);
    h.send(a.conn, { t: "room.list" });
    expect(h.messages(a.conn).some((m) => m.t === "room.list")).toBe(false);
  });
});

describe("signaling", () => {
  test("rtc.signal is forwarded only inside the same room", () => {
    const h = new LobbyHarness();
    const a = h.player("Alice");
    const b = h.player("Bob");
    const x = h.player("Xavier");
    h.join(b, h.create(a));
    h.create(x);
    const offer = { type: "offer" as const, sdp: "v=0\r\n" };
    h.send(b.conn, { t: "rtc.signal", to: a.peerId, signal: offer });
    expect(h.last(a.conn, "rtc.signal")).toEqual({ t: "rtc.signal", from: b.peerId, signal: offer });
    h.send(x.conn, { t: "rtc.signal", to: a.peerId, signal: offer });
    expect(h.lastError(x.conn)?.code).toBe("not-in-room");
    const cand = { type: "candidate" as const, candidate: { candidate: "candidate:1 1 udp 1 1.2.3.4 5 typ host", sdpMid: "0", sdpMLineIndex: 0, extra: 1 } };
    h.send(a.conn, { t: "rtc.signal", to: b.peerId, signal: cand });
    expect(h.last(b.conn, "rtc.signal")!.signal).toEqual({
      type: "candidate",
      candidate: { candidate: "candidate:1 1 udp 1 1.2.3.4 5 typ host", sdpMid: "0", sdpMLineIndex: 0 },
    });
  });

  test("net.status is rebroadcast to the rest of the room", () => {
    const h = new LobbyHarness();
    const a = h.player("Alice");
    const b = h.player("Bob");
    h.join(b, h.create(a));
    h.send(b.conn, { t: "net.status", mode: "relay", rttMs: 41.6 });
    expect(h.last(a.conn, "net.status")).toEqual({ t: "net.status", peerId: b.peerId, mode: "relay", rttMs: 42 });
    expect(h.last(b.conn, "net.status")).toBeUndefined();
  });
});

describe("relay", () => {
  function room() {
    const h = new LobbyHarness();
    const a = h.player("Host");
    const b = h.player("Client");
    const c = h.player("Other");
    const code = h.create(a);
    h.join(b, code);
    h.join(c, code);
    for (const p of [a, b, c]) h.take(p.conn);
    return { h, a, b, c };
  }

  test("a frame goes to the peer in the destination slot with byte 1 set to the sender's slot", () => {
    const { h, a, b, c } = room();
    h.raw(b.conn, frame(0, [0xff, 0xff, 0xff, 0xff, 1, 2, 3]));
    const got = h.frames(a.conn);
    expect(got).toHaveLength(1);
    expect([...got[0]!]).toEqual([RELAY_FRAME, 1, 0xff, 0xff, 0xff, 0xff, 1, 2, 3]);
    h.raw(a.conn, frame(2, [9]));
    expect([...h.frames(c.conn)[0]!]).toEqual([RELAY_FRAME, 0, 9]);
    expect(h.frames(b.conn)).toHaveLength(0);
  });

  test("frames to empty slots, to yourself, oversized, malformed or from outside a room are dropped", () => {
    const { h, a, b, c } = room();
    h.raw(b.conn, frame(3, [1])); // empty slot
    h.raw(b.conn, frame(1, [1])); // itself
    h.raw(b.conn, frame(9, [1])); // no such slot
    h.raw(b.conn, frame(0, new Uint8Array(1401))); // payload too large
    h.raw(b.conn, frame(0, [])); // empty payload
    const wrongType = frame(0, [1]);
    wrongType[0] = 0x02;
    h.raw(b.conn, wrongType);
    const loner = h.player("Loner");
    h.raw(loner.conn, frame(0, [1]));
    for (const p of [a, b, c]) expect(h.frames(p.conn)).toHaveLength(0);
    h.raw(b.conn, frame(0, new Uint8Array(1400)));
    expect(h.frames(a.conn)).toHaveLength(1);
  });

  test("relay frames over the per-connection budget are dropped", () => {
    const { h, a, b } = room();
    for (let i = 0; i < 1000; i++) h.raw(b.conn, frame(0, [i & 0xff]));
    expect(h.frames(a.conn)).toHaveLength(DEFAULT_LIMITS.relayFramesBurst);
    h.clock.advance(1000);
    for (let i = 0; i < 1000; i++) h.raw(b.conn, frame(0, [1]));
    expect(h.frames(a.conn)).toHaveLength(DEFAULT_LIMITS.relayFramesBurst + DEFAULT_LIMITS.relayFramesPerSecond);
  });

  test("relay bytes over budget are dropped", () => {
    const { h, a, b } = room();
    const big = frame(0, new Uint8Array(1400)); // 1402 bytes
    for (let i = 0; i < 700; i++) h.raw(b.conn, big);
    expect(h.frames(a.conn)).toHaveLength(Math.floor(DEFAULT_LIMITS.relayBytesBurst / 1402));
  });
});

describe("rate limits", () => {
  test("JSON bursts above 60 get rate-limited, sustained abuse disconnects", () => {
    const h = new LobbyHarness();
    const a = h.player("Alice"); // hello used one token
    h.take(a.conn);
    for (let i = 0; i < 59; i++) h.send(a.conn, { t: "ping", time: i });
    expect(h.messages(a.conn).filter((m) => m.t === "pong")).toHaveLength(59);
    h.send(a.conn, { t: "ping", time: 99 });
    expect(h.lastError(a.conn)?.code).toBe("rate-limited");
    expect(h.disconnects).toHaveLength(0);
    // refills at 30/s
    h.clock.advance(1000);
    h.take(a.conn);
    for (let i = 0; i < 30; i++) h.send(a.conn, { t: "ping", time: i });
    expect(h.messages(a.conn).filter((m) => m.t === "pong")).toHaveLength(30);
    // a flood
    for (let i = 0; i < 200; i++) h.send(a.conn, { t: "ping", time: i });
    expect(h.disconnects).toEqual([{ conn: a.conn, code: CLOSE_RATE_LIMITED, reason: "rate-limited" }]);
    expect(h.messages(a.conn).filter((m) => m.t === "error" && m.code === "rate-limited")).toHaveLength(1);
  });
});

describe("validation", () => {
  const cases: [string, unknown][] = [
    ["not JSON", "{"],
    ["not an object", [1, 2]],
    ["unknown type", { t: "room.explode" }],
    ["name too long", { t: "rename", name: "x".repeat(25) }],
    ["empty name", { t: "rename", name: "   " }],
    ["control character in name", { t: "rename", name: "a\u0007b" }],
    ["bidi override in name", { t: "rename", name: "a‮b" }],
    ["maxPlayers 9", { t: "room.settings", settings: { maxPlayers: 9 } }],
    ["maxPlayers 0", { t: "room.settings", settings: { maxPlayers: 0 } }],
    ["maxPlayers 2.5", { t: "room.settings", settings: { maxPlayers: 2.5 } }],
    ["unknown map", { t: "room.settings", settings: { map: "mp_nuked" } }],
    ["unknown mod", { t: "room.settings", settings: { mods: ["coop"] } }],
    ["mod listed twice", { t: "room.settings", settings: { mods: ["zinfo", "zinfo"] } }],
    ["visibility", { t: "room.settings", settings: { visibility: "secret" } }],
    ["allowDuplicates string", { t: "room.settings", settings: { allowDuplicates: "yes" } }],
    ["character 4", { t: "slot.character", character: 4 }],
    ["character -1", { t: "slot.character", character: -1 }],
    ["ready not boolean", { t: "slot.ready", ready: 1 }],
    ["files missing", { t: "slot.files" }],
    ["signal type", { t: "rtc.signal", to: "x", signal: { type: "bye" } }],
    ["sdp too large", { t: "rtc.signal", to: "x", signal: { type: "offer", sdp: "a".repeat(16 * 1024 + 1) } }],
    ["candidate not an object", { t: "rtc.signal", to: "x", signal: { type: "candidate", candidate: "x" } }],
    ["sdpMLineIndex", { t: "rtc.signal", to: "x", signal: { type: "candidate", candidate: { sdpMLineIndex: -1 } } }],
    ["net mode", { t: "net.status", mode: "carrier-pigeon", rttMs: 1 }],
    ["negative rtt", { t: "net.status", mode: "p2p", rttMs: -5 }],
    ["ping without time", { t: "ping" }],
    ["kick without peer", { t: "room.kick" }],
    ["join with a number", { t: "room.join", code: 12345 }],
  ];
  for (const [label, msg] of cases) {
    test(`rejects ${label}`, () => {
      const h = new LobbyHarness();
      const a = h.player("Alice");
      h.create(a, { allowDuplicates: true });
      h.take(a.conn);
      h.raw(a.conn, typeof msg === "string" ? msg : JSON.stringify(msg));
      const out = h.messages(a.conn);
      expect(out).toHaveLength(1);
      expect(out[0]).toMatchObject({ t: "error", code: "bad-message" });
    });
  }

  test("messages over 32 KB are rejected", () => {
    const h = new LobbyHarness();
    const a = h.player("Alice");
    h.raw(a.conn, JSON.stringify({ t: "ping", time: 1, pad: "é".repeat(17 * 1024) }));
    expect(h.lastError(a.conn)?.message).toContain("32768 bytes");
  });

  test("names are trimmed and may use non-Latin letters", () => {
    const h = new LobbyHarness();
    const a = h.player("  Влад 🧟  ");
    h.create(a);
    expect(h.state(a.conn).slots[0]!.name).toBe("Влад 🧟");
  });
});

describe("slot.files with a map list", () => {
  test("the server derives hasGameFiles from the list and updates it when the host changes the map", () => {
    const h = new LobbyHarness();
    const a = h.player("Host");
    const b = h.player("Bob");
    const code = h.create(a, { map: "zombie_theater" });
    h.join(b, code);
    h.send(b.conn, { t: "slot.files", hasGameFiles: false, maps: ["zombie_theater", "zombie_moon"] });
    expect(h.state(a.conn).slots[1]).toMatchObject({ hasGameFiles: true, maps: ["zombie_theater", "zombie_moon"] });
    h.send(a.conn, { t: "room.settings", settings: { map: "zombie_pentagon" } });
    expect(h.state(a.conn).slots[1]!.hasGameFiles).toBe(false);
    h.send(a.conn, { t: "room.settings", settings: { map: "zombie_moon" } });
    expect(h.state(a.conn).slots[1]!.hasGameFiles).toBe(true);
  });

  test("unknown map ids are rejected", () => {
    const h = new LobbyHarness();
    const a = h.player("Host");
    h.create(a, {});
    h.send(a.conn, { t: "slot.files", hasGameFiles: true, maps: ["mp_nuketown"] });
    expect(h.lastError(a.conn)?.code).toBe("bad-message");
  });
});
