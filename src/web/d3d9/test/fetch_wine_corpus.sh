#!/bin/bash
# fetch_wine_corpus.sh - download Wine's d3d9 conformance tests (LGPL; data only, not vendored) into
# build/web-d3d9/wine-src and extract their SM2/SM3 shader token arrays into build/web-d3d9/corpus_wine, then run
# them through the translator and glslang:
#
#   src/web/d3d9/test/fetch_wine_corpus.sh && CORPUS=build/web-d3d9/corpus_wine src/web/d3d9/test/run_unit_tests.sh
#
# Expected (wine-8.0 visual.c): 80 shaders, 79 translate (one reads an uninitialized temp on purpose), 79 compile.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../../.." && pwd)"
SRC="$ROOT/build/web-d3d9/wine-src"
OUT="$ROOT/build/web-d3d9/corpus_wine"
mkdir -p "$SRC"
curl -sfL -o "$SRC/visual.c" "https://raw.githubusercontent.com/wine-mirror/wine/wine-8.0/dlls/d3d9/tests/visual.c"
rm -rf "$OUT"
python3 -I "$ROOT/src/web/d3d9/test/extract_shader_arrays.py" "$OUT" "$SRC/visual.c"
