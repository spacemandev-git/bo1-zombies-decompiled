# Engine to WebAssembly (web port)

The browser build of the engine: Emscripten, WebAssembly (wasm32), pthreads, WebGL2 through the D3D9 shim, the Origin
Private File System for the player's game files. The contract with the page (`web/`) is
[`docs/web-engine-interface.md`](web-engine-interface.md); this document is the engine side: how it is built, how it
is put together, what works and what is next.

## Status

| milestone | state |
| --- | --- |
| CMake preset `web` (and `web-debug`, `web-node`), MSVC build unchanged | done |
| Win32 / MSVC CRT compatibility layer (`src/web/compat/`) | done; `bo1_compat_test` passes |
| Compile: all 1186 engine translation units of the web build (+ 16 in `src/web`, + 18 of the D3D9 shim) | **1220 / 1220 compile** |
| Link (`web/engine/bo1.js`, `bo1.wasm` 8.7 MB, `build.json`) | **links, 0 undefined symbols** |
| Boot without game files, Node (`web-node`) | reaches fastfile loading: `Could not find zone '...\zone\english\en_code_pre_gfx_mp.ff'` |
| Boot without game files, headless Chrome (OPFS, COOP/COEP) | same point; reads `localization.txt` from OPFS, writes `console_mp.log` to `/opfs/bo1/home`, `onEngineError` / `onEngineExit` fire |
| Boot with game files (fastfiles, scripts, menus) | not tried yet (no game files on the build machine) |
| Rendering (D3D9 shim) | linked; shim in progress (`src/web/d3d9/README.md`) |
| Sound | null driver (voices are timed, nothing is heard) |
| Cinematics (Bink) | stubbed: every video ends at once |

## Building

Activate the SDK in every shell (`EMSDK_OS` is required in the sandboxed shells used here):

```sh
export EMSDK_PYTHON=/opt/homebrew/bin/python3.12 EMSDK_OS=macos
source ~/emsdk/emsdk_env.sh
```

| command | output |
| --- | --- |
| `cmake --preset web && cmake --build --preset web` | `web/engine/bo1.js`, `bo1.wasm`, `build.json` (served by `web/`) and `build/web/harness.html` |
| `cmake --preset web-debug && cmake --build --preset web-debug` | `build/web-debug/bin/`: `_DEBUG` (iassert on), `-O1 -g`, `-sASSERTIONS` (serve it from `web/engine/` with `-DBO1_WEB_OUTPUT_DIR=<repo>/web/engine`) |
| `cmake --preset web-node && cmake --build --preset web-node` | `build/web-node/bin/bo1.js`: headless Node build (no canvas, NODERAWFS) |
| `cmake --build --preset web-node --target bo1_compat_test` | `build/web-node/bin/bo1_compat_test.js`: checks of the Win32 emulation |

A full build takes about 40 s on 10 cores (the link about 10 s). Emscripten 6.0.11, CMake 4.4, Ninja.

Tools (`src/web/tools/`):

| tool | what |
| --- | --- |
| `census.py [--filter REGEX] [--full]` | compiles every TU of `build/web/compile_commands.json` with `-fsyntax-only` in parallel (~30 s), prints the per-folder table and the most frequent errors; logs in `build/web/census/` |
| `undefined.py [--by-object]` | the engine symbols the link cannot resolve (before Emscripten turns undefined functions into imports) |
| `census.py --warn=-Wcast-function-type-strict` then `fnptr_audit.py` | function pointer casts that change the WebAssembly signature (see "Function pointer casts") |
| `try_compile.py <files>` | syntax-checks files that are not in the web build with its flags |
| `node-boot-test.sh [game dir] [seconds]` | runs the Node build; without a game dir it uses an empty one with `localization.txt` |
| `serve.py [port]` | serves the repo with COOP/COEP; open `http://localhost:8642/build/web/harness.html` (import a game folder into OPFS, start the engine, see its console) |

Configuration (`cmake -D...` on the `web` preset): `BO1_WEB_MEMORY_MB` (default 1536, fixed size, no growth),
`BO1_WEB_ALLOW_UNDEFINED` (default ON: undefined functions become imports that abort when called),
`BO1_WEB_RENDERER` (default ON when `src/web/d3d9/include/d3d9.h` exists), `BO1_WEB_OUTPUT_DIR`.

