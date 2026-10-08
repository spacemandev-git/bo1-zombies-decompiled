#!/bin/bash
# run_unit_tests.sh - build the D3D9 shim's unit tests with emcc, run them under node, then validate every GLSL
# shader they emit with glslangValidator (compile as GLSL ES 3.00, plus VS+PS link checks), and cross-check the
# header constants against MinGW-w64 (if installed).
#
#   src/web/d3d9/test/run_unit_tests.sh
#
# Needs: Emscripten (~/emsdk), node; optional: glslangValidator (brew install glslang), brew llvm + mingw-w64.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../../.." && pwd)"
OUT="$ROOT/build/web-d3d9/unit"
mkdir -p "$OUT/obj" "$OUT/glsl"
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
# rebuild MojoShader when any of its sources or headers changed (local patches live in the headers too)
MOJO_NEWEST=$(ls -t "$MOJO"/*.c "$MOJO"/*.h "$MOJO"/profiles/*.c "$MOJO"/profiles/*.h | head -1)
objs=()
for f in "$MOJO/mojoshader.c" "$MOJO/mojoshader_common.c" "$MOJO/profiles/mojoshader_profile_common.c" \
         "$MOJO/profiles/mojoshader_profile_glsl.c"; do
    o="$OUT/obj/$(basename "$f" .c).o"
    [ "$o" -nt "$MOJO_NEWEST" ] || emcc -O1 -w "${DEFS[@]}" -c "$f" -o "$o"
    objs+=("$o")
done
for f in "$ROOT"/src/web/d3d9/d3d9_*.cpp "$ROOT"/src/web/d3d9/d3dx9_*.cpp "$ROOT/src/web/d3d9/test/unit_tests.cpp"; do
    o="$OUT/obj/$(basename "$f" .cpp).o"
    em++ -std=c++17 -O1 -g -Wall -Wno-unused-parameter "${DEFS[@]}" "${INC[@]}" -c "$f" -o "$o"
    objs+=("$o")
done
em++ -O1 -g "${objs[@]}" -o "$OUT/unit_tests.js" -sENVIRONMENT=node -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1 \
    -sMAX_WEBGL_VERSION=2 -sMIN_WEBGL_VERSION=2 -sEXIT_RUNTIME=1
rm -f "$OUT"/glsl/*
status=0
node "$OUT/unit_tests.js" "$OUT/glsl" "$ROOT/src/mjpeg/yuv.cpp" || status=1

if command -v glslangValidator >/dev/null 2>&1; then
    n=0; bad=0
    for f in "$OUT"/glsl/*.vert "$OUT"/glsl/*.frag; do
        n=$((n + 1))
        if ! out=$(glslangValidator "$f" 2>&1); then
            bad=$((bad + 1)); echo "GLSL FAIL: $f"; echo "$out" | head -20
        fi
    done
    # stage linking (varyings must match like WebGL requires)
    for pair in "pass.vert texmod.frag" "pass_bgra.vert vcolor.frag" "posonly_extra.vert texmod.frag" \
                "pass.vert misc.frag" "reladdr.vert vcolor.frag" "mjpeg.vert mjpeg.frag" "flow.vert math.frag" "math.vert vcolor.frag" "uloop.vert vcolor.frag"; do
        n=$((n + 1))
        # shellcheck disable=SC2086
        if ! out=$(cd "$OUT/glsl" && glslangValidator -l $pair 2>&1); then
            bad=$((bad + 1)); echo "GLSL LINK FAIL: $pair"; echo "$out" | head -20
        fi
    done
    echo "glslang: $((n - bad))/$n GLSL ES 3.00 compile/link checks passed"
    [ "$bad" -eq 0 ] || status=1
else
    echo "glslang: skipped (brew install glslang)"
fi

# CORPUS=<dir>: also re-translate a shader dump (d3d9shim_set_shader_dump / D3D9SHIM_SHADER_DUMP) or any directory
# of raw *.bin D3D9 shader bytecode files, and glslang-compile the results.
if [ -n "${CORPUS:-}" ]; then
    rm -rf "$OUT/corpus" && mkdir -p "$OUT/corpus"
    node "$OUT/unit_tests.js" --corpus "$CORPUS" "$OUT/corpus" | tail -20 || status=1
    if command -v glslangValidator >/dev/null 2>&1; then
        n=0; bad=0
        for f in "$OUT"/corpus/*.vert "$OUT"/corpus/*.frag; do
            [ -f "$f" ] || continue
            n=$((n + 1))
            glslangValidator "$f" >/dev/null 2>&1 || { bad=$((bad + 1)); echo "GLSL FAIL: $f"; }
        done
        echo "corpus glslang: $((n - bad))/$n compiled"
        [ "$bad" -eq 0 ] || status=1
    fi
fi

if command -v brew >/dev/null 2>&1 && [ -d "$(brew --prefix mingw-w64 2>/dev/null)" ]; then
    python3 "$ROOT/src/web/d3d9/test/check_header_values.py" || status=1
fi
exit $status
