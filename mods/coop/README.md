# coop - every player gets the lobby's character (NOT part of the original game)

A mod folder, kept apart from the base game. In an online co-op game (docs/multiplayer.md) the lobby picks a
character for every player. The retail map scripts give a player the character of their entity number (client
numbers 0-3). The lobby gives the first player of each character that character's client number, so with up to four
players and four different characters the retail game is already right and this mod is not loaded. A player who picked
a character someone else already has gets client number 4, 5, 6 or 7; this mod gives them their character. That is
how a game gets duplicate characters and five to eight players.

## Start it

The co-op launch adds it by itself when it is needed (web lobby: web/shared/launch.ts; Windows: tools\coop.ps1). By
hand, on the host: `+set fs_mods coop` (stacks with other mods, e.g. `fs_mods coop zinfo`) together with the host's
`+set bo1_lobby_chars "<character of client 0> <of client 1> ..."` (`-` for an unused number), e.g.
`+set bo1_lobby_chars "0 1 2 3 2"`: client 4 plays character 2. Every machine uses the same `fs_mods`.

Host dvars: `coop_mod 0` turns it off (retail characters), `coop_entity_num 0` keeps `self.entity_num` at the entity
number (see below).

## Leave it

Start without `coop` in `fs_mods`. Nothing is saved: `fs_mods` and `bo1_lobby_chars` are not archived dvars.

## How it works

- `maps/coop/_hooks.gsc` registers `maps\coop\_coop::main_end` on the shared hook at the end of
  `maps\_zombiemode::main()` (`_zombiemode_ffotd::main_end`, mods/_stack).
- `maps/coop/_coop.gsc` reads `bo1_lobby_chars` and replaces two level function pointers the map set before it
  called `maps\_zombiemode::main()` with wrappers that call the map's own function with the lobby's character:
  - `level.zombiemode_give_player_model_override( entity_num )` - the body;
  - `level.zombiemode_player_set_viewmodel_override( entity_num )` - the arms.
  During the call the player's `self.entity_num` is the character too (in case the map's function reads it instead
  of its parameter). Afterwards `self.entity_num` stays the character (`coop_entity_num 1`, default): the retail
  scripts use it as the player index for the character's voice lines and per-character tables. After each spawn the
  mod sets it again if the map set it back.
- A client number the list does not give a character keeps its own (0-3) or its number modulo 4 (4-7), so the
  retail switch (cases 0-3) always finds a model.
- Every decision is a `coop: ...` line in games_mp.log (the map, the list, which pointers were wrapped, each body /
  arms call, each entity_num change).
- The score bars and colours on the HUD follow the character too; that part is engine code (src/cgame/cg_sp_hud.cpp,
  `bo1_lobby_chars` reaches every client with the gamestate) and works without this mod.

## Checked and not checked

Checked: the script syntax by reading (no GSC compiler here); it references only level function pointers, engine
builtins and its own functions, so it loads on every map (a call into another file that does not exist would stop
the script from loading).

NOT checked - based on the expected retail structure of the BO1 zombie map scripts, which are in the fastfiles and
not in this repo:
- that the maps set `level.zombiemode_give_player_model_override` and `level.zombiemode_player_set_viewmodel_override`
  before `maps\_zombiemode::main()`, and that `maps\_zombiemode` calls them on every spawn with the entity number;
- that `self.entity_num` is what the voice lines and per-character tables use, and nothing uses it as a unique
  player id.

## Check it (with the script dump)

1. Start a solo game of the map, open the console and run `bo1_dumpscript maps/zombie_theater.gsc` (the map's own
   script), `bo1_dumpscript maps/_zombiemode.gsc`, `bo1_dumpscript maps/_zombiemode_audio.gsc`, or
   `bo1_dumpscripts` for every script loaded. The files land in `<fs_homepath>/dump/` (build\Release\dump\ with the
   usual start).
2. In the map's script look for `zombiemode_give_player_model_override` and `zombiemode_player_set_viewmodel_override`
   (and the `switch( ... entity_num )` with cases 0-3). In `_zombiemode.gsc` look for where they are called and where
   `self.entity_num` is set. In `_zombiemode_audio.gsc` (or similar) look for `entity_num` in the voice code.
3. If a map uses other names (or calls a `giveCustomCharacters()`-style function directly), the log says
   `body: ... is not set on this map`: that map needs its own wrapper in `_coop.gsc`.

## Not done

- Maps that do not use the two override pointers (see the log line above).
- A late joiner's character (the host's list is fixed at map start).

## Limits

What retail scripts sized for four players may do with client numbers 4-7 (to check with the dump):
- voice lines and character quotes keyed on `entity_num`: covered by `coop_entity_num 1`; two players of the same
  character then share the character's voice (and may talk over each other);
- per-player arrays indexed 0..3 by entity number, `"player_" + n` flags or HUD elements per player number: may stay
  undefined for 4-7 (script errors in the log, a missing hint or icon);
- the zombie count per round grows with `get_players().size` (more zombies with eight players), solo rules (solo Quick
  Revive, the solo pistol) only apply with one player;
- the mini scoreboard stacks eight rows upward (engine), each row clear of the next.
