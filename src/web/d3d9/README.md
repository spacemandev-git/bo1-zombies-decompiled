# Direct3D 9 -> WebGL2 shim (src/web/d3d9)

The web build compiles the unmodified renderer (`src/gfx_d3d`, ~130 TUs, ~490 D3D9 call sites) against this
directory's `include/` instead of the DirectX SDK. The headers declare the real D3D9 types, enums (real numeric
values), structs (real layouts and `_D3DXXX` tag names) and COM interfaces (real method names, parameter lists and
vtable order); the `.cpp` files implement them on WebGL2 (OpenGL ES 3.0 through Emscripten). Shader bytecode from
the fastfiles (vs_3_0 / ps_3_0) is translated to GLSL ES 3.00 at runtime with MojoShader (`third_party/mojoshader`).

Owner of this directory and `third_party/mojoshader`: the D3D9 shim agent.

**Status (2026-10-07).** All IDirect3D9 / IDirect3DDevice9 methods and resource interfaces the engine uses are
implemented. All 232 engine TUs that include D3D headers (`src/gfx_d3d` and friends) compile against them with the web
build's flags, and every D3D symbol the engine objects reference is defined by `libbo1_d3d9shim.a`. Unit tests:
343/343 (node) + 22/22 glslang GLSL ES 3.00 compile/link checks + 1161 header constants / 91 struct sizes
cross-checked against MinGW-w64. Shader corpus: 79/80 of Wine's d3d9 test shaders translate (the 80th reads an
uninitialized temp on purpose) and all 79 compile + link with glslang and with Chromium's WebGL2 (ANGLE Metal,
SwiftShader). Browser smoke tests: 86/86 (single-threaded) and 88/88 (`PROXY_TO_PTHREAD` + OffscreenCanvas) on
Chromium with ANGLE Metal and SwiftShader. Not yet run against real game assets (no fastfiles in the repo) - see
"Plan".

## Integration (for the web build / platform agent)

### Include path and sources

* Add `src/web/d3d9/include` to the include path of every engine TU (it provides `d3d9.h`, `d3d9types.h`,
  `d3d9caps.h`, `d3dx9.h`, `dxerr.h`, `ddraw.h` and the web-only `d3d9shim.h`). It must come before any directory
  that might also contain DirectX headers.
* `include(${CMAKE_SOURCE_DIR}/src/web/d3d9/d3d9shim.cmake)` defines the static library `bo1_d3d9shim` (shim `.cpp`
  files picked up by glob + the four MojoShader `.c` files + MojoShader's config defines; include dir PUBLIC; link
  options `-sMAX_WEBGL_VERSION=2 -sMIN_WEBGL_VERSION=2` INTERFACE). `cmake/web.cmake` already does this and compiles
  the library against `src/web/compat` with the engine's wasm flags - keep it that way (both sides must agree on the
  Win32/COM types).
* Without CMake: compile `src/web/d3d9/d3d9_*.cpp src/web/d3d9/d3dx9_*.cpp` (C++17) and
  `third_party/mojoshader/{mojoshader.c,mojoshader_common.c,profiles/mojoshader_profile_common.c,profiles/mojoshader_profile_glsl.c}`
  with `SUPPORT_PROFILE_{D3D,BYTECODE,HLSL,GLSL120,ARB1,ARB1_NV,METAL,SPIRV,GLSPIRV}=0 MOJOSHADER_NO_VERSION_INCLUDE`.

### Win32 base types (contract with src/web/compat)

`include/d3d9shim_win32.h` decides how to get Win32/COM base types:

* If `<windows.h>` is reachable (`__has_include`), it is included (plus `<objbase.h>`/`<unknwn.h>` if present) and
  **its** types are used. It must provide `BYTE WORD DWORD UINT INT LONG ULONG BOOL HRESULT HANDLE HWND (HWND__*)
  HMONITOR (HMONITOR__*) RECT POINT LARGE_INTEGER (.LowPart/.QuadPart) GUID (struct _GUID) IID REFIID REFGUID
  IUnknown (QueryInterface/AddRef/Release, no virtual destructor) S_OK S_FALSE E_FAIL E_NOTIMPL E_OUTOFMEMORY
  E_INVALIDARG E_NOINTERFACE E_POINTER SUCCEEDED FAILED MAKE_HRESULT CONST WINAPI LPSTR LPCSTR LPVOID`, with `DWORD`
  = `unsigned long` like MSVC. `src/web/compat/windows.h` does (checked).
