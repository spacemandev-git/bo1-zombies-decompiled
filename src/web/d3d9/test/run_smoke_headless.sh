#!/bin/bash
# run_smoke_headless.sh - build (if needed) and run the D3D9 shim browser smoke tests in headless Chromium.
#
#   src/web/d3d9/test/run_smoke_headless.sh [angle-backend ...]     (default: swiftshader; also: metal, gl, vulkan)
#
# Runs both builds: smoke.html (single-threaded) and smoke_mt.html (-pthread, PROXY_TO_PTHREAD, OffscreenCanvas:
# the engine's configuration). Uses $CHROME, else Playwright's cached chrome-headless-shell. Needs node >= 22.
# Exit 0 only if every check passed everywhere. For a human: build_smoke.sh, then
#   python3 src/web/d3d9/test/serve_coi.py 8000   and open http://127.0.0.1:8000/smoke.html (or /smoke_mt.html).
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../../../.." && pwd)"
OUT="$ROOT/build/web-d3d9"
TEST="$ROOT/src/web/d3d9/test"
BACKENDS=("$@")
[ ${#BACKENDS[@]} -gt 0 ] || BACKENDS=(swiftshader)
[ -f "$OUT/smoke.wasm" ] || "$TEST/build_smoke.sh" >/dev/null || exit 2
[ -f "$OUT/smoke_mt.wasm" ] || THREADED=1 "$TEST/build_smoke.sh" >/dev/null || exit 2
CHROME="${CHROME:-}"
if [ -z "$CHROME" ]; then
    for d in "$HOME/Library/Caches/ms-playwright" "$HOME/.cache/ms-playwright"; do
        c=$(find "$d" -type f \( -name chrome-headless-shell -o -name headless_shell \) 2>/dev/null | sort | tail -1)
        [ -n "$c" ] && CHROME="$c" && break
    done
fi
[ -n "$CHROME" ] || { echo "no headless Chromium: set CHROME=/path/to/chrome"; exit 2; }
PORT=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1])')
python3 "$TEST/serve_coi.py" "$PORT" "$OUT" >/dev/null 2>&1 &
SERVER=$!
trap 'kill $SERVER 2>/dev/null' EXIT
sleep 1
status=0
for backend in "${BACKENDS[@]}"; do
    for page in smoke smoke_mt; do
        out=$(node "$TEST/run_smoke_cdp.mjs" "$CHROME" "http://127.0.0.1:$PORT/$page.html" "$backend" 90)
        rc=$?
        echo "$out" | grep -v '^PASS'
        echo "== $page.html on $backend: $([ $rc -eq 0 ] && echo ok || echo FAILED)"
        [ $rc -eq 0 ] || status=1
    done
done
exit $status
