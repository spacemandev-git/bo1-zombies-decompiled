#!/bin/bash
# node-boot-test.sh - run the headless Node build (cmake --preset web-node && cmake --build --preset web-node) against a
# game folder and print the engine's console.
#
#   src/web/tools/node-boot-test.sh [game dir] [seconds] [extra +commands...]
#
# Without a game dir it uses an empty temp folder: the engine then stops at the first missing file (localization.txt,
# then zone/<language>/en_code_pre_gfx_mp.ff), which proves main, threads, dvars, the command line and the file
# system. WasmFS's NODERAWFS roots "/" at the current directory, so node runs from "/" and every path is absolute.
set -u
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
JS="$ROOT/build/web-node/bin/bo1.js"
[ -f "$JS" ] || { echo "build it first: cmake --preset web-node && cmake --build --preset web-node" >&2; exit 2; }
GAME="${1:-}"
SECS="${2:-30}"
shift $(( $# > 2 ? 2 : $# ))
if [ -z "$GAME" ]; then
    GAME="$(mktemp -d /tmp/bo1-node-XXXXXX)/game"
    mkdir -p "$GAME"
    printf 'english\n' > "$GAME/localization.txt"
fi
GAME="$(cd "$GAME" && pwd -P)"
HOME_DIR="$(dirname "$GAME")/home"
mkdir -p "$HOME_DIR"
cd /
node "$JS" +set fs_b "$GAME" +set fs_h "$HOME_DIR" "$@" &
PID=$!
( sleep "$SECS"; kill $PID 2>/dev/null ) &
wait $PID