* `RGNDATA`, `PALETTEENTRY`, `HDC`, `LUID` are only used as `struct _RGNDATA*`, `struct tagPALETTEENTRY*`,
  `struct HDC__*`, `struct _LUID*` and need not exist.
* Otherwise (shim unit/smoke tests) the header defines everything itself. Force a mode with
  `-DD3D9SHIM_USE_WINDOWS_H=1/0`. The headers also define `MAKEFOURCC` if absent, nothing else from Win32.

### Link flags

* `-sMAX_WEBGL_VERSION=2 -sMIN_WEBGL_VERSION=2` (required; there is no WebGL1 fallback). No `-lGL` needed. Do not
  use `-sFULL_ES2/-sFULL_ES3` (no client-side arrays are used) or `-sLEGACY_GL_EMULATION`.
* With `-sPROXY_TO_PTHREAD`: `-sOFFSCREENCANVAS_SUPPORT=1` (default `-sOFFSCREENCANVASES_TO_PTHREAD=#canvas`
  transfers the canvas to the pthread running `main`). This is exactly what `smoke_mt.html` is built with.
* Release builds may add `-sGL_TRACK_ERRORS=0`. `-sGL_SUPPORT_EXPLICIT_SWAP_CONTROL` / `-sOFFSCREEN_FRAMEBUFFER`
  are not used.

### Canvas, context, threads, frame pacing

* `IDirect3D9::CreateDevice` creates (or, after a previous device on the same canvas was released, reuses) a WebGL2
  context with `emscripten_webgl_create_context(selector)`: alpha/depth/stencil/antialias/preserveDrawingBuffer off,
  high-performance; extensions enabled explicitly. Selector default `"#canvas"`; override with
  `d3d9shim_set_canvas_selector()` (`include/d3d9shim.h`) before `CreateDevice`. The `HWND` is ignored (any non-null
  dummy is fine). The canvas pixel size is set to `BackBufferWidth/Height` on `CreateDevice` and `Reset`
  (`emscripten_set_canvas_element_size`); CSS size is the page's business.
* **The thread that calls `CreateDevice` must own the canvas** (the OffscreenCanvas is transferred to exactly one
  pthread) and becomes the *device thread*: every WebGL call happens there. In the engine that is `RB_RenderThread`
  (`R_Init`). Either run the backend on `main`'s pthread (`r_smp_backend 0`), or create the render thread with
  `emscripten_pthread_attr_settransferredcanvases(&attr, "#canvas")` from the thread that owns the canvas.
* Safe from any thread (no GL inside; per-resource mutex): VB/IB `Lock/Unlock`, texture/cube/volume/surface
  `LockRect/UnlockRect/LockBox/UnlockBox` (except render targets), `AddRef/Release` (a final release off the device
  thread is queued and executed on the device thread), EVENT `Issue/GetData`, `SetGammaRamp`. Locks write CPU
  copies; uploads happen on the device thread at the next draw/`SetTexture`/`PreLoad`/present. The engine does this
  from worker threads (water VB, water texture, fence polls) - covered by `smoke_mt.html`.
* Everything else must run on the device thread; off-thread calls log a warning once per method.
* **Frame pacing**: `Present` blits the back buffer to the canvas, but a pthread-owned OffscreenCanvas is only shown
  when that thread **returns to its event loop**, and WebGL query results also only advance between event-loop
  tasks. The engine's render thread must yield once per frame (drive one backend frame per
  `emscripten_set_main_loop` callback on that thread, or `emscripten_sleep(0)` under ASYNCIFY/JSPI). The shim never
  blocks on the GPU (occlusion `GetData` never returns `S_FALSE`), so a non-yielding loop does not deadlock - it just
  never displays anything. The browser build does this: `src/web/web_main.cpp` runs one `Com_Frame` per
  `emscripten_set_main_loop` callback on the main pthread, which owns the canvas and (with `r_smp_backend 0`) runs the
  back end.
* Device lost: `TestCooperativeLevel` returns `D3DERR_DEVICELOST` if the WebGL context was lost; there is no restore.

### Engine-side issues found (outside this directory)

* **`src/gfx_d3d/rb_resource.cpp` `ACTION_RELEASE`** calls `Release` through a raw vtable slot cast to
  `void (__thiscall *)(void *, void *)` with two arguments. On wasm an indirect call whose signature differs from the
  callee's (`ULONG (IUnknown*)`) traps ("function signature mismatch"). Replace with
  `((IUnknown *)action->resource)->Release();`. Done (under `BO1_WEB`, the MSVC path keeps the raw call).
