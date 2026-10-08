# cmake/web.cmake - the browser (Emscripten / WebAssembly) build of BO1Zombies.
#
# Included by CMakeLists.txt when the Emscripten toolchain is active (`cmake --preset web`, see CMakePresets.json and
# docs/web-port.md). cmake_files.cmake stays the single list of engine sources: the target it creates is reused here,
# minus the folders the web build replaces or leaves out (BO1_WEB_EXCLUDE below), plus src/web/.
#
# Output: web/engine/bo1.js + bo1.wasm + build.json (docs/web-engine-interface.md section 1).

set(WEB_DIR "${SRC_DIR}/web")

option(BO1_WEB_NODE "Headless Node.js test build: NODERAWFS (host files) instead of OPFS, no canvas" OFF)
option(BO1_WEB_ALLOW_UNDEFINED "Link with undefined symbols left as imports that abort when called (bring-up)" ON)
set(BO1_WEB_MEMORY_MB 1536 CACHE STRING "wasm memory in MiB (fixed size: no memory growth; PMem alone takes 328 MiB, +256 with bo1_mod_zones)")
if (BO1_WEB_NODE)
    set(_bo1_default_out "${CMAKE_BINARY_DIR}/bin")
else()
    set(_bo1_default_out "${CMAKE_CURRENT_SOURCE_DIR}/web/engine")
endif()
set(BO1_WEB_OUTPUT_DIR "${_bo1_default_out}" CACHE PATH "Where bo1.js / bo1.wasm / build.json are written")

# The D3D9-over-WebGL2 shim (src/web/d3d9/) provides d3d9.h and friends. Until its headers exist, the renderer and
# everything else that needs them is left out of the build (and its symbols stay undefined at link time).
if (EXISTS "${WEB_DIR}/d3d9/include/d3d9.h")
    set(BO1_WEB_HAVE_D3D9 ON)
else()
    set(BO1_WEB_HAVE_D3D9 OFF)
endif()
option(BO1_WEB_RENDERER "Compile src/gfx_d3d against the D3D9 shim (needs src/web/d3d9/include/d3d9.h)" ${BO1_WEB_HAVE_D3D9})

# ----- Sources -----
# Regexes matched against the absolute source paths of cmake_files.cmake. Each one is documented in docs/web-port.md
# ("Excluded from the web build").
set(BO1_WEB_EXCLUDE
    "/src/win32/"                          # Windows platform layer -> src/web/ (except BO1_WEB_SHARED_WIN32 below)
    "/src/tracy/"                          # Tracy profiler client (Windows/posix sockets, not used in release)
    "/src/binklib/"                        # Bink video SDK (no wasm build); cinematics are stubbed (web_stubs.cpp)
    "/src/mjpeg/"                          # movie_start capture (AVI/MJPEG writer)
    "/src/vpx/"                            # VP8 capture (libvpx is a prebuilt Windows lib)
    "/src/nvapi/"                          # NVIDIA driver API
    "/src/CubeMapGenLib/"                  # tools-only cube map filtering (wide-char Win32 UI code); r_screenshot
                                           # only constructs CCubeMapProcessor: web_stubs.cpp
    "/src/groupvoice/"                     # voice chat (DirectSound capture + speex); sv_voice is 0
    "/src/monkey/"                         # test automation over ws2_32
    "/src/sound/snd_driver_xaudio2[^/]*\\.cpp$"  # XAudio2 driver -> web_sound.cpp (SD_* null driver)
)
if (NOT BO1_WEB_RENDERER)
    list(APPEND BO1_WEB_EXCLUDE
        "/src/gfx_d3d/"                    # renderer: needs the D3D9 shim headers
    )
endif()

# Platform files of src/win32/ with no Windows-only code: compiled as they are (the compat layer provides the Win32
# calls they make). The rest of src/win32/ is replaced by src/web/ (docs/web-port.md, "Platform layer").
set(BO1_WEB_SHARED_WIN32
    win_common.cpp          # Sys_ListFiles (_findfirst), Sys_DefaultInstallPath, critical sections
    win_configure.cpp       # system information for autoconfigure
    win_content.cpp
    win_gamerprofile.cpp
    win_localize.cpp        # localization.txt -> language
    win_splash.cpp
    win_steam.cpp           # "no Steam" startup path: the Steamworks C API is web_steamapi.cpp (Steam not running)
    win_stream.cpp          # stream thread
    win_tasks.cpp
    win_workercmds.cpp      # TL job queue workers
)

