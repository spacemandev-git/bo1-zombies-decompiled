# CLAUDE.md - BO1 Zombies (decompiled engine)

Decompiled Call of Duty: Black Ops 1 engine (Treyarch IW-family, Quake 3 lineage) that runs Zombies, so mods can
change the engine itself. Windows x86, MSVC, Direct3D 9. About 1.38 M lines in `src/`, 1250 compiled translation
units. The exe is an **MP-engine decompile with the SP zombies game module ported in** (`BO1_MP` defined;
`+set bo1_zombies 1` switches it to zombies). The player supplies the original game's files (about 11 GB, copied
by `setup.ps1`); the repo holds no game assets and must never hold any.

Workstreams in flight. Each has its own doc; read it before working in that area:

| workstream | doc | code |
| --- | --- | --- |
| Web distribution at spacemandev.games/bo1 (Railway) | `web/README.md`, `web/client/README.md` | `web/`, `.railway/railway.ts` |
| Engine to WebAssembly (Emscripten, WebGL2, OPFS) | `docs/web-port.md`, `docs/web-engine-interface.md` | `CMakePresets.json`, `cmake/`, `src/web/` |
| D3D9 → WebGL2 shim (MojoShader) | `src/web/d3d9/README.md` | `src/web/d3d9/`, `third_party/mojoshader/` |
| Online co-op, 4 players (originals), up to 8 (duplicates), lobby character pick | `docs/multiplayer.md`, `mods/coop/README.md` | `src/client_mp`, `src/server_mp`, `mods/coop/` |
| Controller support (XInput now, Gamepad API on the web) | `docs/controllers.md` | `src/client/gpad_*`, `src/win32/win_gamepad*` |

## Rules that are not negotiable

- **No game assets in the repo or on the server.** No `.ff`, `.iwd`, `.bik`, dumped retail scripts or textures. The web build
  imports the player's own files into their browser (OPFS); the server only serves our code, `mods/` and `data/main/`.
- **Keep the MSVC build identical** when changing shared code for other platforms: guard with `#ifdef BO1_WEB` (web
  build) or use constructs MSVC also accepts. Never reformat decompiled code; edit the smallest region.
- **Tag non-original engine code**: `// mod: ...` or `// mod (<topic>): ...` (`// mod (coop):`, `// mod (gpad):`);
  web-port platform code `// web: ...`. Existing tags: `// zombies:` (1670, SP-port work), `SP 0x00XXXXXX` (2122, address
  of the original function in the retail SP exe), `retail` (the original game's behavior), `measured` (a number backed by
  a test run), `TEST SWITCH` (harness-only dvar), `NOT PORTED` (known gap), `BO1TODO`, `// XREF:` and `// [esp+..]`
  (raw IDA output). "KB" in comments = the MP decompile base this project started from.
- **Never `Read` a huge file whole.** `grep -n` first, then read a window. Largest: `game_mp/g_scr_main_mp.cpp` (18k
  lines, script builtins), `universal/com_expressions_eval.cpp` (14k), `gfx_d3d/r_image_wavelet.cpp` (12.5k),
  `ui/ui_shared.cpp` (11k), `game/g_scr_vehicle.cpp`, `cgame/cg_scr_main.cpp`, `database/db_load.cpp`.
- No commits/pushes unless the user asks. No Railway deploys (`railway up`, `railway config apply`) without the user's go.

## Repository map

