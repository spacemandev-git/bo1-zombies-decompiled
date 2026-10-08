# Web client

The browser side of the BO1 Zombies web distribution: game-file importer, lobby, peer networking, controller
navigation and the bridge to the WebAssembly engine. Vanilla TypeScript and DOM, bundled by `web/scripts/build.ts`
into `web/dist/public`, which `web/server` serves under `BASE_PATH` (default `/bo1`).

The engine contract (OPFS layout, packet rings, gamepad struct, lifecycle) is `docs/web-engine-interface.md`. The
lobby protocol is `web/shared/protocol.ts`; maps, characters and mods are `web/shared/roster.ts`; client numbers and
command lines are `web/shared/launch.ts`.

## Structure

```
client/
  index.html              template: <!--BASE-->, %%CSS%%, %%JS%% are filled in by the build
  public/                 copied as is (favicon.svg)
  src/
    main.ts               shell (tabs, screen area, hint bar), routing, ?room=CODE auto-join
    app.ts                shared state: /api/config, capabilities, imported files, lobby connection
    base.ts               base URL from document.baseURI; site, WebSocket, data and invite URLs
    style.css
    assets/
      manifest.ts         which files Zombies needs; root detection; availability per map (pure)
      import.ts           folder picking, the import session, import.json
      copy.ts             streams Blobs into OPFS (worker with sync access handles, createWritable fallback)
      copyworker.ts       the OPFS writer worker
      datasync.ts         copies <base>/data/ (repo mods/ and data/main) into OPFS before a game
      opfs.ts             OPFS helpers, storage estimate / persist
      filesview.ts        "Game files" screen
    lobby/
      home.ts             name, create, join by code, public games
      lobby.ts            room view: settings, players, characters, ready, start
      rules.ts            start blockers, mod rules (pure)
      store.ts            room / launch / public rooms / peers' net status from the server
    net/
      signaling.ts        WebSocket client: hello + resume, reconnect, typed events, requests, ping, relay frames
      peer.ts             RTCPeerConnection per peer (star around the host)
      transport.ts        GameTransport: data channel or relay per slot, probes, test burst, net.status
      probe.ts            the page's own RTT / burst packets (pure)
    engine/
      bridge.ts           loads <base>/engine/bo1.js and runs createBo1()
      cmdline.ts          web prefix + engineCommands() (pure)
      ring.ts             bo1_net_ring / bo1_net_shared read and write (pure)
      padstruct.ts        bo1_gamepad encoding (pure)
      netbridge.ts        rings <-> GameTransport
      gamepadbridge.ts    navigator.getGamepads() -> bo1_gamepad, rumble
      gamescreen.ts       "Game" screen: data sync, engine or fallback with network test
    input/
      nav.ts              focus navigation, context actions, keyboard handling
      spatial.ts          direction -> next element (pure)
      uipad.ts            controllers for the UI (repeat 400 ms, then 120 ms)
      osk.ts              on-screen keyboard
      hints.ts            hint bar (controller glyphs or keys)
    ui/                   dom helpers, dialogs, toasts, icons, portraits, capability check, system view, net panel
```

## Screens

- **Play / home**: player name (stored in `localStorage`), Create game, Join with a code, public games (refreshed
  every 10 s). Opening `<base>?room=CODE` joins that room.