The Windows build does not change: `CMakeLists.txt` includes `cmake/web.cmake` and returns when the Emscripten
toolchain is active; everything after that block is the MSVC build as before.

## Architecture

### Build (`cmake/web.cmake`)

- `cmake_files.cmake` stays the only list of engine sources. The web build takes the target it creates, removes the
  folders listed in `BO1_WEB_EXCLUDE` (regexes), adds back the portable `src/win32/` files (`BO1_WEB_SHARED_WIN32`) and
  adds `src/web/*.cpp` and `src/web/compat/*.cpp`.
- Defines: `BO1_WEB`, `BO1_MP`, `_CONSOLE`, `_CRT_SECURE_NO_WARNINGS`, `NDEBUG` / `_DEBUG`. `WIN32` / `_WIN32` are
  **not** defined; the two places that needed the WIN32 path say `#if defined(WIN32) || defined(BO1_WEB)`
  (`universal/q_shared.h`, `client_mp/cl_main_pc_mp.cpp`).
- Include order: `src/web/compat` first (it provides `windows.h` and the other Windows headers), then
  `src/web/d3d9/include`, then the MSVC build's folders, then `src/web/compat/case` (forwarders for `Windows.h`,
  `DbgHelp.h`, ... on case-sensitive file systems).
- Every TU gets `-include src/web/compat/bo1_web_prelude.h` (the MSVC CRT extensions and intrinsics MSVC provides
  without an include).
- Compiler: `-std=gnu++20 -pthread -fwasm-exceptions -sSUPPORT_LONGJMP=wasm -msimd128 -msse -msse2 -fms-extensions
  -fdeclspec -mms-bitfields -fno-strict-aliasing -fwrapv -fno-delete-null-pointer-checks -O2`, warnings off for engine
  files (`-w`), a few clang errors that MSVC accepts demoted (`-Wno-c++11-narrowing`, `-Wno-register`, ...).
  `-mms-bitfields` gives MSVC's bit-field layout: every `static_assert(sizeof(...))` in the engine holds on wasm32.
- Link: `-sPROXY_TO_PTHREAD -sEXIT_RUNTIME -sWASMFS -sINITIAL_MEMORY=1536MB -sALLOW_MEMORY_GROWTH=0 -sSTACK_SIZE=8MB
  -sDEFAULT_PTHREAD_STACK_SIZE=2MB -sPTHREAD_POOL_SIZE=16 -sMALLOC=mimalloc -sMODULARIZE -sEXPORT_NAME=createBo1
  -sEXPORT_ES6 -sOFFSCREENCANVAS_SUPPORT -sMIN/MAX_WEBGL_VERSION=2`, exports `_main`, `_bo1_net_get_shared`,
  `_bo1_input_gamepads`, `--profiling-funcs` (names in stack traces), `-Wl,--wrap=fopen,remove,rename` (path
  resolution for plain C file calls).
- The D3D9 shim is its own static library (`src/web/d3d9/d3d9shim.cmake`, target `bo1_d3d9shim`); `web.cmake` adds the
  compat include folder and the engine's wasm features (threads, wasm exceptions, SIMD, MS extensions) to it and links
  it.
- A POST_BUILD step writes `build.json` (`cmake/web_buildinfo.cmake`: `git describe`, UTC time).

### Threads

`main()` (`src/web/web_main.cpp`) runs in a worker (`PROXY_TO_PTHREAD`); the page's main thread only runs JS glue,
the html5 input callbacks and the bridge. The engine's own threads (render back end, server, database, stream, sound
occlusion, TL job queue workers) are created through `CreateThread` as on Windows: `src/web/compat/win_sync.cpp`
implements threads (`CREATE_SUSPENDED` / `ResumeThread`), events (manual / auto reset), mutexes, semaphores,
`WaitForSingleObject(Ex)` / `WaitForMultipleObjects(Ex)` with timeouts, critical sections (recursive pthread
mutexes), SRW locks, TLS and APCs over pthreads. `qcommon/threads.cpp` compiles unchanged; the MSVC thread-naming
exception (`RaiseException(0x406D1388)`) names the thread in the browser's devtools.

`Interlocked*` / `_Interlocked*` are sequentially consistent `__atomic` builtins, cast like MSVC's lenient intrinsics
(the decompiled code passes `volatile unsigned int *`, `int *`, `T **`).

### Files (OPFS)