get_target_property(_bo1_all_sources ${BIN_NAME} SOURCES)
set(BO1_ENGINE_SOURCES ${_bo1_all_sources})
foreach (_re IN LISTS BO1_WEB_EXCLUDE)
    list(FILTER BO1_ENGINE_SOURCES EXCLUDE REGEX "${_re}")
endforeach()
foreach (_f IN LISTS BO1_WEB_SHARED_WIN32)
    list(APPEND BO1_ENGINE_SOURCES "${SRC_DIR}/win32/${_f}")
endforeach()
list(FILTER BO1_ENGINE_SOURCES INCLUDE REGEX "\\.(c|cpp)$")

# src/web/ (platform layer) and src/web/compat/ (Win32 / MSVC CRT emulation). src/web/d3d9/ belongs to the D3D9 shim
# (below).
file(GLOB BO1_WEB_PLATFORM_SOURCES CONFIGURE_DEPENDS
    "${WEB_DIR}/*.cpp"
    "${WEB_DIR}/compat/*.cpp"
)
# The D3D9 shim is its own static library (src/web/d3d9/d3d9shim.cmake, owned by the shim; see its README). It is
# compiled against the same <windows.h> (src/web/compat) and with the same wasm features (threads, exceptions, SIMD)
# as the engine, so both sides agree on the Win32 / COM types and the objects can be linked together.
if (BO1_WEB_RENDERER AND EXISTS "${WEB_DIR}/d3d9/d3d9shim.cmake")
    include("${WEB_DIR}/d3d9/d3d9shim.cmake")
    target_include_directories(bo1_d3d9shim BEFORE PRIVATE "${WEB_DIR}/compat")
    target_compile_definitions(bo1_d3d9shim PRIVATE BO1_WEB NOMINMAX $<$<NOT:$<CONFIG:Debug>>:NDEBUG>)
    target_compile_options(bo1_d3d9shim PRIVATE -pthread -fwasm-exceptions -sSUPPORT_LONGJMP=wasm -msimd128 -msse -msse2
        -fno-strict-aliasing -fms-extensions -fdeclspec -Wno-ignored-attributes -Wno-microsoft)
    target_link_libraries(${BIN_NAME} PRIVATE bo1_d3d9shim)
endif()

set_property(TARGET ${BIN_NAME} PROPERTY SOURCES ${BO1_ENGINE_SOURCES} ${BO1_WEB_PLATFORM_SOURCES})