- **Play / lobby**: room code and invite link; map, mods, max players (1–8), duplicate characters and visibility
  (editable by the host); the map's four characters with usage; ready; players with host badge, character, ready,
  files and connection (P2P or relay, RTT); kick (host); Start (host, enabled when every player is connected,
  ready and has the map's files; otherwise the reasons are listed); a network test panel.
- **Game**: after the host starts. Copies mods and data into OPFS, then runs the engine, or, while
  `<base>/engine/bo1.js` does not exist, shows the exact engine command line and a live network panel.
- **Game files**: import, what is imported and which maps are playable, storage quota, delete all.
- **System check**: browser capabilities and what a missing one means, server and engine status, controllers.

## Base path

The build writes `<base href="BASE_PATH">` into `index.html` (`BASE_PATH` env at build time, default `/bo1/`). Every
URL is relative to `document.baseURI`: `api/config`, `ws` (ws: or wss: matching the page), `data/...`, `engine/...`.
The server must use the same base path.

## Importer

The player picks their Black Ops folder with `showDirectoryPicker()` (Chromium) or `<input type=file
webkitdirectory>` (Firefox, Safari). The install folder, its parent, or `zone/` and `main/` one after the other all
work: the root is the folder that holds `zone/` and `main/` (case-insensitive).

Imported (from `manifest.ts`):

| files | required |
| --- | --- |
| `localization.txt` | yes |
| `zone/Common/{code_pre_gfx_mp, code_post_gfx_mp, code_post_gfx, patch_mp, patch, ui_mp, frontend, common_zombie, common_zombie_patch}.ff` | yes |
| `zone/<Language>/<prefix>{code_pre_gfx_mp, code_post_gfx_mp, code_post_gfx, patch_mp, patch, common_zombie}.ff` | yes |
| other localized core zones, `patch_ui`, `patch_ui_mp`, `dev_mp` | when present |
| `main/iw_*.iwd`, `main/localized_<language>_iwd*.iwd` | at least one of each |
| per map: `zone/Common/<map>.ff`, `zone/<Language>/<prefix><map>.ff` | for that map |
| per map: `zone/Common/<map>_patch.ff` | when present |
| `main/video/*.bik` | opt-in, not used yet |

Everything else (multiplayer and campaign zones, executables, Redist) is ignored. The language is the first line of
`localization.txt`; prefixes follow the engine's table (`en_`, `fr_`, `ge_`, ...).

Files are streamed into OPFS `bo1/game/<path>` with the retail directory names and the file's own name. The worker
writes with synchronous access handles; browsers without them use `createWritable()`. Progress is shown per file and
overall; Cancel stops after the current chunk; a file already present with the same size is skipped, so picking the
same folder again resumes. The import asks for persistent storage and shows the quota. Afterwards `bo1/import.json`
(`{ files: { path: size }, maps: [...] }`) is rebuilt from what is actually in OPFS. Nothing is uploaded.

Before every game `datasync.ts` fetches `<base>/data/manifest.json` and downloads new or changed files (size and
sha256 against `bo1/data-index.json`) into `bo1/game/`; files that left the manifest are deleted. Paths that are player
game files are never written.

## Lobby and signaling

One WebSocket per tab. `hello` carries `PROTOCOL_VERSION` and the resume token from `sessionStorage`; after a drop the
client reconnects with exponential backoff (0.5 s to 10 s) and the server gives the player their slot back. A close
with code 4000 (another tab resumed the same session, e.g. a duplicated tab) starts a fresh session. Server errors are
shown as toasts. The player's `slot.files` flag is recomputed whenever the room's map or the imported files change.

## Peer networking

Star topology: every non-host player opens one `RTCPeerConnection` to the host as soon as both are in the room. The
non-host makes the offer; the host answers (and replaces its connection when an offer has a new DTLS fingerprint).
Data channels are pre-negotiated: `game` (id 0, unordered, no retransmits) and `ctl` (id 1, reliable).

- RTT: a ping on `ctl` every 2 s; while on the relay, probe packets through the relay instead.
- Fallback: while `game` is not open (8 s timeout, failure, disconnect), packets go through the server relay
  (`[RELAY_FRAME][slot][payload]`). The offerer keeps retrying in the background: ICE restarts, every third try a new
  connection, backing off to 30 s.
- Non-host players report `net.status` (mode, RTT) on change and every 5 s.
- Host migration and leaving players rebuild the links from the next `room.state`.
- Probe packets (`probe.ts`, header `FE FF FF FF "B1NT"`) are answered by the transport and never reach the engine.
  The test burst sends 100 of them to each reachable player and reports loss and RTT.

`GameTransport` (`transport.ts`): `send(toSlot, bytes)`, `onPacket((fromSlot, bytes) => ...)`, `status(slot)`,
`reachableSlots()`.

## Engine bridge

On `room.started` the command line is `+set fs_b /opfs/bo1/game +set fs_h /opfs/bo1/home +set r_fullscreen 0`
followed by `engineCommands(launch, slotAddress(hostSlot))`, one `+command` per array entry. When `/api/config`
reports `engine.available` (and the capability check passes), `bridge.ts` imports `<base>/engine/bo1.js` (as an ES
module, or as a classic script defining `createBo1`) and calls `createBo1({ canvas, arguments, locateFile, print,
printErr, mainScriptUrlOrBlob, onRuntimeInitialized, onEngineExit, onEngineError, onAbort })`.

- `onRuntimeInitialized` (before `main`) writes this player's packed address into `bo1_net_shared.local_ip`.
- `netbridge.ts` waits for `ready`, then drains `to_page` whenever `Atomics.waitAsync` on `write_index` wakes up (50 ms
  timeout; 4 ms polling without `waitAsync`) and routes by destination (`10.66.0.(s+1)` to slot `s`, `.255` to every
  reachable slot, its own address back into `from_page`). Incoming packets are written to `from_page` with the
  sender's address and port 28960; a full ring drops them.
- `gamepadbridge.ts` writes every standard-mapping pad into `bo1_gamepad` each animation frame (XInput bits, Y axes
  negated, `seq` incremented), keeps pads in stable slots, and plays rumble at most every 50 ms.
- While the engine runs, the UI navigation layer is suspended and the top bar is hidden. `onEngineExit` returns to
  the lobby (the host sends `room.ended`); `onEngineError` shows the message and the console. "End game" / "Leave
  game" stops the engine (`Module.abort()`).

The page needs from the engine build: `_bo1_net_get_shared` and `_bo1_input_gamepads` exported, and the memory
reachable as `HEAPU8` (or `HEAP32` / `wasmMemory`) on the module (`EXPORTED_RUNTIME_METHODS`).

## Controllers and keyboard

The whole UI works with a standard-mapping controller: D-pad or left stick moves the focus spatially (repeat after
400 ms, then every 120 ms), A activates, B goes back or closes a dialog. Context buttons per screen are shown in the
hint bar: on the lobby Y = ready, X = character picker, LB/RB = previous/next character, Start = start (host),
Back = copy invite; elsewhere LB/RB switch tabs. A on a text field opens the on-screen keyboard (X delete, Y space,
Start done). A on a select opens an option list. Keyboard: arrows, Enter, Escape, Tab, and the keys shown in the hint
bar. The hint bar switches between controller glyphs and keys with the last input used, and asks the player to press a
button when no controller has been seen (browsers expose pads only after a press).

## Build

```
cd web
bun run build                   # BASE_PATH=/bo1/ by default
BASE_PATH=/ bun run build
```

Output in `web/dist/public`: `index.html`, `assets/*-[hash].{js,css}` with linked source maps, `favicon.svg`, and `.gz` /
`.br` next to every compressible file over 1 KB.

## Tests

```
bun test web/test/client*.test.ts
```

Pure modules are covered: manifest matching, ring buffers (layout, wraparound, full ring), gamepad encoding, command
lines, URL helpers, probes, spatial navigation, lobby rules.

Multiplayer on one machine:

1. `cd web && bun run dev` (builds the client, starts the server on `PORT`, default 8080).
2. Open `http://localhost:8080/bo1/` in one browser window and create a game.
3. Open the invite link in a second window of another browser profile (or a private window, or another browser).
   Each tab is its own player; a second tab in the same profile works too, since the session lives in
   `sessionStorage`. Profiles share nothing; tabs of one profile share the imported files (OPFS).
4. Ready up in both windows and start. Without the engine build the Game screen shows the command line of each player
   and a network panel; "Send test burst" measures loss and RTT over the same path game packets take. On one machine
   WebRTC connects directly (P2P); blocking UDP or disabling WebRTC shows the relay fallback.

`window.bo1` in the console is the app state (lobby, transport, files).

## Testing without game files

The lobby only checks `bo1/import.json`, so a test browser can pretend to have a map: write an index into OPFS from the
page (devtools or a Playwright `page.evaluate`) and reload.

```js
const root = await navigator.storage.getDirectory();
const bo1 = await root.getDirectoryHandle("bo1", { create: true });
const game = await bo1.getDirectoryHandle("game", { create: true });
const loc = await (await game.getFileHandle("localization.txt", { create: true })).createWritable();
await loc.write("english\n"); await loc.close();
const files = {};   // the core zones, en_ copies, one iw_*.iwd, localized_english_iwd00.iwd, localization.txt, a map
for (const z of ["code_pre_gfx_mp", "code_post_gfx_mp", "code_post_gfx", "patch_mp", "patch", "ui_mp", "frontend",
  "common_zombie", "common_zombie_patch", "zombie_theater", "zombie_theater_patch"]) files[`zone/Common/${z}.ff`] = 1;
for (const z of ["code_pre_gfx_mp", "code_post_gfx_mp", "code_post_gfx", "patch_mp", "patch", "common_zombie",
  "zombie_theater"]) files[`zone/English/en_${z}.ff`] = 1;
Object.assign(files, { "main/iw_00.iwd": 1, "main/localized_english_iwd00.iwd": 1, "localization.txt": 1 });
const w = await (await bo1.getFileHandle("import.json", { create: true })).createWritable();
await w.write(JSON.stringify({ files, maps: ["zombie_theater"] })); await w.close();
```

With two such browser contexts (host and guest) the whole flow runs: lobby, ready, start, the engine command lines,
the WebRTC / relay network test, and, when `web/engine/` is built, the engine boot up to its first fastfile (which
fails, as the files are not real: `Could not find zone ...en_code_pre_gfx_mp.ff`).