- `main()` mounts the OPFS backend at `/opfs` (`wasmfs_create_opfs_backend`), creates `fs_b` / `fs_h` (from the command
  line; defaults `/opfs/bo1/game`, `/opfs/bo1/home`), makes `fs_b` the exe folder (`GetModuleFileNameA` ->
  `Sys_DefaultInstallPath`) and the current directory, and points `SHGetFolderPathA` at `fs_h`.
- Every path the engine opens goes through `src/web/compat/web_path.cpp`: `\` or `/`, relative to the tracked current
  directory, each component looked up exact first then case-insensitively in a cached directory listing. Writes through
  the emulation invalidate the directory. Used by `CreateFileA`, `FindFirstFileA`, `GetFileAttributesA`, `_findfirst*`,
  `_mkdir`, `_access`, ..., and by plain `fopen` / `remove` / `rename` (wrapped at link time).
- Fastfiles: `database/db_file_load.cpp` is unchanged. Its unbuffered overlapped reads (`ReadFileEx` 256 KB into a
  512 KB ring, `SleepEx(INFINITE, TRUE)` per chunk on the database thread) work because `ReadFileEx` reads
  synchronously at the OVERLAPPED offset and queues its completion routine as an APC of the calling thread, and an
  alertable wait runs one APC per return (the loader counts exactly one chunk per `SleepEx`). Past the end of the file
  it fails with `ERROR_HANDLE_EOF`, as the loader expects.
- Node build: WasmFS `NODERAWFS` roots `/` at the process's current directory, so the Node test runs from `/` with
  absolute paths (`node-boot-test.sh` does this).

### Memory

`VirtualAlloc(MEM_RESERVE)` allocates the whole range at once (64 KB aligned, zeroed); commit / decommit only change
per-page bookkeeping that `VirtualQuery` reports (`universal/com_memory.cpp` walks it); re-committed pages are zeroed;
`MEM_RELEASE` frees. PMem (328 MiB, +256 MiB with `bo1_mod_zones`) and the hunk come from there. The wasm memory is a
fixed 1536 MiB (`BO1_WEB_MEMORY_MB`): memory growth with pthreads slows every JS access to the heap (the WebGL shim makes
many). `GlobalMemoryStatus` reports the wasm memory (so `sys_sysMB` is 1024, the engine's cap).

### Network (`src/web/web_net.cpp`)

Replaces `win32/win_net.cpp`. Two rings in wasm memory per the contract (section 3): `Sys_SendPacket` writes `to_page`
and calls `Atomics.notify` on its `write_index`; `Sys_GetPacket` polls `from_page`; `NET_Sleep` waits on `from_page`'s
`write_index`. Each direction has a lock, so several engine threads (main, server) can send and receive while each
ring keeps one producer and one consumer. `NET_OpenIP` sets `net_ip` to the page's `local_ip` (`10.66.0.<slot+1>`)
and `net_port` to 28960. Broadcast packets are dropped (no broadcast route in the contract). No DNS: names other than
`localhost` do not resolve. The remote debug socket (`net_listen`, `net_connect`) is absent.

### Input

- Keyboard / mouse (`src/web/web_input.cpp`, replacing `win_input.cpp` and the input half of `win_wndproc.cpp`):
  Emscripten html5 callbacks on the page's main thread record into a lock-free queue (keys, characters, mouse buttons,
  wheel) and atomics (mouse motion). The engine thread drains the queue in `Win_GetEvent` with `win_wndproc.cpp`'s
  `virtualKeyConvert` table (DOM `keyCode` = Windows virtual key; the extended bit from `KeyboardEvent.location`;
  `Backquote` = the console key 126) and calls `CL_MouseEvent` in `IN_Frame`. When the game owns the mouse
  (`CL_MouseEvent` asks to recenter) the next click requests Pointer Lock; leaving that state exits it. F5, F11, F12
  stay with the browser; keys are ignored while a page text field has focus.
- Gamepads (`src/web/web_gamepad.cpp`): the backend of `client/gpad_backend.h` over the page-written `bo1_gamepad`
  array (contract section 4); a read is retried when `seq` moves during it; rumble goes to `rumble_low` /
  `rumble_high`.

### Render thread and the canvas

The page's `<canvas>` is transferred to the engine's main pthread (`OFFSCREENCANVAS_SUPPORT`, default
`OFFSCREENCANVASES_TO_PTHREAD=#canvas`). The shim creates its WebGL2 context on the thread that calls
`IDirect3D9::CreateDevice` and makes every GL call there. The engine creates the device on its main thread
(`R_Init` -> `R_InitGraphicsApi` -> `R_CreateDevice`) and then normally renders on `RB_RenderThread`. The web build
therefore starts with `+set r_smp_backend 0` (prepended in `web_main.cpp`, so the page can override it): the back end
runs on the main thread, which owns the canvas. Running the back end on its own thread later needs either the device
created there or the canvas handed to that thread (`bo1_web_next_thread_takes_canvas()` in `compat/windows.h` makes
the next `CreateThread` take the canvas). Frame pacing: the browser build runs one `Com_Frame` per
`emscripten_set_main_loop` callback (requestAnimationFrame) instead of WinMain's `for (;;)`, so the thread returns to
its event loop every frame; that is when the OffscreenCanvas frame is shown and WebGL query results advance (the
`web-node` build keeps the plain loop). The window is a fake HWND whose client size is what the engine gives
`CreateWindowExA` / `SetWindowPos` (the back buffer size in windowed mode); display modes are a fixed list.

