// roster.ts - the Zombies maps the lobby offers, their four playable characters, and the mods a host can pick.
//
// A character is identified by its index 0..3. The retail map scripts give each player a character by entity number
// (giveCustomCharacters: `switch (self.entity_num)`, cases 0..3), so index N is the character a player in client
// slot N gets in the original game. Display names below follow that order as far as it is known; the order is not yet
// checked against the retail scripts for every map (`verified: false`). The index is what the engine receives, the
// names are labels only.

export const ORIGINAL_CHARACTER_COUNT = 4;
export const MAX_PLAYERS_ORIGINAL = 4;
export const MAX_PLAYERS_EXTENDED = 8;

export interface Crew {
  id: string;
  name: string;
  characters: [string, string, string, string];
  verified: boolean;
}

export const CREWS: Record<string, Crew> = {
  ultimis: {
    id: "ultimis",
    name: "Ultimis",
    characters: ["Tank Dempsey", "Nikolai Belinski", "Takeo Masaki", "Edward Richtofen"],
    verified: false,
  },
  pentagon: {
    id: "pentagon",
    name: "Pentagon",
    characters: ["John F. Kennedy", "Robert McNamara", "Fidel Castro", "Richard Nixon"],
    verified: false,
  },
  coast: {
    id: "coast",
    name: "Call of the Dead",
    characters: ["Sarah Michelle Gellar", "Robert Englund", "Danny Trejo", "Michael Rooker"],
    verified: false,
  },
};

export interface ZombiesMap {
  id: string; // the map / zone name the engine loads (zone/<id>.ff)
  name: string;
  crew: keyof typeof CREWS;
  // dlc dvar the front end gates the map on (src/qcommon/common.cpp Com_InitContentPacks_SP); null = base game
  pack: string | null;
}

export const MAPS: ZombiesMap[] = [
  { id: "zombie_theater", name: "Kino der Toten", crew: "ultimis", pack: null },
  { id: "zombie_pentagon", name: "\"Five\"", crew: "pentagon", pack: null },
  { id: "zombie_cosmodrome", name: "Ascension", crew: "ultimis", pack: "dlc2" },
  { id: "zombie_coast", name: "Call of the Dead", crew: "coast", pack: "dlc3" },
  { id: "zombie_temple", name: "Shangri-La", crew: "ultimis", pack: "dlc4" },
  { id: "zombie_moon", name: "Moon", crew: "ultimis", pack: "dlc5" },
  { id: "zombie_cod5_prototype", name: "Nacht der Untoten", crew: "ultimis", pack: "dlc1" },
  { id: "zombie_cod5_asylum", name: "Verrückt", crew: "ultimis", pack: "dlc1" },
  { id: "zombie_cod5_sumpf", name: "Shi No Numa", crew: "ultimis", pack: "dlc1" },
  { id: "zombie_cod5_factory", name: "Der Riese", crew: "ultimis", pack: "dlc1" },
];

export function findMap(id: string): ZombiesMap | undefined {
  return MAPS.find((m) => m.id === id);
}

export function characterName(mapId: string, character: number): string {
  const map = findMap(mapId);
  const crew = CREWS[map?.crew ?? "ultimis"];
  return crew?.characters[character] ?? `Character ${character + 1}`;
}

export interface ModInfo {
  id: string; // folder under mods/
  name: string;
  description: string;
  maps: string[] | null; // null = every map
  // "stack": goes into fs_mods with the other stackable mods (mods/_stack/maps/_modstack.gsc runs their hooks).
  // "game": needs fs_game (a whole-game mod); only one per game, and it is put first in fs_mods.
  kind: "stack" | "game";
  // extra +set dvars the mod needs on every machine (host and clients must match, see docs/multiplayer.md)
  dvars?: Record<string, string>;
}

// The lobby's optional mods. "coop" is not listed: the launcher adds it itself when a game needs it (shared/launch.ts).
// Descriptions follow each mod's README.
export const MODS: ModInfo[] = [
  {
    id: "zinfo",
    name: "Zombie counter",
    description: "Hold Tab (Back on a controller) to see how many zombies are alive and left this round.",
    maps: null,
    kind: "stack",
    dvars: { zinfo: "1" },
  },
  {
    id: "noperks",
    name: "No perks",
    description: "Removes the perk machines.",
    maps: null,
    kind: "stack",
    dvars: { noperks: "1" },
  },
  {
    id: "horde",
    name: "Horde",
    description: "The next round starts as soon as its last zombie has spawned; bigger crowds.",
    maps: null,
    kind: "stack",
    // mods/horde/README.md launch line; bo1_mod_maxactors must be on the command line (read at map load)
    dvars: { bo1_mod_maxactors: "64", horde_max_zombies: "48" },
  },
  {
    id: "sandbox",
    name: "Sandbox",
    description: "Five as a test area: pads that give every perk, weapon and power-up and control the rounds.",
    maps: ["zombie_pentagon"],
    kind: "game",
  },
];

export function findMod(id: string): ModInfo | undefined {
  return MODS.find((m) => m.id === id);
}