| path | what |
| --- | --- |
| `src/win32/` | Windows platform layer: `WinMain` (`win_main.cpp`), window proc, mouse, XInput, Winsock UDP (`win_net.cpp`), stream thread, headless mode, minidumps, Steam glue. Replaced by `src/web/` in the web build |
| `src/qcommon/` | `Com_Init`/`Com_Frame` (`common.cpp`), threads + Win32 events (`threads.cpp`), netchan (`net_chan_mp.cpp`), msg/huffman, cmd/cbuf, collision (`cm_*`), mod engine code (`cm_mapkit`, `cm_noperks`) |
| `src/universal/` | dvars, filesystem + search paths (`com_files.cpp`), memory (`physicalmemory.cpp`, `com_memory.cpp`), `q_shared`, asserts (`assertive.h`), expressions |
| `src/database/` | fastfile (zone) loading: `db_registry.cpp` (zone lists, pools, `DB_Thread`), `db_file_load.cpp` (overlapped reads, inflate), `db_load.cpp` (asset deserializers) |
| `src/clientscript/` | GSC/CSC script compiler + VM (`cscr_*`); `Scr_ReadFile` (mods override fastfile scripts), generated `maps/_modstack_list.gsc` |
| `src/server_mp/`, `src/client_mp/` | MP netcode actually used by zombies: connect handshake, snapshots, usercmds. NB `sv_client_mp.cpp` lives in `client_mp/` |
| `src/server/`, `src/client/` | shared/SP-side pieces; `client/cl_keys.cpp` (binds, keycodes), `client/cl_gamepad.cpp` |
| `src/game/`, `src/game_mp/`, `src/game_sp/` | game module: actors/AI (`game/actor*`), MP game + script builtins (`game_mp/g_scr_main_mp.cpp`), SP zombies port (`game_sp/`, `G_SP_*`, `scr_sp_tables.h` layered builtin tables, headless test labs `g_sp_headless_*`, `bo1_testclient*`) |
| `src/cgame/`, `src/cgame_mp/` | client game: HUD (`cgame/cg_sp_hud.cpp`, zombies scorebars), view, prediction, entities |
| `src/bgame/` | shared game code: pmove, weapons defs, anim, misc tables (`bg_misc.cpp` reads `playlists_sp.info`) |
| `src/gfx_d3d/` | D3D9 renderer: front end `r_*`, back end `rb_*` (render thread), `r_state.cpp` (all state + the one draw call), `r_shade.cpp` (constants), `r_material*.cpp`, `r_init.cpp` (window/device) |
| `src/sound/` | sound engine (portable C) over a thin XAudio2 driver (`snd_driver_xaudio2*.cpp`, `SD_*` API); streams in `snd_stream.cpp` |
| `src/ui/`, `src/ui_mp/` | menu system (menus come from fastfiles; `ui_shared.cpp` key handling), playlists |
| `src/physics/`, `tl/` | rigid bodies (GJK), TL job queue (`tl/jobqueue`), TL physics defs |
| `src/xanim/`, `src/EffectsCore/`, `src/DynEntity/`, `src/glass/`, `src/flame/`, `src/ik/`, `src/ragdoll/`, `src/vehicle/`, `src/turret/`, `src/aim_assist/` | animation, FX, dynamic entities, and the rest of the gameplay systems |
| `src/live/`, `src/DW/`, `DemonWare/`, `src/steam/` | online services: mostly dead code (`BO1_LIVE`/`BO1_DW` never defined); Steam is optional at runtime |
| `src/groupvoice/`, `src/speex/` | voice chat (off: `sv_voice 0`) |
| `src/binklib/`, `src/mjpeg/`, `src/vpx/`, `src/libs/libvpx-1.5.0` | Bink cinematics (prebuilt DLL) and video *capture* (not playback) |
| `src/libs/`, `src/zlib/`, `src/jpeg/`, `src/minilzo/`, `src/tracy/`, `src/nvapi/`, `src/CubeMapGenLib/` | third-party |
| `src/demo/`, `src/devgui/`, `src/monkey/`, `src/ddl/`, `src/stringed/` | demos, dev GUI, test automation over Winsock, stats data definitions, localized strings |
| `mods/` | script mods (GSC/CSC), copied next to the exe by the build; see "Mods" |
| `data/main/` | base-game data the install lacks (`playlists_sp.info`), copied to `main/` by the build |
| `tools/` | `headless.ps1` (windowless runs + checks), `mod-launcher.hta`, `*_flags.js`, `mapkit/*.mjs` (layout doctor), `syntax-check.sh` |
| `web/` | Bun server (static, lobby, WebRTC signaling, relay) + TypeScript client; `web/shared/` = lobby protocol, map/character roster, launch command builder |
| `docs/` | design + status docs for the workstreams above |
| `CMakeLists.txt`, `cmake_files.cmake` | MSVC build; `cmake_files.cmake` is the single list of every source file |