# Decompiled code: no warnings (there are hundreds of thousands). src/web/ keeps the default warnings, minus the ones
# the engine headers it includes would raise.
set_source_files_properties(${BO1_ENGINE_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")
set_source_files_properties(${BO1_WEB_PLATFORM_SOURCES} PROPERTIES COMPILE_OPTIONS
    "-Wno-inconsistent-missing-override;-Wno-missing-declarations;-Wno-invalid-offsetof;-Wno-deprecated-declarations;-Wno-dangling-else;-Wno-switch;-Wno-nonportable-include-path;-Wno-deprecated-volatile;-Wno-array-bounds;-Wno-nontrivial-memcall;-Wno-void-pointer-to-int-cast")

# ----- Preprocessor -----
# WIN32 / _WIN32 are NOT defined: code that needs a Win32 path for the web build says `#if defined(WIN32) ||
# defined(BO1_WEB)` explicitly. BO1_WEB marks every web-only change in shared code.
target_compile_definitions(${BIN_NAME} PRIVATE
    BO1_WEB
    BO1_MP
    _CONSOLE
    _CRT_SECURE_NO_WARNINGS
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
)
if (BO1_WEB_NODE)
    target_compile_definitions(${BIN_NAME} PRIVATE BO1_WEB_NODE)
endif()

# ----- Include directories -----
# compat/ first: windows.h, winsock2.h, intrin.h, io.h, direct.h, ... (src/web/compat/README in docs/web-port.md).
target_include_directories(${BIN_NAME} BEFORE PRIVATE "${WEB_DIR}/compat")
if (BO1_WEB_HAVE_D3D9)
    target_include_directories(${BIN_NAME} PRIVATE "${WEB_DIR}/d3d9/include")
endif()
target_include_directories(${BIN_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${SRC_DIR}"
    "${SRC_DIR}/libs"
    "${SRC_DIR}/libs/libtomcrypt-1.17/src/headers"
    "${SRC_DIR}/libs/libtommath-1.0"
    "${WEB_DIR}"
    # Windows header names the engine spells in another case (Windows.h, XInput.h, ...): forwarders for
    # case-sensitive file systems; never reached on case-insensitive ones (the compat/ name matches first)
    "${WEB_DIR}/compat/case"
)

# ----- Compiler flags -----
set(BO1_WEB_COMMON_FLAGS
    -pthread
    -fwasm-exceptions                      # wasm EH; setjmp/longjmp (Com_Error) use it too (SUPPORT_LONGJMP=wasm)
    -sSUPPORT_LONGJMP=wasm
    -msimd128 -msse -msse2                 # <xmmintrin.h>/<emmintrin.h> map onto wasm SIMD
    -fms-extensions -fdeclspec             # __declspec, __int64, __forceinline, anonymous structs, ...
    -mms-bitfields                         # MSVC bit-field layout (static_assert(sizeof) checks rely on it)
    -fno-strict-aliasing                   # IDA-style punning (LODWORD, *(float *)&i, ...) everywhere
    -fwrapv                                # MSVC-era code expects wrapping signed arithmetic
    -fno-delete-null-pointer-checks
    -include "${WEB_DIR}/compat/bo1_web_prelude.h"   # MSVC CRT extensions and intrinsics for every TU
    # errors by default in clang that MSVC accepts; demoted (and silenced by -w in engine files)
    -Wno-c++11-narrowing
    -Wno-register
    -Wno-reserved-user-defined-literal
    -Wno-dynamic-exception-spec
    -Wno-incompatible-pointer-types
    -Wno-incompatible-function-pointer-types
    -Wno-int-conversion
    -Wno-implicit-function-declaration
    -Wno-implicit-int
    -Wno-return-mismatch
    -Wno-address-of-temporary
    -Wno-invalid-noreturn
    -Wno-ignored-attributes                # __stdcall / __fastcall / __thiscall have no meaning on wasm
    -Wno-microsoft
    -Wno-pragma-pack
    -Wno-unknown-pragmas
    -Wno-parentheses                       # cg_draw_reticles.cpp: a chained comparison MSVC accepts (docs/web-port.md)
)
target_compile_options(${BIN_NAME} PRIVATE ${BO1_WEB_COMMON_FLAGS})

set(CMAKE_C_FLAGS_RELEASE "-O2")
set(CMAKE_CXX_FLAGS_RELEASE "-O2")
set(CMAKE_C_FLAGS_DEBUG "-O1 -g")
set(CMAKE_CXX_FLAGS_DEBUG "-O1 -g")

# ----- Link -----
math(EXPR _bo1_mem_bytes "${BO1_WEB_MEMORY_MB} * 1024 * 1024")
set(BO1_WEB_LINK_FLAGS
    -pthread
    -fwasm-exceptions
    -sSUPPORT_LONGJMP=wasm
    -sPROXY_TO_PTHREAD                     # main() (WinMain's loop) runs in a worker; the page thread stays free
    -sEXIT_RUNTIME=1                       # exit() (quit, Sys_Error) ends every engine thread
    -sWASMFS
    -sINITIAL_MEMORY=${_bo1_mem_bytes}
    -sALLOW_MEMORY_GROWTH=0
    -sSTACK_SIZE=8MB                       # main thread (Windows reserved 1 MB, but IDA frames are large)
    -sDEFAULT_PTHREAD_STACK_SIZE=2MB
    -sPTHREAD_POOL_SIZE=16                 # engine threads (~7, qcommon/threads.cpp) + TL job queue workers + OPFS
    -sPTHREAD_POOL_SIZE_STRICT=0
    -sMALLOC=mimalloc                      # multithreaded allocator (dlmalloc takes one global lock)
    -sABORTING_MALLOC=0
    -sSTACK_OVERFLOW_CHECK=1
    "-sEXPORTED_FUNCTIONS=_main,_bo1_net_get_shared,_bo1_input_gamepads"
    # stringToUTF8OnStack: Emscripten 6.0.11's OPFS backend lists directories with it (_wasmfs_opfs_get_entries) but
    # does not depend on it; when nothing else pulls it in, every readdir under /opfs fails with EIO
    "-sEXPORTED_RUNTIME_METHODS=HEAP8,HEAPU8,HEAP16,HEAPU16,HEAP32,HEAPU32,HEAPF32,UTF8ToString,stringToUTF8,stringToUTF8OnStack,callMain,FS"
    --profiling-funcs                      # function names in stack traces
    # the engine's plain C file calls get the case-insensitive path resolver (src/web/compat/win_file.cpp)
    "-Wl,--wrap=fopen" "-Wl,--wrap=remove" "-Wl,--wrap=rename"
)
if (BO1_WEB_NODE)
    list(APPEND BO1_WEB_LINK_FLAGS
        -sENVIRONMENT=node,worker
        -sNODERAWFS=1                      # the host file system is the engine's file system (fs_b = a local install)
    )
else()
    list(APPEND BO1_WEB_LINK_FLAGS
        -sENVIRONMENT=web,worker
        -sMODULARIZE=1
        -sEXPORT_NAME=createBo1
        -sEXPORT_ES6=1
        -sOFFSCREENCANVAS_SUPPORT=1        # the page's <canvas> is transferred to the engine (D3D9 shim -> WebGL2)
        -sMAX_WEBGL_VERSION=2
        -sMIN_WEBGL_VERSION=2
        -sFULL_ES3=0
    )
endif()
if (BO1_WEB_ALLOW_UNDEFINED)
    list(APPEND BO1_WEB_LINK_FLAGS -sERROR_ON_UNDEFINED_SYMBOLS=0 -sWARN_ON_UNDEFINED_SYMBOLS=1)
endif()
target_link_options(${BIN_NAME} PRIVATE ${BO1_WEB_LINK_FLAGS})
target_link_options(${BIN_NAME} PRIVATE $<$<CONFIG:Debug>:-sASSERTIONS=1> $<$<CONFIG:Debug>:-g>)

set_target_properties(${BIN_NAME} PROPERTIES
    OUTPUT_NAME "bo1"
    SUFFIX ".js"
    RUNTIME_OUTPUT_DIRECTORY "${BO1_WEB_OUTPUT_DIR}"
)

# build.json next to bo1.js: { "version": "<git describe>", "built": "<ISO time>" }
add_custom_command(TARGET ${BIN_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR} -DOUT=${BO1_WEB_OUTPUT_DIR}/build.json
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/web_buildinfo.cmake
    VERBATIM)

# checks of the Win32 emulation (Node build only): cmake --build --preset web-node --target bo1_compat_test
if (BO1_WEB_NODE)
    file(GLOB _bo1_compat_sources CONFIGURE_DEPENDS "${WEB_DIR}/compat/*.cpp")
    add_executable(bo1_compat_test EXCLUDE_FROM_ALL "${WEB_DIR}/test/compat_test.cpp" ${_bo1_compat_sources})
    target_include_directories(bo1_compat_test BEFORE PRIVATE "${WEB_DIR}/compat")
    target_compile_definitions(bo1_compat_test PRIVATE BO1_WEB BO1_WEB_NODE)
    target_compile_options(bo1_compat_test PRIVATE ${BO1_WEB_COMMON_FLAGS})
    target_link_options(bo1_compat_test PRIVATE -pthread -fwasm-exceptions -sSUPPORT_LONGJMP=wasm -sPROXY_TO_PTHREAD
        -sEXIT_RUNTIME=1 -sWASMFS -sNODERAWFS=1 -sENVIRONMENT=node,worker -sINITIAL_MEMORY=64MB
        "-Wl,--wrap=fopen" "-Wl,--wrap=remove" "-Wl,--wrap=rename")
    set_target_properties(bo1_compat_test PROPERTIES SUFFIX ".js" RUNTIME_OUTPUT_DIRECTORY "${BO1_WEB_OUTPUT_DIR}")
endif()

# a quick test page for the browser build (python3 src/web/tools/serve.py, then /build/web/harness.html)
if (NOT BO1_WEB_NODE)
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/web_harness.html" "${CMAKE_BINARY_DIR}/harness.html" COPYONLY)
endif()

message(STATUS "BO1 web build: renderer=${BO1_WEB_RENDERER} node=${BO1_WEB_NODE} memory=${BO1_WEB_MEMORY_MB}MiB -> ${BO1_WEB_OUTPUT_DIR}")