### Sound (`src/web/web_sound.cpp`)

The `SD_*` driver API as a null driver: voices are timed exactly as XAudio2 would play them (start after the start
delay, `SamplesPlayed` at rate x pitch, end when their buffers have played, loops until stopped), streamed voices take
two windows at a time from `snd_stream.cpp` and give each back when played, so the stream thread keeps reading and
EOF ends the voice. Nothing is audible yet. The XAudio2 types the sound headers embed are declared (types only) in
`compat/XAudio2.h` / `XAPOBase.h`.

### Errors and lifecycle

`Sys_Error`, fatal out-of-memory and failed asserts call `Module.onEngineError(text)` on the page's main thread
(`MAIN_THREAD_EM_ASM`); quitting calls `Module.onEngineExit(code)` and ends the runtime (`_Exit`: no static
destructors while other engine threads run, like `ExitProcess`). `__debugbreak` (the last step of every failed assert,
as on Windows) prints the location and a stack trace, reports it and aborts. Console output is `stdout`
(`Module.print`); `OutputDebugStringA` and `MessageBoxA` go to `stderr` (`MessageBoxA` answers "no" to yes/no
questions and never blocks).

## Compatibility layer (`src/web/compat/`)

| file | provides |
| --- | --- |
| `bo1_web_prelude.h` | (force-included) MSVC CRT names (`_stricmp`, `_snprintf` with MSVC semantics, `sprintf_s` & co. incl. the array templates, `_itoa`, `_aligned_malloc`, `_time64`, ...), intrinsics (`__debugbreak`, `_Interlocked*`, `_BitScan*`, `__rdtsc`, `__readfsdword(0x24)` = thread id, `_ReturnAddress` = 0, `__cpuid`), `_setjmp` / `longjmp` on the engine's `int[16]` buffers, renames for names MSVC lacks but musl has (`random`, `__unaligned`, `_iobuf`) |
| `windows.h` | the Win32 types, structs, constants and declarations the engine uses; COM base (`IUnknown` with its uuid, `GUID`, `HRESULT`, `STDMETHOD`), SEH macros (`__try` runs its block, `__except` never), Winsock 1 via `winsock2.h` |
| `win_sync.cpp` | threads, waits, events, mutexes, semaphores, critical sections, SRW, TLS, APCs, `_beginthreadex` |
| `win_file.cpp`, `web_path.cpp` | files, find, directories, the fopen/remove/rename wraps, case-insensitive resolver, tracked cwd |
| `win_memory.cpp` | `VirtualAlloc` / `VirtualFree` / `VirtualQuery`, `Global*`, `Local*`, `Heap*`, memory status |
| `win_misc.cpp` | errors, time (QPC = 1 MHz monotonic), system info, locale (en-US), UTF-8 / Latin-1 conversions, modules (no DLLs), processes, Toolhelp, Winsock stubs |
| `win_gui.cpp` | the fake window, messages (none), cursor, monitors, display modes, clipboard (none), key state |
| `win_crt.cpp` | the prelude's CRT functions, `bo1_web_debugbreak`, `bo1_web_exit`, the page hooks |
| `intrin.h`, `mmintrin.h`, `xmmintrin.h` | SSE through Emscripten's wasm SIMD headers; MMX (`__m64`) emulated (gfx_d3d skinning) |
| `XAudio2.h`, `XAPOBase.h`, `dsound.h` | types only (no implementation) |
| `io.h`, `direct.h`, `process.h`, `ShlObj.h`, `dbghelp.h`, `psapi.h`, `tlhelp32.h`, `sal.h`, `mmsystem.h`, `memoryapi.h`, `minwindef.h`, `wtypes.h`, `corecrt_malloc.h`, `winsock*.h`, `ws2tcpip.h` | what their names say |

