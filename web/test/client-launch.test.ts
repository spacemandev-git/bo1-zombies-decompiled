import { describe, expect, test } from "bun:test";
import { buildLaunch } from "../shared/launch";
import type { LaunchConfig, RoomState, SlotState } from "../shared/protocol";
import { baseUrl, dataUrl, inviteUrl, siteUrl, wsUrl } from "../client/src/base";
import { buildCommandLine, commandLineText, WEB_PREFIX } from "../client/src/engine/cmdline";

function slot(index: number, name: string, character: number | null, isHost = false): SlotState {
  return { index, peerId: `p${index}`, name, character, ready: true, isHost, connected: true, hasGameFiles: true, maps: [] };
}

function room(slots: SlotState[], mods: string[] = [], map = "zombie_theater"): RoomState {
  return {
    code: "ABCD",
    hostPeerId: slots.find((s) => s.isHost)!.peerId!,
    phase: "in-game",
    settings: { map, mods, maxPlayers: Math.max(4, slots.length), allowDuplicates: false, visibility: "private" },
    slots,
  };
}

describe("engine command line", () => {
  const r = room([slot(0, "Host Guy", 2, true), slot(1, "Bob", 0), slot(2, "Eve", 1)]);

  test("web prefix first, one +command per entry", () => {
    expect(WEB_PREFIX).toEqual(["+set fs_b /opfs/bo1/game", "+set fs_h /opfs/bo1/home", "+set r_fullscreen 0"]);
    const args = buildCommandLine(buildLaunch(r, 0));
    expect(args.slice(0, 3)).toEqual([...WEB_PREFIX]);
    for (const a of args) expect(a.startsWith("+")).toBe(true);
  });

  test("host runs the map", () => {
    const args = buildCommandLine(buildLaunch(r, 0));
    expect(args).toContain("+map zombie_theater");
    expect(args).toContain("+set systemlink 1");
    expect(args).toContain("+set net_port 28960");
    expect(args).toContain("+set name Host_Guy");
    expect(args).toContain("+set bo1_slot 2");
    expect(args.some((a) => a.startsWith("+connect"))).toBe(false);
    expect(args).toContain("+set bo1_expected_players 3");
  });

  test("clients connect to the host's fake slot address", () => {
    const args = buildCommandLine(buildLaunch(r, 1));
    expect(args).toContain("+connect 10.66.0.1:28960");
    expect(args).toContain("+set bo1_slot 0");
    expect(args.some((a) => a.startsWith("+map "))).toBe(false);
    // host in another slot
    const r2 = room([slot(0, "A", 0), slot(1, "B", 1), slot(2, "H", 2, true)]);
    expect(buildCommandLine(buildLaunch(r2, 0))).toContain("+connect 10.66.0.3:28960");
  });

  test("duplicates load the co-op mod, game mods set fs_game", () => {
    const dup = room([slot(0, "A", 0, true), slot(1, "B", 0)], ["zinfo"]);
    const args = buildCommandLine(buildLaunch(dup, 0));
    expect(args).toContain("+set fs_mods coop zinfo");
    expect(args).toContain("+set zinfo 1");
    const sandbox = room([slot(0, "A", 0, true)], ["sandbox", "zinfo"], "zombie_pentagon");
    const a2 = buildCommandLine(buildLaunch(sandbox, 0));
    expect(a2).toContain("+set fs_game mods/sandbox");
    expect(a2).toContain("+set fs_mods sandbox zinfo");
  });

  test("text form", () => {
    const launch: LaunchConfig = buildLaunch(r, 0);
    expect(commandLineText(buildCommandLine(launch))).toMatch(/^\+set fs_b \/opfs\/bo1\/game \+set fs_h/);
  });
});

describe("base URL helpers", () => {
  test("derive everything from the base", () => {
    const page = "https://spacemandev.games/bo1/";
    expect(baseUrl(page).toString()).toBe("https://spacemandev.games/bo1/");
    expect(baseUrl("https://spacemandev.games/bo1/index.html?room=X#y").toString()).toBe("https://spacemandev.games/bo1/");
    expect(siteUrl("api/config", page)).toBe("https://spacemandev.games/bo1/api/config");
    expect(siteUrl("/engine/bo1.js", page)).toBe("https://spacemandev.games/bo1/engine/bo1.js");
    expect(wsUrl("ws", page)).toBe("wss://spacemandev.games/bo1/ws");
    expect(wsUrl("ws", "http://localhost:8098/bo1/")).toBe("ws://localhost:8098/bo1/ws");
    expect(inviteUrl("ABCD", page)).toBe("https://spacemandev.games/bo1/?room=ABCD");
    expect(dataUrl("mods/zinfo/maps/a b.gsc", page)).toBe("https://spacemandev.games/bo1/data/mods/zinfo/maps/a%20b.gsc");
    expect(siteUrl("api/config", "http://localhost:3000/")).toBe("http://localhost:3000/api/config");
  });
});
