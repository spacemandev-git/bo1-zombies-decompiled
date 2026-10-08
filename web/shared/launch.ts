// launch.ts - turns a finished lobby into engine client numbers and engine command lines.
//
// Character selection works through the client number, because the retail map scripts give a player the character
// of their entity number (cases 0..3):
//   - the first player (lowest lobby slot) to pick character C gets client number C, so with up to four players
//     and no duplicate picks the unmodified game shows every character correctly;
//   - every further player who picked an already-taken character gets the next client number from 4 up, and the
//     co-op mod (mods/coop) gives them their character from bo1_lobby_chars.
//
// Engine dvars used here (docs/multiplayer.md has the full contract):
//   bo1_slot              userinfo: the client number this player asks the host's server for (SV_DirectConnect)
//   bo1_lobby_chars       host: character index for client numbers 0.., "-" for an unused number
//   bo1_expected_players  host: how many players to wait for before round 1 (getnumexpectedplayers)
//   sv_maxclients         host: highest client number + 1
//   systemlink 1          host and clients: the co-op HUD / scoreboard path instead of solo

import { GAME_PORT, slotAddress, type LaunchConfig, type LaunchPlayer, type RoomState } from "./protocol";
import { findMod, MAX_PLAYERS_EXTENDED, ORIGINAL_CHARACTER_COUNT } from "./roster";

export const COOP_MOD = "coop";

/** Client numbers for the lobby's players: originals keep their character's number, duplicates get 4.. */
export function assignClientNums(players: { slot: number; character: number }[]): Map<number, number> {
  const bySlot = [...players].sort((a, b) => a.slot - b.slot);
  const result = new Map<number, number>();
  const taken = new Set<number>();
  for (const p of bySlot) {
    if (p.character >= 0 && p.character < ORIGINAL_CHARACTER_COUNT && !taken.has(p.character)) {
      taken.add(p.character);
      result.set(p.slot, p.character);
    }
  }
  let next = ORIGINAL_CHARACTER_COUNT;
  for (const p of bySlot) {
    if (!result.has(p.slot)) result.set(p.slot, next++);
  }
  return result;
}

/** Builds the LaunchConfig the server sends every player of `room` when the host starts. */
export function buildLaunch(room: RoomState, yourSlot: number): LaunchConfig {
  const seated = room.slots.filter((s) => s.peerId !== null);
  const host = seated.find((s) => s.isHost);
  const nums = assignClientNums(seated.map((s) => ({ slot: s.index, character: s.character ?? 0 })));
  const players: LaunchPlayer[] = seated.map((s) => ({
    slot: s.index,
    clientNum: nums.get(s.index)!,
    name: s.name,
    character: s.character ?? 0,
  }));
  return {
    roomCode: room.code,
    map: room.settings.map,
    mods: room.settings.mods,
    hostSlot: host ? host.index : 0,
    yourSlot,
    maxPlayers: room.settings.maxPlayers,
    players,
  };
}

/** True when some player's character differs from their client number (duplicates, or more than four players). */
export function needsCoopMod(launch: LaunchConfig): boolean {
  return launch.players.some((p) => p.clientNum !== p.character);
}

/** The +set lines every machine in the game must share (mods are loaded on the clients too: client scripts). */
function sharedCommands(launch: LaunchConfig): string[] {
  const cmds = ["+set bo1_zombies 1", "+set logfile 2", `+set net_port ${GAME_PORT}`, "+set systemlink 1"];
  const mods = launch.mods.map((id) => findMod(id)).filter((m) => m !== undefined);
  const game = mods.find((m) => m.kind === "game");
  const stack = mods.filter((m) => m.kind === "stack").map((m) => m.id);
  if (needsCoopMod(launch)) stack.unshift(COOP_MOD);
  if (game) cmds.push(`+set fs_game mods/${game.id}`);
  // fs_game's mod goes first in fs_mods (src/universal/com_files.cpp FS_Startup), so naming it again is harmless
  const fsMods = game ? [game.id, ...stack] : stack;
  if (fsMods.length) cmds.push(`+set fs_mods ${fsMods.join(" ")}`);
  for (const m of mods) for (const [k, v] of Object.entries(m.dvars ?? {})) cmds.push(`+set ${k} ${v}`);
  return cmds;
}

function playerName(name: string): string {
  // the command line is split on '+' and tokenized on spaces/quotes (Com_ParseCommandLine): keep names to one token
  return name.replace(/[^A-Za-z0-9_\-.]/g, "_").slice(0, 15) || "player";
}

/**
 * The engine command line for one player, as a list of +commands.
 * hostAddress: where clients connect (web: the host's fake slot address; native: the host's IP). Ignored for the host.
 */
export function engineCommands(launch: LaunchConfig, hostAddress?: string): string[] {
  const me = launch.players.find((p) => p.slot === launch.yourSlot);
  if (!me) throw new Error(`slot ${launch.yourSlot} is not in the game`);
  const cmds = sharedCommands(launch);
  cmds.push(`+set name ${playerName(me.name)}`, `+set bo1_slot ${me.clientNum}`);
  if (launch.yourSlot === launch.hostSlot) {
    const highest = Math.max(...launch.players.map((p) => p.clientNum));
    const chars: string[] = Array.from({ length: highest + 1 }, () => "-");
    for (const p of launch.players) chars[p.clientNum] = String(p.character);
    cmds.push(
      `+set sv_maxclients ${Math.min(Math.max(highest + 1, launch.players.length), MAX_PLAYERS_EXTENDED)}`,
      `+set bo1_expected_players ${launch.players.length}`,
      `+set bo1_lobby_chars ${chars.join(" ")}`,
      `+map ${launch.map}`,
    );
  } else {
    const addr = hostAddress ?? slotAddress(launch.hostSlot);
    cmds.push(`+connect ${addr}:${GAME_PORT}`);
  }
  return cmds;
}