## How the engine runs (anchors; line numbers drift, grep the function)

- **Boot**: `WinMain` (`win32/win_main.cpp`) → `Sys_InitializeCriticalSections`, `PMem_Init`, `Dvar_Init`, command line
  into `sys_cmdline` (1024 bytes) → `Com_Init` (`qcommon/common.cpp`, setjmp error trap) → `Com_ParseCommandLine` splits on
  `+`; `set` lines apply first (`Com_StartupVariable`), FS, configs, `NET_Init`, `SV_Init`, `CL_Init`, `R_InitThreads`,
  `SND_Init`; other `+commands` run after (`Com_AddStartupCommands`), then `Com_LoadFrontEnd` (the zombies main menu is a
  *level*, `map frontend`, with 3D TV menus).
- **Frame**: `Com_Frame` → `Cbuf_Execute`, `SV_Frame`, `CL_Frame`, `SCR_UpdateScreen`. Errors longjmp back (`Com_Error`).
- **Threads** (`qcommon/threads.h` contexts): main, render back end (`RB_RenderThread`), TL job workers, server
  (`SV_ServerThread`, reads the socket when a server runs), sound occlusion, database (`DB_Thread`), stream (texture +
  sound streaming). Sync via `Interlocked*`, critical sections, Win32 events. Thread-affine asserts in `com_files.cpp`.
- **Server/client**: zombies is a **listen server** in the host process (local client over loopback). Netchan packets
  ≤ 1264 bytes, huffman above netchan. Transport seam: `Sys_SendPacket` / `Sys_GetPacket` (`win32/win_net.cpp`).
  `NET_SendPacket` (`net_chan_mp.cpp`) routes loopback in memory. 32 clients supported engine-side.
- **Filesystem** (`universal/com_files.cpp` `FS_Startup`): later search paths win. Order: dev folders, `players`, `main`,
  `fs_game` (must be `mods/<x>`), then each `fs_mods` entry and `mods/_stack`. IWDs = zips (`main/iw_*.iwd`,
  `localized_*`). `fileSysCheck.cfg` missing is fatal. **Fastfiles** resolve from the *exe folder*
  (`zone/Common/<z>.ff`, `zone/<Language>/<z>.ff`), not `fs_b`; videos from the cwd. Three roots, backslash paths.
- **Zones for zombies**: boot `code_pre_gfx_mp`; then `code_post_gfx_mp`, `code_post_gfx`, `patch_mp`, `patch`, `ui_mp`,
  `frontend`; per map `common_zombie(_patch)`, `<map>(_patch)`, `en_*` localized copies. Plain zlib, no signing (`IWffu100`).
- **Scripts**: `Scr_ReadFile` (`clientscript/cscr_parser.cpp`) takes a loose file from the search path when a mod is active,
  else the fastfile copy. Builtins: MP tables in `game_mp/g_scr_main_mp.cpp`, SP layered tables (`game_sp/scr_sp_tables.h`);
  project-added builtins `bo1_mapkit_*`, `bo1_mod_*` (`game_sp/g_scr_sp_ai.cpp`).
- **Renderer**: front end builds sorted draw surfaces → command buffer → back end thread (`rb_backend.cpp`). Shaders are
  precompiled SM3 bytecode inside fastfiles. `r_smp_backend 0` runs the back end inline.
- **Sound**: no software mixer; XAudio2 mixes. Banks are fastfile assets; streamed audio through the FS layer.
- **Input**: `MainWndProc` → `Sys_QueEvent` → `CL_KeyEvent`; mouse via `IN_MouseMove` → `CL_MouseEvent`; gamepad →
  `CL_GamepadEvent` / `CL_GamepadButtonEventForPort`; usercmd in `client_mp/cl_input_mp.cpp` `CL_CreateCmd`.
- **Memory**: PMem 328 MiB committed at start (+256 MiB with `bo1_mod_zones`), hunk reserve/commit. ~400 MB total.

## Dvars and conventions you will meet