* `RB_HW_ReadOcclusionQuery` (`rb_corona.cpp`) and `RB_CalcSunSpriteSamples` (`rb_sky.cpp`) spin on
  `GetData(...) == S_FALSE` with `Sleep(0)`. Safe: the shim never returns `S_FALSE` for occlusion queries.
* `src/binklib/dx9rad3d.cpp` uses the fixed-function pipeline (`SetFVF`, texture stage states) and
  `D3DXCompileShader`; neither exists here (bink playback must go through the platform's cinematic path).

## Layout

| file | what |
| --- | --- |
| `include/d3d9.h d3d9types.h d3d9caps.h` | D3D9 API (real values/layouts; verified against MinGW-w64) |
| `include/d3dx9.h dxerr.h ddraw.h` | the D3DX / dxerr / DirectDraw subset the engine references |
| `include/d3d9shim_win32.h` | Win32/COM base types (compat layer or standalone fallback) |
| `include/d3d9shim.h` | web-only controls: canvas selector, log level, trace, GL error checks, display size, stats |
| `d3d9_internal.h d3d9_device.h d3d9_templates.h` | implementation classes |
| `d3d9_direct3d.cpp` | `Direct3DCreate9(Ex)`, adapter/modes, caps (from the live context or a probe context), `CheckDeviceFormat` policy |
| `d3d9_device.cpp` | device creation/Reset/lifetime, state setters, resource creation, deferred cross-thread destruction |
| `d3d9_draw.cpp` | GL state cache, program cache, constant upload, vertex attributes, samplers, raster state, draws |
| `d3d9_framebuffer.cpp` | FBO cache, `Clear`, `StretchRect`, `ColorFill`, readbacks, `UpdateSurface/Texture`, Present (+gamma) |
| `d3d9_resources.cpp` | textures (2D/cube/volume), surfaces, volumes, VB/IB, vertex declarations |
| `d3d9_shaders.cpp d3d9_queries.cpp d3d9_swapchain.cpp` | shader objects, queries, implicit swap chain |
| `d3d9_formats.* d3d9_states.* d3d9_vertexdecl.* d3d9_shader_translate.*` | pure logic (unit-tested in node) |
| `d3d9_log.cpp d3dx9_stubs.cpp` | logging, dxerr strings, PIX stubs, IIDs, control API; D3DX subset |
| `d3d9shim.cmake` | the `bo1_d3d9shim` library target |
| `test/` | unit tests, header cross-check, engine compile check, browser smoke tests (see "Tests") |

## Architecture

**Object model.** Each interface has exactly one implementation class deriving (single inheritance) from
`Unknown<Interface>`, so the interface vtable is the object's primary vtable (IUnknown at slots 0..2, as in COM);
downcasts use `static_cast` (no RTTI). Refcounts follow D3D9: every resource holds a device reference (the device is
freed when the last resource and external reference are gone); texture sub-surfaces and volumes forward
`AddRef/Release` to their texture; the back buffer forwards to the device-owned swap chain; binding an object
(`SetTexture`, `SetStreamSource`, ...) takes no reference, and destroying a bound object unbinds it.
`GetSurfaceLevel` returns the same surface object every time (the engine compares surface pointers).

**Render targets are rendered top-down.** Every color target is a GL texture (render target textures, standalone
render targets, offscreen plain surfaces and the back buffer); depth-stencil surfaces are renderbuffers
(`DEPTH24_STENCIL8` for D24S8/D24X8/D15S1, `DEPTH_COMPONENT16` for D16). The vertex shader negates clip-space Y, so
GL row 0 is the D3D top row in every target: D3D viewport/scissor/clear/StretchRect rectangles are GL window
rectangles, render-target textures sample with the same orientation as uploaded textures, and readbacks need no flip.
Only `Present` flips (blit from the back-buffer FBO to the canvas). Winding flips with Y, so with `glFrontFace(GL_CCW)`
a D3D clockwise triangle is a GL front face: `D3DCULL_CCW` -> cull `GL_BACK`, `D3DCULL_CW` -> `GL_FRONT`, the two-sided
stencil `CCW_*` ops are the GL back-face ops and `VFACE`/`gl_FrontFacing` agree.

**Clip space.** Every translated vertex shader ends with `y *= -1`, the D3D9 half-pixel offset
(`xy += (1/W, 1/H) * 63/64 * w`, Wine's trick) and `z = 2z - w` (D3D [0,w] -> GL [-w,w]); `glDepthRangef(MinZ,MaxZ)`
gives the same window depth as D3D, so `D3DRS_SLOPESCALEDEPTHBIAS` maps 1:1 to the polygon-offset factor and
`D3DRS_DEPTHBIAS` (normalized depth) to `units = bias * 2^depthBits` of the bound depth buffer.

**State.** D3D state is stored as-is (`SetRenderState` etc. never touch GL) and translated at draw time through a GL
state cache (redundant calls are skipped). Blend (incl. separate alpha, `BOTHSRCALPHA`, blend factor), depth, stencil
(incl. two-sided), color write mask, cull, scissor, depth bias -> GL state; alpha test -> a `discard` in the pixel
shader driven by a uniform (8-bit compare like D3D9); sampler states -> cached GL sampler objects (filter clamped to
`NEAREST` for non-filterable formats such as R32F without `OES_texture_float_linear`; `MAXMIPLEVEL` -> `MIN_LOD`).

**Formats** (`d3d9_formats.cpp`): A8R8G8B8 -> RGBA8 (BGRA swizzled on upload, back on readback); X8R8G8B8 -> RGB8 (so
sampled/blended alpha is 1 as in D3D); A8B8G8R8/X8B8G8R8 -> RGBA8; R8G8B8 -> RGB8; R5G6B5 -> RGB565 (same bits);
A1R5G5B5/X1R5G5B5 -> RGB5_A1 and A4R4G4B4/X4R4G4B4 -> RGBA4 (bit rotations); A2R10G10B10/A2B10G10R10 -> RGB10_A2;
L8/A8/A8L8 -> unsized LUMINANCE/ALPHA/LUMINANCE_ALPHA (WebGL2 has no texture swizzle); V8U8/Q8W8V8U8 -> SNORM;
DXT1/2/3/4/5 -> `WEBGL_compressed_texture_s3tc` (software decode to RGBA8 when the extension is missing, for volume
textures, and for level-0 sizes that are not multiples of 4); A16B16G16R16/G16R16 -> `EXT_texture_norm16` if present,
else RGBA16F/RG16F with conversion; R16F/G16R16F/A16B16G16R16F/R32F/G32R32F/A32B32G32R32F native (renderable with
`EXT_color_buffer_float`). Vendor FOURCCs (INTZ, RESZ, NULL, ATOC, SSAA, DF16/24, RAWZ) and sampleable depth textures
are reported unsupported, so the engine takes its fallbacks (R32F float-Z, R32F shadow maps, no RESZ/ATOC/SSAA).
D24FS8 is reported unsupported so the engine picks D24S8 (exact depth-bias mapping).

**Resources and uploads.** Textures keep a CPU copy per level/face in D3D layout (D3D9 managed-pool semantics; needed
for partial locks such as the model-lighting volume); `UnlockRect` records a dirty box; the next use on the device
thread converts and uploads only the dirty region (`glTexStorage*` + `glTex(Compressed)SubImage*`). Compressed managed
textures (the bulk of game data) drop their CPU copy after upload. VB/IB keep a CPU shadow; `Unlock` records the dirty
byte range; draws upload it with `glBufferSubData` (DISCARD/NOOVERWRITE ring buffers upload just what was written).
`DrawPrimitiveUP`/`DrawIndexedPrimitiveUP` stream through internal buffers and unset stream 0 / indices afterwards like
D3D9. `DrawIndexedPrimitive`'s `BaseVertexIndex` is applied through attribute offsets (WebGL2 has no base vertex).

**Vertex input.** `linkDeclaration` matches shader inputs to declaration elements by (usage, usage index) like D3D9
(POSITIONT satisfies POSITION); unmatched inputs read the GL generic default (0,0,0,1). D3DCOLOR elements are BGRA in
memory and GL cannot swizzle attributes, so the vertex shader is re-translated with MojoShader's input swizzle (`.zyxw`)
for exactly the inputs the current declaration feeds from D3DCOLOR - one program per (VS, PS, bgra-mask), cached.

**Shaders** (`d3d9_shader_translate.cpp`): MojoShader's `glsles3` profile (GLSL ES 3.00) with two small local
patches (predicated instructions; the pixel-shader predicate register; see `third_party/mojoshader/VENDOR.md`).
Before parsing, predicated instructions are rewritten from D3D's token order (destination, predicate, sources - as
Wine/vkd3d read real fxc output) to the order MojoShader expects. After translation:
precision forced to highp (plus explicit `sampler3D` precision, which GLSL ES 3.00 lacks), `main` renamed and wrapped
(vertex: the clip-space fixups; pixel: alpha test), vPos made D3D9-style (integer pixel centers), and dummy outputs added
to the vertex shader for varyings the pixel shader reads but it does not write (D3D tolerates that, GLSL ES linking
does not). FOG (and PSIZE n>0) vertex outputs, which MojoShader writes as scalars, are routed through a float local
into a vec4 varying. Shaders that use relative constant addressing but carry no CTAB are re-parsed with a synthetic CTAB that
declares c0..c255 (c0..c223 for pixel shaders) as one array. Uniforms are uploaded as MojoShader's packed arrays from the
D3D constant register files whenever a register changed since that program's last upload (version counters).
Translation happens at `Create*Shader`; GL compile + link happen lazily at first draw with a given pair (shader
warming in the engine triggers them early). A shader that fails translation still yields an object (`S_OK`, error
logged once); draws using it are skipped. GLSL ES 1.00 (`glsles`) was rejected: no `textureLod` in fragment shaders
without an extension, no `textureGrad` (texldd), no `sampler3D`.

**Queries.** EVENT: always signaled (WebGL executes in order and the engine only uses fences for CPU/GPU throttling).
OCCLUSION: `ANY_SAMPLES_PASSED`; `GetData` reports `0` or a constant (`d3d9shim_set_occlusion_visible_count`, default
65536) once the result is available and the previous value before that - never `S_FALSE`. Corona/sun/superflare
visibility is a ratio of two queries, so it comes out as 0 or 1. TIMESTAMP*: CPU clock (profiling only). Others:
`D3DERR_NOTAVAILABLE`.

**Readbacks.** `GetRenderTargetData`/`GetFrontBufferData` use synchronous `glReadPixels` (RGBA8 or float path, then
converted to the D3D format): a GPU pipeline stall, acceptable for screenshots, reflection probes and the dev mip path.
`GetFrontBufferData` returns the back buffer (there is no readable front buffer on the web).

**Present and gamma.** `Present` blits the back buffer to the canvas with a vertical flip. When the gamma ramp
(`SetGammaRamp`, the engine's brightness) is not the identity, it instead draws the back buffer through a 256-entry LUT.

**Diagnostics.** `D3D9SHIM_LOG=0..3` (or `d3d9shim_set_log_level`), `D3D9SHIM_TRACE=1` (every device call, like the
engine's `r_logFile`), `D3D9SHIM_GLCHECK=1` (`glGetError` after each draw/clear/copy/present, logged with the D3D call),
`D3D9SHIM_SHADER_DUMP=<dir>` (+ `D3D9SHIM_SHADER_DUMP_ALL=1`) or `d3d9shim_set_shader_dump()` (writes the bytecode,
GLSL and errors of failing - or all - shaders, for the corpus tools below), `d3d9shim_get_stats` (draws, programs
linked/failed, upload bytes). Unsupported features fail softly: logged once, then ignored or `D3DERR_NOTAVAILABLE`.

## Coverage

| interface / method | status |
| --- | --- |
| `Direct3DCreate9`, `Direct3DCreate9Ex` | done (9Ex object also implements `CreateDeviceEx` for the headless path) |
| `IDirect3D9` adapter/modes/display mode | done (1 adapter, VendorId 0, modes up to `d3d9shim_set_display_size`) |
| `IDirect3D9::GetDeviceCaps` / device `GetDeviceCaps` | done: SM 3.0, 1 RT, real texture limits/anisotropy; passes every `R_CheckDxCaps` check (unit-tested) |
| `CheckDeviceFormat`, `CheckDepthStencilMatch`, `CheckDeviceType`, `CheckDeviceFormatConversion` | done (policy above) |
| `CheckDeviceMultiSampleType` | NONE only (MSAA not implemented) |
| `CreateDevice`, `Reset`, `TestCooperativeLevel`, `Present`, `GetSwapChain`, `GetBackBuffer` | done |
| `SetGammaRamp` / `GetGammaRamp` | done (LUT at present) |
| `CreateTexture`, `CreateCubeTexture`, `CreateVolumeTexture` | done (all pools; RT/DS usage; DYNAMIC; AUTOGENMIPMAP) |
| `CreateVertexBuffer`, `CreateIndexBuffer` (16/32-bit) | done |
| `CreateRenderTarget`, `CreateDepthStencilSurface`, `CreateOffscreenPlainSurface` | done (MSAA ignored; lockable RTs not lockable) |
| `SetRenderTarget` / `GetRenderTarget` | RT 0 only (`NumSimultaneousRTs` = 1); resets viewport + scissor like D3D9 |
| `SetDepthStencilSurface` / `Get...` | done (DS may be larger than the RT) |
| `Clear` | done (rect list; honors viewport + scissor; ignores write masks like D3D9) |
| `StretchRect` | done (color with point/linear scaling; depth-stencil same size) |
| `ColorFill`, `UpdateSurface`, `UpdateTexture` | done |
| `GetRenderTargetData`, `GetFrontBufferData` | done (synchronous; front buffer = back buffer) |
| `BeginScene` / `EndScene` | done |
| `SetViewport` / `GetViewport`, `SetScissorRect` / `Get...` | done |
| `SetRenderState` / `GetRenderState` | all states stored; translated: Z*, ALPHATEST*, *BLEND*, BLENDOP*, SEPARATEALPHABLENDENABLE, BLENDFACTOR, CULLMODE, STENCIL*, TWOSIDEDSTENCILMODE, CCW_STENCIL*, COLORWRITEENABLE, SCISSORTESTENABLE, DEPTHBIAS, SLOPESCALEDEPTHBIAS. Ignored (logged once where relevant): FILLMODE wireframe, SRGBWRITEENABLE, CLIPPLANEENABLE, ADAPTIVETESS_* (ATOC/SSAA hack), WRAPn, fixed-function/fog/point states, MULTISAMPLE* |
| `SetSamplerState` / `Get...` | ADDRESS U/V/W (BORDER -> clamp, MIRRORONCE -> mirror), MIN/MAG/MIPFILTER, MAXANISOTROPY, MAXMIPLEVEL. Ignored: MIPMAPLODBIAS, SRGBTEXTURE, BORDERCOLOR |
| `SetTexture` / `GetTexture` | s0..s15; vertex/displacement samplers stored only |
| `DrawPrimitive`, `DrawIndexedPrimitive`, `DrawPrimitiveUP`, `DrawIndexedPrimitiveUP` | done (all primitive types) |
| `CreateVertexDeclaration` / `Set` / `Get` | done (all D3DDECLTYPEs; UDEC3/DEC3N approximate w) |
| `SetStreamSource` / `Get`, `SetIndices` / `Get` | done; `SetStreamSourceFreq` (instancing) not supported |
| `Create/Set/Get Vertex/PixelShader`, `Set/Get ...ShaderConstantF/I/B` | done (vs/ps 2.0 and 3.0 via MojoShader) |
| `CreateQuery` + `IDirect3DQuery9` | EVENT, OCCLUSION, TIMESTAMP, TIMESTAMPDISJOINT, TIMESTAMPFREQ |
| Texture `LockRect`/`UnlockRect`/`LockBox`, `GetSurfaceLevel`/`GetCubeMapSurface`/`GetVolumeLevel`, `GetLevelDesc`, `GetLevelCount`, `SetLOD`, `GenerateMipSubLevels`, `AddDirtyRect`, `PreLoad` | done |
| Surface `LockRect` (non-RT), `GetDesc`, `GetContainer`; `GetDC` | done; `GetDC` -> `D3DERR_INVALIDCALL` |
| VB/IB `Lock` (DISCARD/NOOVERWRITE/READONLY), `GetDesc` | done |
| Transforms, materials, lights, clip planes, texture stage states, `SetFVF`, palettes, state blocks, `ProcessVertices`, patches | stored or stubbed: the fixed-function pipeline is not implemented (the engine's renderer does not use it) |
| D3DX: `D3DXCreateBuffer`, `D3DXGetShaderConstantTable`, `D3DXGetShaderInput/OutputSemantics`, `D3DXGetShaderSize/Version`, `D3DXSaveSurfaceToFileA` | done (save: TGA/BMP of A8R8G8B8/X8R8G8B8 surfaces) |
| `D3DXCompileShader` | `E_NOTIMPL` (no HLSL compiler; the runtime-material compiler is a dev path) |
| `DXGetErrorStringA/DescriptionA`, `D3DPERF_*`, IIDs | done |

## Tests

| command | what |
| --- | --- |
| `src/web/d3d9/test/run_unit_tests.sh` | builds `test/unit_tests.cpp` with emcc and runs it under node: caps vs the engine's `R_CheckDxCaps` tables, format tables and pixel conversions (BGRA, X8R8G8B8, 1555/4444/565, unorm16->half, S3TC decode, readback conversions), render-state/sampler mapping, depth bias, vertex declaration linking (engine-like decls, D3DCOLOR masks), translation of hand-assembled SM3 shaders (`test/shader_asm.h`, and `test/shader_corpus.h`: loops/rep/if/ifc/break, predication, texldd/dsx/dsy, texldb/texldp/texldl, texkill, oDepth, relative addressing through aL and a0, FOG/PSIZE outputs, TEXCOORD13, DEPTH vertex input, uniform loop counts) and of the real fxc-compiled mjpeg shaders from `src/mjpeg/yuv.cpp`, synthetic CTAB, predicate token reordering, `CheckDeviceFormat` policy, D3DX helpers, error strings. Then validates every emitted GLSL ES 3.00 shader with `glslangValidator` (compile + VS/PS link) and cross-checks header constants/struct sizes against MinGW-w64 (`test/check_header_values.py`). Result: 343/343, 22/22, 1161 constants + 91 structs. |
| `src/web/d3d9/test/fetch_wine_corpus.sh` then `CORPUS=build/web-d3d9/corpus_wine src/web/d3d9/test/run_unit_tests.sh` | downloads Wine's d3d9 conformance tests (data only, into `build/`), extracts their 80 SM2/SM3 shader token arrays (`test/extract_shader_arrays.py`) and translates + glslang-compiles them (`unit_tests.js --corpus`). Result: 79 translated, 79/79 compiled. `CORPUS=` also accepts a shader dump directory. |
| `CORPUS=<dir> src/web/d3d9/test/build_corpus_check.sh` | `build/web-d3d9/corpus_check.html`: compiles + links every translated corpus shader with the browser's WebGL2 compiler (run like the smoke tests). Result: 79/79 on ANGLE Metal and SwiftShader. |
| `python3 src/web/d3d9/test/check_engine_compile.py` | syntax-checks `src/gfx_d3d` (+ the shim) with the exact flags of `build/web/compile_commands.json` (`cmake --preset web`): 144 files, 0 errors. |
| `src/web/d3d9/test/build_smoke.sh` / `THREADED=1 src/web/d3d9/test/build_smoke.sh` | builds `build/web-d3d9/smoke.html` (single-threaded) / `smoke_mt.html` (`-pthread -sPROXY_TO_PTHREAD -sOFFSCREENCANVAS_SUPPORT=1`). |
| `python3 src/web/d3d9/test/serve_coi.py 8000` then open `http://127.0.0.1:8000/smoke.html` (or `/smoke_mt.html`) | manual run in any browser (the server sends COOP/COEP for the pthread build; `python3 -m http.server` is enough for `smoke.html`). PASS/FAIL lines on the page and console, verdict in the title and `window.D3D9_SMOKE`; the canvas ends green (pass) or red (fail). |
| `src/web/d3d9/test/run_smoke_headless.sh [swiftshader metal ...]` | both smoke pages in headless Chromium via DevTools (`test/run_smoke_cdp.mjs`, node >= 22; uses Playwright's cached chrome-headless-shell or `$CHROME`). Result: 86/86 and 88/88 on SwiftShader and on ANGLE Metal. |

The smoke test drives the shim only through D3D9 calls: device creation on the canvas, VB/IB/declarations (incl.
D3DCOLOR), shaders assembled from tokens, A8R8G8B8 / X8R8G8B8 / 1555 / 4444 / 565 / A16B16G16R16 / A8 / A8L8 / L8 /
DXT1 (hardware and software decode) / DXT5 / cube / volume / mipmapped textures, render-to-texture + StretchRect +
sampling the RT, depth (incl. D3D near/far clipping), stencil, alpha test, blending, culling, scissor (draw and
Clear), UP draws, relative constant addressing (synthetic CTAB; def precedence inside relative arrays), event/occlusion queries (results after yielding),
R32F render target + float readback, Present (flip check on the canvas) + gamma ramp, Reset, full release
(device refcount 0) and re-creation on the same canvas, and (pthread build) locks from a worker thread - with
`D3D9SHIM_GLCHECK` on, so any GL error would be reported.

## Known gaps and risks

* **Shader translation of real game shaders is unproven**: the corpus is hand-assembled shaders, the two fxc-compiled
  mjpeg shaders and Wine's 80 d3d9 test shaders (there are no fastfiles in the repo). The corpus already found and
  fixed five MojoShader problems (predicate token order, predicated writes, the pixel-shader predicate register,
  FOG outputs, non-constant loop bounds). Shaders MojoShader rejects (e.g. reading an uninitialized temp, which fxc
  never emits) are logged and their draws skipped, not fatal. Running the real fastfile shaders through the corpus
  tools is plan item A (`D3D9SHIM_SHADER_DUMP_ALL` collects them from a running engine).
* **Occlusion counts** are binary (WebGL2 has no sample counts) and lag at least one yielded frame.
* **16-bit unorm render targets** (A16B16G16R16 bloom) become RGBA16F unless `EXT_texture_norm16` exists: values are
  not clamped to [0,1] when blending, unlike D3D.
* **Sampleable depth / hardware shadow maps / INTZ / RESZ** are reported unsupported: the engine renders float-Z and
  shadow depth into R32F color targets (one extra pass; `sm_enable` sun shadows take the fallback path). Needs
  `EXT_color_buffer_float` (all desktop browsers).
* **MSAA** is not implemented (`r_aaSamples` falls back to none). **sRGB** read/write is not implemented (the
  engine does not use it). **Wireframe** fill mode is ignored. **Instancing** (`SetStreamSourceFreq`), user clip
  planes, vertex texture fetch and the fixed-function pipeline are not implemented (unused by the engine renderer).
* **Readback stalls**: `GetRenderTargetData`/`GetFrontBufferData` wait for the GPU (rare paths). Screenshots in JPG
  (`D3DXSaveSurfaceToFileA` with `D3DXIFF_JPG`, the engine default) return `E_NOTIMPL`; TGA works.
* **Thread affinity / frame pacing**: see Integration; a render thread that never yields shows nothing.
* **Uniform limits**: a vertex shader with relative addressing and no CTAB uses a 256-vec4 array plus the fixup
  uniform (WebGL2's guaranteed minimum is 256; every desktop implementation offers >= 1024).
* **Memory**: CPU copies of all uncompressed/dynamic textures and all VB/IB live in the wasm heap (like D3D9's
  managed pool lived in the game's address space); compressed managed textures drop theirs after upload.
* **Context loss** is reported as `D3DERR_DEVICELOST` but never restored. `GetAvailableTextureMem` is a constant
  (1.5 GB).

## Plan for the remaining work (parallelizable)

A. **Real-shader validation** (independent): run the engine (browser or the `web-node` build) with
   `D3D9SHIM_SHADER_DUMP=<dir> D3D9SHIM_SHADER_DUMP_ALL=1` while loading every zombies map, then
   `CORPUS=<dir> run_unit_tests.sh` and `build_corpus_check.sh`; fix failures in the post-processing or with documented
   MojoShader patches (`third_party/mojoshader/VENDOR.md`). Linking per technique pass (VS+PS pairs) happens naturally
   in the engine run (link failures are dumped too).
B. **Engine bring-up in the browser** (needs the platform's render-thread yielding + canvas transfer): run with
   `D3D9SHIM_GLCHECK=1 D3D9SHIM_LOG=2`, fix what shows up, compare frames against Windows captures.
C. **Performance** (after B): profile draw overhead; upload only dirty constant ranges or move constants to UBOs;
   batch dynamic VB uploads per frame; precompile programs during load (`KHR_parallel_shader_compile`); cache sampler
   decisions per state block.
D. **Shadow maps / INTZ**: sampleable D24S8 textures + `sampler2DShadow` (compare mode) for the samplers the engine
   uses with hardware shadow maps (needs shader-side detection of shadow samplers in the translation); report D24S8
   textures supported; optionally INTZ for float-Z.
E. **MSAA**: multisampled renderbuffers for the back buffer and its depth buffer, resolve on StretchRect/Present
   (resolve blits cannot flip: resolve first, then flip).
F. **Context loss / restore**: recreate GL objects from CPU copies on `webglcontextrestored`; textures that dropped
   their copy need the engine's `Reset` path (report DEVICENOTRESET).
G. **Screenshots**: JPG in `D3DXSaveSurfaceToFileA` (stb_image_write) or route screenshots to the page.
H. **Small features as needed**: wireframe (line index buffer), instancing via `glVertexAttribDivisor`, user clip
   planes via shader output, minimal fixed-function for any remaining FVF users.
