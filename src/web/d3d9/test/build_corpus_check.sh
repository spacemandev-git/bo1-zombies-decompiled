#!/bin/bash
# build_corpus_check.sh - build build/web-d3d9/corpus_check.html with $CORPUS (a directory of *.bin D3D9 shaders,
# e.g. from fetch_wine_corpus.sh or a d3d9shim shader dump) preloaded at /corpus. The page compiles + links every
# translated shader with the browser's WebGL2 compiler. Run headless:
#   node src/web/d3d9/test/run_smoke_cdp.mjs <chrome> http://127.0.0.1:<port>/corpus_check.html swiftshader
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../../.." && pwd)"
OUT="$ROOT/build/web-d3d9"
CORPUS="${CORPUS:?set CORPUS=<dir of *.bin shaders>}"
if ! command -v em++ >/dev/null 2>&1; then
    export EMSDK_PYTHON=${EMSDK_PYTHON:-/opt/homebrew/bin/python3.12} EMSDK_OS=${EMSDK_OS:-macos}
    # shellcheck disable=SC1090
    source ~/emsdk/emsdk_env.sh >/dev/null 2>&1
fi
MOJO="$ROOT/third_party/mojoshader"
DEFS=(-DSUPPORT_PROFILE_D3D=0 -DSUPPORT_PROFILE_BYTECODE=0 -DSUPPORT_PROFILE_HLSL=0 -DSUPPORT_PROFILE_GLSL120=0
      -DSUPPORT_PROFILE_ARB1=0 -DSUPPORT_PROFILE_ARB1_NV=0 -DSUPPORT_PROFILE_METAL=0 -DSUPPORT_PROFILE_SPIRV=0
      -DSUPPORT_PROFILE_GLSPIRV=0 -DMOJOSHADER_NO_VERSION_INCLUDE)
mkdir -p "$OUT/corpus-obj"
objs=()
for f in "$MOJO/mojoshader.c" "$MOJO/mojoshader_common.c" "$MOJO/profiles/mojoshader_profile_common.c" \
         "$MOJO/profiles/mojoshader_profile_glsl.c"; do
    o="$OUT/corpus-obj/$(basename "$f" .c).o"
    emcc -O1 -w "${DEFS[@]}" -c "$f" -o "$o"
    objs+=("$o")
done
em++ -std=c++17 -O1 -w "${DEFS[@]}" -I"$ROOT/src/web/d3d9/include" -I"$ROOT/src/web/d3d9" -I"$MOJO" \
    "$ROOT/src/web/d3d9/test/corpus_check.cpp" "$ROOT/src/web/d3d9/d3d9_shader_translate.cpp" "${objs[@]}" \
    -o "$OUT/corpus_check.html" --shell-file "$ROOT/src/web/d3d9/test/smoke_shell.html" \
    --preload-file "$CORPUS@/corpus" -sMAX_WEBGL_VERSION=2 -sMIN_WEBGL_VERSION=2 -sALLOW_MEMORY_GROWTH=1
echo "built $OUT/corpus_check.html"
