import { describe, expect, test } from "bun:test";
import { decodeProbe, encodeProbe, isProbe, probeReply, ProbeType, PROBE_SIZE, summarizeBurst } from "../client/src/net/probe";
import { pickNext, type Rect } from "../client/src/input/spatial";
import { modsForMap, startBlockers } from "../client/src/lobby/rules";
import { safeDataPath } from "../client/src/assets/datasync";
import type { RoomState } from "../shared/protocol";

describe("probe packets", () => {
  test("round trip and replies", () => {
    const p = { type: ProbeType.Burst, id: 0xdeadbeef, time: 1234.5 };
    const b = encodeProbe(p);
    expect(b.length).toBe(PROBE_SIZE);
    expect(isProbe(b)).toBe(true);
    expect(decodeProbe(b)).toEqual(p);
    expect(probeReply(p)).toEqual({ type: ProbeType.BurstEcho, id: p.id, time: p.time });
    expect(probeReply({ type: ProbeType.Ping, id: 1, time: 2 })?.type).toBe(ProbeType.Pong);
    expect(probeReply({ type: ProbeType.Pong, id: 1, time: 2 })).toBeNull();
  });

  test("engine packets are never mistaken for probes", () => {
    const connectionless = new Uint8Array(PROBE_SIZE).fill(0xff); // -1 header
    expect(isProbe(connectionless)).toBe(false);
    const sequenced = new Uint8Array(PROBE_SIZE);
    new DataView(sequenced.buffer).setInt32(0, 12345, true);
    expect(isProbe(sequenced)).toBe(false);
    expect(isProbe(new Uint8Array(PROBE_SIZE + 1))).toBe(false);
  });

  test("burst summary", () => {
    expect(summarizeBurst(1, 100, [10, 20, 30])).toEqual({ slot: 1, sent: 100, received: 3, lossPct: 97, rttAvg: 20, rttMin: 10, rttMax: 30 });
    expect(summarizeBurst(2, 100, []).rttAvg).toBeNull();
  });
});

describe("spatial navigation", () => {
  const r = (left: number, top: number, w = 100, h = 40): Rect => ({ left, top, right: left + w, bottom: top + h });
  const grid = [
    { rect: r(0, 0), item: "a" },
    { rect: r(120, 0), item: "b" },
    { rect: r(240, 0), item: "c" },
    { rect: r(0, 60), item: "d" },
    { rect: r(120, 60), item: "e" },
    { rect: r(0, 200, 400), item: "wide" },
  ];
  test("moves within rows and columns", () => {
    expect(pickNext(r(0, 0), grid, "right")).toBe("b");
    expect(pickNext(r(120, 0), grid, "left")).toBe("a");
    expect(pickNext(r(120, 0), grid, "down")).toBe("e");
    expect(pickNext(r(120, 60), grid, "up")).toBe("b");
    expect(pickNext(r(240, 0), grid, "right")).toBeNull();
    expect(pickNext(r(240, 0), grid, "down")).toBe("e");
    expect(pickNext(r(120, 60), grid, "down")).toBe("wide");
  });
});

describe("lobby rules", () => {
  const base: RoomState = {
    code: "X",
    hostPeerId: "h",
    phase: "lobby",
    settings: { map: "zombie_theater", mods: [], maxPlayers: 4, allowDuplicates: false, visibility: "private" },
    slots: [
      { index: 0, peerId: "h", name: "Host", character: 0, ready: true, isHost: true, connected: true, hasGameFiles: true, maps: [] },
      { index: 1, peerId: "b", name: "Bob", character: 1, ready: false, isHost: false, connected: true, hasGameFiles: false, maps: [] },
      { index: 2, peerId: null, name: "", character: null, ready: false, isHost: false, connected: false, hasGameFiles: false, maps: [] },
    ],
  };
  test("start blockers", () => {
    expect(startBlockers(base)).toEqual(["Bob is missing the game files for Kino der Toten."]);
    const ok = { ...base, slots: base.slots.map((s) => ({ ...s, ready: true, hasGameFiles: true })) };
    expect(startBlockers(ok)).toEqual([]);
  });
  test("mods follow the map and allow one whole-game mod", () => {
    expect(modsForMap(["zinfo", "sandbox"], "zombie_theater")).toEqual(["zinfo"]);
    expect(modsForMap(["zinfo", "sandbox"], "zombie_pentagon")).toEqual(["zinfo", "sandbox"]);
    expect(modsForMap(["nope", "horde"], "zombie_moon")).toEqual(["horde"]);
  });
  test("data sync never touches player files", () => {
    expect(safeDataPath("mods/zinfo/maps/_zombiemode_ffotd.gsc")).toBe(true);
    expect(safeDataPath("main/playlists_sp.info")).toBe(true);
    expect(safeDataPath("main/iw_00.iwd")).toBe(false);
    expect(safeDataPath("zone/Common/patch.ff")).toBe(false);
    expect(safeDataPath("mods/../../x")).toBe(false);
    expect(safeDataPath("localization.txt")).toBe(false);
  });
});
