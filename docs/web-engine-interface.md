# Web engine interface (the contract between the wasm engine and the page)

The browser build of the engine (`src/web/`, built by `cmake --preset web`) runs inside the page served by
`web/` (Bun server + TypeScript client). This file is the contract between the two halves. Both sides
implement exactly this; change it here first, then on both sides.

Engine side: `src/web/web_bridge.h` (the C structs below), `src/web/web_net.cpp`, `src/web/web_gamepad.cpp`.
Page side: `web/client/src/engine/bridge.ts` (module loading, command line), `netbridge.ts` and `ring.ts` (section 3),
`gamepadbridge.ts` and `padstruct.ts` (section 4), `web/client/src/net/` (WebRTC / relay transport).
The page reads and writes wasm memory through `HEAPU8` (or `wasmMemory.buffer`), so the module must export it
(`-sEXPORTED_RUNTIME_METHODS=HEAPU8,...`), together with `_bo1_net_get_shared` and `_bo1_input_gamepads`
(`-sEXPORTED_FUNCTIONS`).

## 1. Loading

The engine is an Emscripten module built with `-sMODULARIZE -sEXPORT_NAME=createBo1`, pthreads and
`-sPROXY_TO_PTHREAD` (the engine's `main` runs in a worker; the page's main thread only runs JS glue, input
callbacks and this bridge). Files, served by `web/server` under `<BASE_PATH>/engine/`:

| file | what |
| --- | --- |
| `bo1.js` | module factory (also the pthread worker script) |
| `bo1.wasm` | the engine |
| `build.json` | `{ "version": "<git describe>", "built": "<ISO time>" }` - the page shows it and uses it to bust caches |

The page needs `crossOriginIsolated === true` (the server sends `Cross-Origin-Opener-Policy: same-origin` and
`Cross-Origin-Embedder-Policy: require-corp`), WebGL2, OPFS and `SharedArrayBuffer`.

```ts
const module = await createBo1({
  canvas,                       // the <canvas>; the engine transfers it to its render thread (OffscreenCanvas)
  arguments: commandLine,       // string[]: the +commands from web/shared/launch.ts engineCommands(), plus the
                                // web-only ones below, one token per +command (the engine joins them with spaces)
  locateFile: (f) => `${base}engine/${f}`,
  print, printErr,              // console lines (console_mp.log is also written to /opfs/bo1/home)
});
```

Web-only commands the page prepends: `+set fs_b /opfs/bo1/game +set fs_h /opfs/bo1/home +set r_fullscreen 0`.

## 2. Files

