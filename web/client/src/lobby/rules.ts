// rules.ts - lobby rules shared by the lobby view and tests (pure).

import type { RoomSettings, RoomState, SlotState } from "../../../shared/protocol";
import { findMap, findMod, MAX_PLAYERS_ORIGINAL, type ModInfo } from "../../../shared/roster";

export function duplicatesAllowed(s: RoomSettings): boolean {
  return s.allowDuplicates || s.maxPlayers > MAX_PLAYERS_ORIGINAL;
}

export function seated(room: RoomState): SlotState[] {
  return room.slots.filter((s) => s.peerId !== null);
}

/** Why the host cannot start yet (empty: ready to start). */
export function startBlockers(room: RoomState): string[] {
  const out: string[] = [];
  if (room.phase !== "lobby") out.push("The game is already running.");
  const mapName = findMap(room.settings.map)?.name ?? room.settings.map;
  for (const s of seated(room)) {
    if (!s.connected) out.push(`${s.name} is reconnecting.`);
    else if (!s.hasGameFiles) out.push(`${s.name} is missing the game files for ${mapName}.`);
    else if (s.character === null) out.push(`${s.name} has not picked a character.`);
    else if (!s.ready) out.push(`${s.name} is not ready.`);
  }
  const mods = room.settings.mods.map((id) => findMod(id)).filter((m): m is ModInfo => !!m);
  if (mods.filter((m) => m.kind === "game").length > 1) out.push("Only one whole-game mod can be used.");
  for (const m of mods) {
    if (m.maps && !m.maps.includes(room.settings.map)) out.push(`${m.name} does not work on ${mapName}.`);
  }
  return out;
}

/** Mods usable on `map`; keeps load order and at most one "game" mod. */
export function modsForMap(mods: string[], map: string): string[] {
  let game = false;
  return mods.filter((id) => {
    const m = findMod(id);
    if (!m || (m.maps && !m.maps.includes(map))) return false;
    if (m.kind === "game") {
      if (game) return false;
      game = true;
    }
    return true;
  });
}