- `bo1_zombies 1` (command line only) selects zombies. ~172 `bo1_*` dvars are project additions: `bo1_mod_*` (mod features,
  several must be on the command line because they are read at map load: `bo1_mod_maxactors`, `bo1_mod_zones`),
  `bo1_headless*`, `bo1_testclient*`, `bo1_autoquit`, `bo1_shots`. Find any dvar with `grep -rn '"<name>"' src`.
- Engine prefixes: `Com_ Sys_ R_ RB_ CG_ G_ SV_ CL_ Scr_ DB_ BG_ CM_ FS_ SND_ SD_ UI_ GPad_`. Added: `G_Mapkit_*`,
  `CM_NoPerks_*`, `G_SP_Headless*`, `g_mod*`.
- C++: 4 spaces (`.editorconfig`), LF. GSC: tabs. Mod GSC files start `// <NAME> MOD - not part of the original game.`
- Doc style (READMEs, MAPKIT.md, docs/): plain declarative sentences, no promotion; every dvar with default and range;
  "retail" for the original; numbers with their conditions; exact log lines in backticks; sections like
  Start it / Leave it / How it works / Not done / Limits. Titles of mod docs end "(NOT part of the original game)".

## Build, run, test (Windows, the real target)

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32 -DDXSDK_DIR=C:/path/to/DXSDK_June2010
cmake --build build --config Release            # copies mods/ and data/main/ next to the exe
powershell -NoProfile -ExecutionPolicy Bypass -File setup.ps1 [-GameDir "D:\...\Call of Duty Black Ops"]   # once
Play.bat                                         # vanilla, main menu
build\Release\BO1Zombies.exe +set fs_b $R +set fs_h $R +set bo1_zombies 1 +set fs_mods zinfo +devmap zombie_pentagon
```

- Logs: `console_mp.log` (`logfile 2`) and `games_mp.log` (script `LogPrint`, appends) in `build\Release\main\`, or in
  `build\Release\mods\<x>\` with `fs_game`. Config: `players\config.cfg` (per-mod copy under `players\mods\<x>\` unless
  `bo1_mod_sharedconfig 1`).
- Headless checks: `tools\headless.ps1 -Zombies -Commands "+devmap zombie_pentagon" -AutoQuitMs 60000` (private
  desktop, crash/window watchdog, exit codes 0/10/11/12/20/21, up to 6 slots on `net_port` 29060+100·slot; `-Client` adds
  a local client; `-ShotsAtMs` screenshots). Self-tests: `sandbox_selftest`, `mapkit_selftest` (+ `tools/mapkit/doctor.mjs`),
  `noperks_selftest`. Test players: `bo1_testclient*` (headless only).

## Working from macOS/Linux (no MSVC)

- **Compile-check engine edits**: `tools/syntax-check.sh <files>` or `tools/syntax-check.sh --changed` (clang
  `-fsyntax-only`, Windows x86 target, MinGW-w64 headers; `brew install llvm mingw-w64`). 1115/1250 files pass on a clean
  tree; `tools/syntax-check/known-failures.txt` lists the rest (MinGW/MSVC header differences, mostly `gfx_d3d`). A file
  you changed must not gain errors. It is not the MSVC build: MSVC-only leniency (extra macro args, implicit includes,
  two-phase lookup) shows up here as errors; fix those portably.
- **Web engine build**: Emscripten in `~/emsdk`; activate with
  `export EMSDK_PYTHON=/opt/homebrew/bin/python3.12 EMSDK_OS=macos; source ~/emsdk/emsdk_env.sh` (`EMSDK_OS` is needed
  because `platform.mac_ver()` is empty in sandboxed shells). `cmake --preset web && cmake --build --preset web` →
  `web/engine/bo1.{js,wasm}` + `build.json` (presets `web-debug` with iassert, `web-node` = headless Node build with
  NODEFS: `src/web/tools/node-boot-test.sh`). Status (2026-10-07): every web-build TU compiles (1220/1220), links with
  no undefined symbols, boots in Node and Chrome to the first fastfile; the next step needs real game files.
  `docs/web-port.md` has the census, exclusions, risks and the parallel plan; `src/web/tools/` has `census.py`,
  `try_compile.py`, `undefined.py`, `fnptr_audit.py`.
- **Web port rules**: shared-code changes go behind `#ifdef BO1_WEB` (`BO1_WEB_NODE` for the Node build) or are no-ops
  for MSVC. Decompiled code calls functions through mismatched pointer types, which x86 tolerates and wasm traps on
  ("function signature mismatch"): wrap such calls in `BO1_FNCAST(T, fn)` (`src/web/web_fncast.h`; plain cast on
  Windows) and audit with `fnptr_audit.py`. `src/web/compat/` provides `windows.h` & co over pthreads (force-included
  header for MSVC names); the platform layer `src/web/web_*.cpp` replaces `src/win32/` files one by one. The back end
  runs on the main pthread (`+set r_smp_backend 0`, prepended by `web_main.cpp`), which owns the canvas and yields to
  the browser once per frame (`emscripten_set_main_loop`).
