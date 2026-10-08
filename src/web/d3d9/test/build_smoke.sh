#!/bin/bash
# build_smoke.sh - build the D3D9 shim browser smoke test (single-threaded, no COOP/COEP needed).
#
#   src/web/d3d9/test/build_smoke.sh
#   cd build/web-d3d9 && python3 -m http.server 8000      # or: bunx serve build/web-d3d9
#   open http://localhost:8000/smoke.html                  # PASS/FAIL lines on the page + console; title has the tally
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../../.." && pwd)"
OUT="$ROOT/build/web-d3d9"
mkdir -p "$OUT/smoke-obj" "$OUT/smoke_mt-obj"
if ! command -v em++ >/dev/null 2>&1; then
    export EMSDK_PYTHON=${EMSDK_PYTHON:-/opt/homebrew/bin/python3.12} EMSDK_OS=${EMSDK_OS:-macos}
    # shellcheck disable=SC1090
    source ~/emsdk/emsdk_env.sh >/dev/null 2>&1
fi
MOJO="$ROOT/third_party/mojoshader"
DEFS=(-DSUPPORT_PROFILE_D3D=0 -DSUPPORT_PROFILE_BYTECODE=0 -DSUPPORT_PROFILE_HLSL=0 -DSUPPORT_PROFILE_GLSL120=0
      -DSUPPORT_PROFILE_ARB1=0 -DSUPPORT_PROFILE_ARB1_NV=0 -DSUPPORT_PROFILE_METAL=0 -DSUPPORT_PROFILE_SPIRV=0
      -DSUPPORT_PROFILE_GLSPIRV=0 -DMOJOSHADER_NO_VERSION_INCLUDE)
INC=(-I"$ROOT/src/web/d3d9/include" -I"$ROOT/src/web/d3d9" -I"$MOJO")
OPT=${OPT:--O2}
# THREADED=1: the engine's configuration (-pthread, PROXY_TO_PTHREAD, canvas transferred to main()'s pthread as an
# OffscreenCanvas) -> smoke_mt.html. Needs COOP/COEP headers: serve with src/web/d3d9/test/serve_coi.py.
MT=()
NAME=smoke
if [ "${THREADED:-0}" = 1 ]; then
    MT=(-pthread -DSMOKE_THREADED)
    NAME=smoke_mt
fi
# rebuild MojoShader when any of its sources or headers changed (local patches live in the headers too)
MOJO_NEWEST=$(ls -t "$MOJO"/*.c "$MOJO"/*.h "$MOJO"/profiles/*.c "$MOJO"/profiles/*.h | head -1)
objs=()
for f in "$MOJO/mojoshader.c" "$MOJO/mojoshader_common.c" "$MOJO/profiles/mojoshader_profile_common.c" \
         "$MOJO/profiles/mojoshader_profile_glsl.c"; do
    o="$OUT/$NAME-obj/$(basename "$f" .c).o"
    [ "$o" -nt "$MOJO_NEWEST" ] || emcc $OPT -w ${MT[@]+"${MT[@]}"} "${DEFS[@]}" -c "$f" -o "$o"
    objs+=("$o")
done
for f in "$ROOT"/src/web/d3d9/d3d9_*.cpp "$ROOT"/src/web/d3d9/d3dx9_*.cpp "$ROOT/src/web/d3d9/test/smoke.cpp"; do
    o="$OUT/$NAME-obj/$(basename "$f" .cpp).o"
    em++ -std=c++17 $OPT -g -Wall -Wno-unused-parameter ${MT[@]+"${MT[@]}"} "${DEFS[@]}" "${INC[@]}" -c "$f" -o "$o"
    objs+=("$o")
done
LINK=(-sMAX_WEBGL_VERSION=2 -sMIN_WEBGL_VERSION=2 -sALLOW_MEMORY_GROWTH=1)
if [ "$NAME" = smoke_mt ]; then
    LINK+=(-pthread -sPROXY_TO_PTHREAD -sOFFSCREENCANVAS_SUPPORT=1 -sPTHREAD_POOL_SIZE=4 -sENVIRONMENT=web,worker)
else
    LINK+=(-sENVIRONMENT=web)
fi
em++ $OPT -g "${objs[@]}" -o "$OUT/$NAME.html" --shell-file "$ROOT/src/web/d3d9/test/smoke_shell.html" "${LINK[@]}"
echo "built $OUT/$NAME.html - serve $OUT (python3 -m http.server; smoke_mt needs serve_coi.py) and open /$NAME.html"
