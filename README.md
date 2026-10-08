# BO1 Zombies

Decompiled engine code for Black Ops 1 Zombies, so mods can change the engine itself, not just scripts and maps.

![](images/1.webp)

![](images/2.webp)

## Documentation

- `CLAUDE.md` - how the engine and the repo are organized (also the index for AI-assisted work).
- Mods: `mods/<name>/README.md` (`sandbox`, `horde`, `nightmare`, `zinfo` + `noperks`, `coop`), `mods/mapkit/MAPKIT.md`;
  `tools\mod-launcher.hta` starts them.
- Online co-op (up to 4 players with the original characters, 5-8 with duplicates): `docs/multiplayer.md`,
  `tools\coop.ps1` (`coop-host.bat`, `coop-join.bat`).
- Controllers: `docs/controllers.md`.
- Browser version (spacemandev.games/bo1): `web/README.md`, `docs/web-port.md`, `docs/web-engine-interface.md`.

None of this is part of the original game. The game's own files are never in this repository: `setup.ps1` copies them
from your install, and the browser version reads them from your computer.