- **D3D9 shim**: `src/web/d3d9/` (README has the per-method coverage table), MojoShader `glsles3` profile vendored in
  `third_party/mojoshader/` (`VENDOR.md`: commit, `BO1-WEB` patches). Tests: `src/web/d3d9/test/run_unit_tests.sh`
  (Node), `build_smoke.sh` / `run_smoke_headless.sh` (browser). Debug env: `D3D9SHIM_GLCHECK=1`, `D3D9SHIM_TRACE=1`,
  `D3D9SHIM_SHADER_DUMP=<dir>` (feed real shaders to the corpus tools).
- **Web site**: `cd web && bun install && bun run build && bun run dev` → http://localhost:8080/bo1/ ; `bun test`;
  `bunx tsc --noEmit -p .`. With `web/engine/` built, starting a game in the lobby boots the wasm engine in the page.
  UI checks: drive two browser contexts (host + guest) with `playwright-core` and a local Chromium; the client has
  `data-testid`s on every key control (`create-game`, `room-code`, `ready-button`, `start-button`, `character-option-N`,
  `command-line`, `test-burst`, ...). Fake an import by writing `bo1/import.json` into OPFS (see `web/client/README.md`).
- You cannot run the game here. State exactly what was verified (syntax check, unit tests) and give the user a Windows
  test plan (headless.ps1 command lines and the log lines to look for) for everything else.

## Mods

Loaded only with `+set fs_game mods/<x>` (one mod; also moves logs/config) or `+set fs_mods "a b"` (stacked; `mods/_stack`
is appended automatically and must not be named). `_stack` ships the merged retail hook scripts
(`maps/_zombiemode_ffotd.gsc`, `maps/zombie_pentagon_ffotd.gsc`) that call `maps\_modstack::run(hook)`; each mod registers
hooks in `maps/<mod>/_hooks.gsc` `register()` with `maps\_modstack::add(hookName, func)`. Hook names:
`_zombiemode_ffotd::main_start|main_end`, `zombie_pentagon_ffotd::main_start|main_end`, `mod_zones/<zone>::main`.
Mods: `horde` (rounds chain, big crowds, `bo1_mod_maxactors`), `nightmare` (special enemies from other maps via
`bo1_mod_zones`), `mapkit` (JSON map layouts on Five's assets, `mods/mapkit/MAPKIT.md`), `noperks`, `sandbox` (Five test
pads), `zinfo` (Tab zombie counter), `coop` (lobby characters / 5-8 players), `_stack` (base). The retail map scripts are
not in the repo; `bo1_dumpscript <name>` writes one from the loaded fastfiles to `<fs_h>/dump/` for local reading (never commit dumps).

## Web distribution, multiplayer, controllers (summaries; details in the docs)

- **Hosting**: one Railway service (`.railway/railway.ts`, Dockerfile `web/Dockerfile`, build context = repo root) owns
  `spacemandev.games`, serves the site under `BASE_PATH=/bo1`, health check `/bo1/healthz`. Lobby state is in memory:
  keep **1 replica**. COOP/COEP headers are mandatory (SharedArrayBuffer for wasm threads).
- **Engine ↔ page contract**: `docs/web-engine-interface.md` (OPFS layout `/opfs/bo1/{game,home}`, network rings,
  gamepad struct, command line). Change it there first, then both sides.