## Platform layer (`src/web/`)

| `src/win32/` file | web build |
| --- | --- |
| `win_common.cpp`, `win_configure.cpp`, `win_content.cpp`, `win_gamerprofile.cpp`, `win_localize.cpp`, `win_splash.cpp`, `win_steam.cpp`, `win_stream.cpp`, `win_tasks.cpp`, `win_workercmds.cpp` | compiled as they are (`BO1_WEB_SHARED_WIN32`) |
| `win_main.cpp`, `win_syscon.cpp`, `win_mini_dumper.cpp` | `web_main.cpp` (entry point, event queue, `Sys_Error` / `Sys_Quit` / `Sys_Print`, `Sys_Init`, info dvars; the headless test modes do not exist on the web) |
| `win_shared.cpp` | `web_shared.cpp` (`Sys_Milliseconds`, `Sys_SnapVector` with x87 rounding) |
| `win_net.cpp` | `web_net.cpp` |
| `win_input.cpp`, `win_wndproc.cpp` | `web_input.cpp` |
| `win_gamepad.cpp` | `web_gamepad.cpp` (backend of `client/gpad_backend.h`) |
| `win_voice.cpp`, `win_libspeex_misc.cpp` | `web_stubs.cpp` (no voice) |

Also: `web_fncast.h` (`BO1_FNCAST` thunks), `web_sound.cpp` (SD_* null driver), `web_cinematic.cpp` (Bink API,
`DirectSoundCreate8`), `web_steamapi.cpp` (Steamworks C API: "Steam is not running"), `web_stubs.cpp` (voice, monkey,
movie capture, NvAPI, CubeMapGenLib), `web_bridge.h` (the contract's structs and exports), `test/compat_test.cpp`.

## Excluded from the web build

| what | why | instead |
| --- | --- | --- |
| `src/win32/` (10 of 20) | Windows platform layer | `src/web/` (table above) |
| `src/tracy/` | profiler client | none |
| `src/binklib/` | Bink is a prebuilt Windows DLL | `web_cinematic.cpp`: `BinkOpen` fails, videos end at once (`r_cinematic.cpp` is compiled) |
| `src/mjpeg/`, `src/vpx/` | movie capture (`movie_start`) | stubs: never encoding |
| `src/CubeMapGenLib/` | tools-only filtering, wide-char Win32 UI | stub class (r_screenshot only constructs it) |
| `src/nvapi/` (library) | NVIDIA driver API | stubs: `NvAPI_Initialize` fails, `dx.nvInitialized` stays 0 |
| `src/groupvoice/` | voice chat (DirectSound capture, speex) | `Voice_*` stubs; `sv_voice` is 0 |
| `src/monkey/` | test automation over Winsock | stubs: never running |
| `src/sound/snd_driver_xaudio2*.cpp` | XAudio2 | `web_sound.cpp` |

Steam is not excluded: `live/live_steam*.cpp` and `win32/win_steam.cpp` compile and take their existing no-Steam path
(`SteamAPI_Init` reports "no Steam client").

## Changes to shared engine code

All are either `#ifdef BO1_WEB` or no-ops for MSVC, tagged `// web:`. `tools/syntax-check.sh` over every TU shows no new
failures against `tools/syntax-check/known-failures.txt` (10 files have fewer errors than listed).

| file | change |
| --- | --- |
| `CMakeLists.txt` | `if (EMSCRIPTEN) include(cmake/web.cmake) return() endif()` after `include(cmake_files.cmake)` |
| `universal/q_shared.h` | the `WIN32` defines block also for `BO1_WEB` (PITCH/YAW/ROLL, byte order, PATH_SEP) |
| `client_mp/cl_main_pc_mp.cpp` | `CL_CDKeyValidate`: `#if defined(WIN32) \|\| defined(BO1_WEB)` |
| `qcommon/msg.cpp` | `MSG_RoundFloatToInt`: x87 `fld/fadd/fistp` as double arithmetic + `rint` under `BO1_WEB` (below) |
| `gfx_d3d/r_cinematic.h` | Bink headers included with `_WIN32` defined around them under `BO1_WEB` (RAD's platform detection) |
| `gfx_d3d/rb_backend.h` | `_D3D9_H_` defined under `BO1_WEB` before `nvapi.h` (it keys its D3D9 entry points on the real header's guard) |
| `physics/phys_gjk.cpp` | the "expects x87 floats" `#error` skipped under `BO1_WEB` |
| `qcommon/bitarray.h`, `universal/q_parse.cpp`, `ddl/ddl_api.cpp`, `gfx_d3d/r_warn.cpp` | `char *` va_list copies declared `va_list` (MSVC's `va_list` is `char *`) |
| `universal/memfile.cpp` | `lzo_init()` without the arguments MSVC silently dropped |
| `clientscript/cscr_debugger.h` | class `operator new` takes `decltype(sizeof(0))` (size_t), not `unsigned int` |
| `game/g_load_utils.cpp` | global `index` renamed `s_vtosIndex` (POSIX `index()` collides) |
| `gfx_d3d/r_dpvs_static.cpp`, `gfx_d3d/r_model_skin.cpp` | `alignas(16)` moved before the type |
| `gfx_d3d/rb_resource.cpp`, `universal/com_expressions_eval.cpp` | explicit function pointer -> `void *` casts |
| `gfx_d3d/r_model_skin_sse.cpp` | `__m128` initializers without the inner braces; `m128_u64` / `m128_u32` member reads as pointer reads |
| `gfx_d3d/rb_backend.cpp` | the file's global `data` made `static` (Itanium mangling collides with `r_water_sim.cpp`'s) |
| `gfx_d3d/r_dpvs_sceneent.cpp` | its static `R_CullSphereDpvs` copy gets a file-local name (MSVC gave the static redeclaration internal linkage) |
| `physics/phys_local.h` | `#include <new>` |
| `physics/phys_broad_phase.h`, `demo/demo_common.h` | two never-defined virtual "key functions" defined inline (the Itanium ABI emits typeinfo/vtables with them) |
| `game_sp/g_sp_save_actor.inl` | `offsetof(actor_s, sentientInfo.inl[n]...)` (offsetof cannot go through `operator[]`) |
| `ui/ui_main.cpp` | `const serverFilter_s serverFilters[1] = {};` |
| `universal/q_shared.h` | `BO1_FNCAST(T, fn)`: the plain cast on Windows, a signature-fixing thunk on the web (below) |
| `qcommon/tl_support.cpp`, `universal/mem_userhunk.cpp`, `sound/snd_bank.cpp`, `physics/phys_main.cpp`, `game/g_missile.cpp`, `gfx_d3d/r_image.cpp`, `r_material.cpp`, `r_model.cpp`, `r_font.cpp`, `cgame/cg_visionsets.cpp`, `ui/ui_viewer.cpp`, `ui/ui_shared_obj.cpp`, `xanim/xanim.cpp`, `xanim/xmodel.cpp`, `game_mp/ui_gameinfo_mp.cpp`, `game_mp/g_scr_main_mp.cpp` | casts that change a call's signature written as `BO1_FNCAST(T, fn)` (33 sites) |
| `gfx_d3d/rb_backend.cpp` | `rb_tessTable` call: under `BO1_WEB` the two context pointers are passed as the `GfxCmdBufContext` the entries take |

## Compile census

| area | TUs | compile (web) | not in web build |
| --- | ---: | ---: | --- |
| CubeMapGenLib | 4 | 0 | 4 (excluded) |
| DW | 14 | 14 | |
| DemonWare | 9 | 9 | |
| DynEntity | 6 | 6 | |
| EffectsCore | 20 | 20 | |
| aim_assist | 2 | 2 | |
| bgame | 26 | 26 | |
| binklib | 2 | 0 | 2 (excluded) |
| cgame | 45 | 45 | |
| cgame_mp | 22 | 22 | |
| client | 18 | 18 | |
| client_mp | 13 | 13 | |
| clientscript | 17 | 17 | |
| common | 1 | 1 | |
| database | 9 | 9 | |
| ddl | 5 | 5 | |
| demo | 7 | 7 | |
| devgui | 3 | 3 | |
| flame | 10 | 10 | |
| game | 55 | 55 | |
| game_mp | 17 | 17 | |
| game_sp | 31 | 31 | |
| gfx_d3d | 130 | 130 | |
| glass | 6 | 6 | |
| groupvoice | 41 | 0 | 41 (excluded) |
| ik | 5 | 5 | |
| jpeg | 46 | 46 | |
| libs/libtomcrypt-1.17 | 290 | 290 | |
| libs/libtommath-1.0 | 128 | 128 | |
| live | 31 | 31 | |
| minilzo | 1 | 1 | |
| mjpeg | 3 | 0 | 3 (excluded) |
| monkey | 2 | 0 | 2 (excluded) |
| physics | 30 | 30 | |
| qcommon | 41 | 41 | |
| ragdoll | 5 | 5 | |
| server | 4 | 4 | |
| server_mp | 10 | 10 | |
| sound | 18 | 16 | 2 (XAudio2 driver) |
| stringed | 4 | 4 | |
| tl | 8 | 8 | |
| tracy | 1 | 0 | 1 (excluded) |
| turret | 1 | 1 | |
| ui | 20 | 20 | |
| ui_mp | 4 | 4 | |
| universal | 40 | 40 | |
| vehicle | 4 | 4 | |
| vpx | 1 | 0 | 1 (excluded) |
| win32 | 20 | 10 | 10 (replaced by src/web) |
| xanim | 11 | 11 | |
| zlib | 11 | 11 | |
| **total** | **1252** | **1186** | 66 |

Plus `src/web` (9 platform + 7 compat TUs) and the D3D9 shim (14 + 4 MojoShader): 1220 TUs in all, all compiling.

## Known issues and risks

- **Floating point.** MSVC's x86 build uses SSE2 scalar math like wasm, so most results match. Exceptions:
  - `MSG_RoundFloatToInt` (snapshot origin quantization) is computed as x87 does it at 53/64-bit precision control
    (the Windows thread default, which the host's server thread uses to write snapshots). Without
    `D3DCREATE_FPU_PRESERVE`, Direct3D 9 switches the thread that creates the device (the Windows main thread, which
    reads snapshots) to 24-bit precision; there `value + 2^-30` rounds back to `value` and exact .5 values round to
    even instead of up. A Windows client and a web host could therefore disagree on exact .5 origins; between two web
    builds, or a web client and a Windows host, the writer's and the reader's arithmetic is the same. Worth a check in a
    mixed game.
  - `phys_gjk.cpp` was built with `/arch:IA32` (x87 extended precision) because GJK hit its iteration cap in SSE
    precision; the web build has IEEE single/double only. Watch for "gjk max iterations" warnings.
- **Function pointer casts.** See the section below: the casts that would trap on the common paths are fixed; 58
  flagged casts remain in dead, tool-only or loose-file (non-fastfile) code, and casts *from* `void (*)()` are not
  reported by clang at all.
- `cgame/cg_draw_reticles.cpp:1043`: `a < leftArc - rightArc < b`, a chained comparison (the decompiled `&&` sits in
  the commented line above). MSVC and clang compile the same (wrong-looking) expression; `-Wno-parentheses` keeps it
  quiet. Probably a decompilation bug for the gameplay side.
- `wchar_t` is 32-bit (Windows: 16). Only the Win32 wide conversions use it; fastfile structures do not.
- `long double` is 128-bit on wasm (MSVC: 64). A handful of uses (`SysInfo`, menu parser, timing); not in loaded data.
- Emscripten 6.0.11 bugs worked around: the OPFS backend's directory listing needs `stringToUTF8OnStack` (exported in
  `web.cmake`; without it every `readdir` under `/opfs` fails with EIO); WasmFS `getcwd()` drops a mount point's name
  (`/opfs/bo1/game` -> `//bo1/game`), so the emulation tracks the current directory itself.
- Contract: `bo1_gamepad`'s fields end at byte 44 while the contract (and the page, `GAMEPAD_STRIDE` 48) say 48;
  `web_bridge.h` adds `uint32_t reserved` so the engine matches the page. The contract's struct should show that field.
- `Sys_SuspendOtherThreads` (fatal errors) cannot stop pthreads; after `Sys_Error` other engine threads run on for a
  few milliseconds until the runtime ends.
- First boot cost: 16 pool workers each instantiate the 8.7 MB module.

## Function pointer casts

Decompiled code calls functions through casts to other function types: `XAnimFindData_FastFile(name)` called as
`(name, alloc)`, 3-parameter allocators in 4-parameter table slots (`mem_userhunk.cpp`, the TL callbacks), 1-parameter
`DB_EnumXAssets` callbacks called as `(header, data)`, the spawn and trace filter callbacks. x86 cdecl tolerates it;
WebAssembly traps on an indirect call whose type is not the callee's ("function signature mismatch"). wasm-ld reports
mismatched *direct* calls and reported none.

`BO1_FNCAST(T, fn)` (`universal/q_shared.h`, `src/web/web_fncast.h`) replaces such a cast: on Windows it is `((T)(fn))`,
on the web the address of a thunk of exactly type T that calls fn with the arguments fn declares (same-size
arguments keep their bits, as the x86 stack slot would; numbers convert; missing ones are zero) and converts the
result (void gives zero). `rb_backend.cpp`'s `rb_tessTable` call is fixed by hand (a struct passed as two pointers).

Finding them: `python3 src/web/tools/census.py --warn=-Wcast-function-type-strict` compiles everything with that
warning, `python3 src/web/tools/fnptr_audit.py` keeps the casts whose wasm signatures differ (every pointer, int, enum,
bool, char and short is an i32 in wasm, so most of clang's reports are harmless). Now: 58 remain, all in code that the
web build does not run in normal play - DemonWare file sharing (`live_fileshare.cpp`, `live_storage*.cpp`), the effects
profiler (`fx_profile.cpp`), dev graphs (`graph.cpp`, `devgui.cpp`, `cg_draw_debug.cpp`), demo file I/O
(`demo_common.cpp`), loose-file asset loading (`*_load_obj.cpp`, the LoadObj branch of `DB_EnumXAssets`) - plus two
harmless ones (`g_spawn_mp.cpp` re-casts before calling; `cg_info.cpp` bool vs unsigned char). clang does not report casts
*from* `void (*)()` (`tl_support.cpp`'s `TL_ReleaseFile` was one, fixed); those show up as traps at run time.

## Next

1. **Real boot with game files** (Node first). `node-boot-test.sh /path/to/BlackOps` against a real install:
   fastfile loading through the APC path, IWDs, scripts, `map frontend`. A dedicated server
   (`+set dedicated 1 +map zombie_theater`) exercises the database, scripts, AI and physics without a renderer.
   Expect function-signature traps and asserts; fix each at the source.
2. **D3D9 shim integration** (with the shim agent): device creation on the main thread with `r_smp_backend 0`; then
   decide how the back end gets its own thread (create the device on `RB_RenderThread`, or proxy).
3. **Web Audio driver**: replace the null driver's "advance" with a mixer (PCM16 and MS-ADPCM decode, the engine's pan
   matrix from `SDXA2_UpdateVoiceSends`, the DSP chain of `snd_driver_xaudio2_dsp.cpp`, radverb) feeding an
   AudioWorklet through a SharedArrayBuffer ring.
4. **Cinematics**: a Bink 1 decoder (wasm) or pre-transcoded videos through WebCodecs writing the Y/cR/cB/A planes
   `r_cinematic.cpp` uploads.
5. **Performance**: profile a map (Chrome's wasm profiler with `--profiling-funcs`), then `-O3`, LTO, wasm size,
   thread count, the global SSE emulation hot spots.

### Parallel work for the next session

These touch disjoint files and can run as separate agents:

| agent | scope | files |
| --- | --- | --- |
| boot (Node, game files) | run `web-node` against an install, fix runtime traps/asserts in shared code | engine `src/**` (small guarded fixes), `src/web/web_main.cpp` |
| renderer | gfx_d3d on the shim: device thread, r_smp_backend, skinning, render targets | `src/gfx_d3d/**` guards, `src/web/d3d9/**` |
| audio | Web Audio mixer behind `SD_*` | `src/web/web_sound.cpp`, new `src/web/web_audio*.cpp`, page AudioWorklet |
| cinematics | Bink replacement | `src/web/web_cinematic.cpp` |
| page integration | canvas sizing, focus / pointer lock UX, OPFS importer, lifecycle | `web/client/**`, `src/web/web_input.cpp` |
| network | two-browser co-op through the rings, latency, broadcast (if the lobby wants LAN discovery) | `src/web/web_net.cpp`, `web/client/src/net/**` |
| audits | function pointer casts (scan casts of builtin tables), x87-dependent code, struct layouts of networked data | engine `src/**` |