The engine mounts the Origin Private File System at `/opfs` (WasmFS OPFS backend, synchronous access from the
engine's threads). Layout, all under the OPFS directory `bo1/`:

| OPFS path | engine path | written by | content |
| --- | --- | --- | --- |
| `bo1/game/zone/Common/*.ff`, `bo1/game/zone/<Language>/*.ff` | `/opfs/bo1/game/zone/...` | page importer | the player's own Black Ops files, names exactly as in their install |
| `bo1/game/main/*.iwd`, `bo1/game/localization.txt`, `bo1/game/main/video/*.bik` | same | page importer | same |
| `bo1/game/main/playlists_sp.info` | same | page, from `<BASE_PATH>/data/` | the repo's `data/main/` |
| `bo1/game/mods/<mod>/...` | same | page, from `<BASE_PATH>/data/` | the repo's `mods/` |
| `bo1/home/` | `/opfs/bo1/home` (`fs_h`) | engine | `players/config.cfg`, logs |
| `bo1/import.json` | - | page importer | what was imported: `{ "files": { "<path>": <size> }, "maps": ["zombie_theater", ...] }` |

The engine resolves paths case-insensitively and with `\` or `/` (Windows installs and the engine's own code
mix `English` / `english`, `zone\Common`). `<exeDir>` (`Sys_DefaultInstallPath`) is `/opfs/bo1/game`.

The server publishes `<BASE_PATH>/data/manifest.json`: `{ "files": [{ "path": "mods/zinfo/maps/_zombiemode_ffotd.gsc",
"size": 1234, "sha256": "..." }, ...] }` for everything under `mods/` and `data/main/` (paths relative to the
game root, `data/main/x` published as `main/x`), and serves each file at `<BASE_PATH>/data/<path>`. The page copies
new or changed files into `bo1/game/` before every start.

Nothing the player imports ever leaves the browser.

## 3. Network

Game traffic is plain engine UDP packets (at most 1264 bytes, `src/qcommon/net_chan_mp.cpp`). The engine's
`Sys_SendPacket` / `Sys_GetPacket` (`src/web/web_net.cpp`, replacing `src/win32/win_net.cpp`) exchange them
with the page through two single-producer single-consumer rings in wasm memory. Every player has a fake IPv4
address `10.66.0.<lobby slot + 1>`, port 28960 (`web/shared/protocol.ts` `slotAddress`); the engine sees these as
LAN addresses (no rate cap, passes the LAN connect check). The page maps addresses to WebRTC data channels or
the server relay.

```c
// src/web/web_bridge.h
#define BO1_NET_MAX_PACKET 1400
#define BO1_NET_SLOT_SIZE  1408            // 8-byte header + BO1_NET_MAX_PACKET
typedef struct bo1_net_ring {
    uint32_t capacity;                     // number of slots, a power of two (engine sets it: 256)
    uint32_t slot_size;                    // BO1_NET_SLOT_SIZE
    _Atomic uint32_t write_index;          // producer: write slot (write_index % capacity), then increment
    _Atomic uint32_t read_index;           // consumer: read slot (read_index % capacity), then increment
    // then capacity slots, each: uint32_t ip; uint16_t port; uint16_t length; uint8_t data[BO1_NET_MAX_PACKET];
} bo1_net_ring;                            // header is 16 bytes, slots start at offset 16

typedef struct bo1_net_shared {
    bo1_net_ring *to_page;                 // engine produces (Sys_SendPacket), page consumes
    bo1_net_ring *from_page;               // page produces, engine consumes (Sys_GetPacket)
    uint32_t local_ip;                     // page sets before main(): this player's fake address
    uint32_t ready;                        // engine sets to 1 once the rings exist
} bo1_net_shared;

EMSCRIPTEN_KEEPALIVE bo1_net_shared *bo1_net_get_shared(void);
```

- `ip` is the four address bytes `a.b.c.d` packed as `a | b<<8 | c<<16 | d<<24` (so `10.66.0.1` is
  `0x0100420A`); `port` is in host byte order.
- Full ring: the producer drops the packet (UDP semantics; the netchan retransmits what matters).
- The engine calls `Atomics.notify` on `to_page->write_index` after each write
  (`emscripten_atomic_notify` / `__builtin_wasm_memory_atomic_notify`). The page waits with
  `Atomics.waitAsync(HEAP32, addrOf(write_index) >> 2, lastSeen)` on its main thread and drains the ring each
  time it wakes, so outgoing packets leave without polling delay.
- The engine polls `from_page` in `Sys_GetPacket` (non-blocking). Which engine thread reads is the engine's
  business (the server thread on a host, the main thread otherwise).
- The page sets `local_ip` from `bo1_net_get_shared()` as soon as the module's runtime is initialized, before
  the engine opens its socket (`NET_OpenIP` on the web returns this address for `net_ip`).

Page routing: a packet to `10.66.0.(s+1)` goes to the data channel of the peer in lobby slot `s` (WebRTC,
`ordered: false, maxRetransmits: 0`, label `game`), or, while no data channel is open, as a relay frame through
the lobby WebSocket (`web/shared/protocol.ts` `RELAY_FRAME`). Incoming packets are written to `from_page` with the
sender's slot address as `ip` and 28960 as `port`.

## 4. Gamepads

The Gamepad API exists only on the page's main thread, so the page polls `navigator.getGamepads()` every
animation frame and writes the state into wasm memory; the engine's web gamepad backend
(`src/web/web_gamepad.cpp`, behind the platform-neutral backend of `docs/controllers.md`) reads it.

```c
#define BO1_MAX_GAMEPADS 4
typedef struct bo1_gamepad {
    uint32_t connected;                    // page: 1 if a pad with mapping "standard" is in this slot
    uint32_t buttons;                      // page: XInput button bits (below)
    float lt, rt;                          // page: triggers 0..1
    float lx, ly, rx, ry;                  // page: sticks -1..1, Y positive = up (XInput convention; the page
                                           //       negates the Gamepad API's Y axes)
    float rumble_low, rumble_high;         // engine: 0..1 motor strengths; the page plays them
    uint32_t seq;                          // page: incremented after every write of this entry
    uint32_t reserved;                     // 0; pads the entry to 48 bytes (the page uses a 48-byte stride)
} bo1_gamepad;                             // 48 bytes: static_assert(sizeof(bo1_gamepad) == 48)

EMSCRIPTEN_KEEPALIVE bo1_gamepad *bo1_input_gamepads(void);   // BO1_MAX_GAMEPADS entries
```

XInput button bits: DPAD_UP 0x0001, DPAD_DOWN 0x0002, DPAD_LEFT 0x0004, DPAD_RIGHT 0x0008, START 0x0010,
BACK 0x0020, LEFT_THUMB 0x0040, RIGHT_THUMB 0x0080, LEFT_SHOULDER 0x0100, RIGHT_SHOULDER 0x0200, A 0x1000,
B 0x2000, X 0x4000, Y 0x8000.

Standard-mapping Gamepad API indices: buttons 0 A, 1 B, 2 X, 3 Y, 4 LB, 5 RB, 6 LT (analog `value`), 7 RT
(analog), 8 Back, 9 Start, 10 L3, 11 R3, 12 up, 13 down, 14 left, 15 right; axes 0 LX, 1 LY, 2 RX, 3 RY.

Rumble: when `rumble_low` or `rumble_high` is non-zero the page calls
`pad.vibrationActuator?.playEffect("dual-rumble", { duration: 100, strongMagnitude: rumble_low,
weakMagnitude: rumble_high })` at most every 50 ms.

Keyboard and mouse do not go through this bridge: the engine registers Emscripten html5 callbacks on the canvas
itself and asks for Pointer Lock while the game (not a menu) owns the mouse.

## 5. Lifecycle

| event | page | engine |
| --- | --- | --- |
| host starts | lobby server sends `room.started` with the `LaunchConfig` | - |
| boot | `createBo1(...)` with `engineCommands(launch)` | `main` → `Com_Init` → `Com_Frame` loop |
| quit / game over | - | calls `Module.onEngineExit?.(code)` (an `EM_JS` hook) before exiting |
| tab closed or room left | `Module.abort()` / page unload | - |
| error | shows `printErr` lines and the `Com_Error` text | `Sys_Error` calls `Module.onEngineError?.(message)` |