- **Multiplayer model**: star topology; the host's tab/process is the listen server. Every player has a fake LAN
  address `10.66.0.<lobby slot+1>:28960`; the page maps it to a WebRTC data channel (unordered, no retransmits) or the
  WebSocket relay. Characters: retail scripts pick the character by entity number, so the first player to pick
  character C gets client number C (`bo1_slot`), duplicates get 4..7 and `mods/coop` maps them via `bo1_lobby_chars`.
  Host dvars `sv_maxclients`, `bo1_expected_players`, `systemlink 1`. Command lines are built in one place:
  `web/shared/launch.ts` (`engineCommands`), mirrored by `tools/coop-*` for native.
- **Controllers**: the console gamepad stack (XInput, aim assist, analog usercmds) is compiled in; the platform-neutral
  backend `src/client/gpad_backend.h` (`GPad_Backend_Poll`) is implemented by XInput on Windows and by the
  `bo1_gamepad` shared struct on the web.

## Finding things fast

- Where is X registered/used: `grep -rn '"x"' src` (dvars, commands, script builtin names are string literals).
- SP-port vs original: `grep -rn "// zombies:" <dir>`; project additions: `grep -rn "// mod" <dir>`.
- Retail address of a function: comments `SP 0x...` (SP exe) next to the definition.
- Which files a feature spans: `grep -rln <Symbol> src | xargs wc -l | sort -n`.
- Build membership: `cmake_files.cmake` (`grep -n "<file>" cmake_files.cmake`); exclusions for web in `cmake/`.
- Debug output on the engine side goes to `Com_Printf` → `console_mp.log`; script side `LogPrint` → `games_mp.log`.

## Using subagents (parallel work)

The tree is too large for one context. Fan out, keep conclusions, and give each agent exclusive files.

- **Exploration**: one `Explore` agent per subsystem, all in one message ("very thorough", ask for path:line facts, a
  word budget, and a recommendation). Good splits: platform/build/FS · renderer · netcode/players · input/UI ·
  sound/database/assets · scripts/mods/tools. Do not also grep the same area yourself while they run.
- **Implementation**: `general-purpose` agents with **disjoint file ownership** stated in the prompt (directories or
  files they may edit), plus: "other agents edit concurrently: re-read a file right before editing it, keep edits
  minimal, never revert or reformat code you did not write, no git state changes". Shared contracts (headers, protocol
  types, docs like `docs/web-engine-interface.md`) are written by the lead *before* launching and are read-only to agents.
- **Natural parallel units**: the web port's next phase as listed in `docs/web-port.md` "Parallel work" (runtime boot
  with real files, renderer bring-up on the shim, Web Audio driver behind `SD_*`, Bink replacement, page integration,
  two-browser co-op over the rings, function-pointer/x87 audits); D3D9 shim work items in `src/web/d3d9/README.md`;
  `web/server` vs `web/client`; each GSC mod; docs per workstream. Engine compile/portability fixes split by top-level
  `src/` directory.
- **Gates every agent must pass before reporting**: syntax check (no new errors vs `known-failures.txt`) for engine
  C/C++; `bun test` + `tsc` for `web/`; `cmake --build --preset web` error count for the web port. Ask for a short final
  report: files changed, verified vs not, contract deviations, open decisions.
- **Prompt preamble to reuse**: repo path, "decompiled IDA output; tag added code `// mod (<topic>):`; MSVC build must
  stay identical; cannot build/run the Windows game here; read <doc> first; own only <paths>".
- Use worktree isolation (`isolation: "worktree"`) only for experiments that may be thrown away; merging parallel
  worktrees that touch `common.cpp` or `cmake_files.cmake` costs more than file ownership in one tree.

## Known stale references

Mentioned in comments but not in the repo: `mods/_stack/README.md`, `notes/*.md` (run/lab IDs like `L60`,
`mapkit-fix-3c`), `tools/{mergecheck.sh, realtest.ps1, stress.ps1, unjam_table.mjs, gen_sp_builtins.py}`,
`mods/mapkit_horde`. `cm_noperks.cpp` points at `mods/zinfo` for the noperks script (it is `mods/noperks`).
