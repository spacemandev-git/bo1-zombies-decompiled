#!/bin/bash
# syntax-check.sh - compile-check engine source files on macOS/Linux without MSVC (no object files, no link).
#
#   tools/syntax-check.sh src/client/cl_gamepad.cpp src/win32/win_gamepad.cpp ...
#   tools/syntax-check.sh --changed          # every .c/.cpp changed against HEAD (plus untracked)
#
# Runs clang -fsyntax-only for the Windows x86 target against the MinGW-w64 headers (brew install mingw-w64 llvm),
# with the build's defines and include folders (CMakeLists.txt) and tools/syntax-check/include for the two June 2010
# DirectX SDK headers MinGW lacks. It catches typos, missing declarations and type errors in code you changed; it is
# NOT the MSVC build (MinGW headers differ in small ways, warnings are off, nothing is linked). Exit 1 if any file
# has an error. TOP=n shows the n most frequent distinct errors per file (default 8), FULL=1 prints them all.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
MW="${MINGW_ROOT:-$(brew --prefix mingw-w64 2>/dev/null)/toolchain-i686}"
CL="${CLANGXX:-$(brew --prefix llvm 2>/dev/null)/bin/clang++}"
INC="$MW/i686-w64-mingw32/include"
CXXV="$(ls "$INC/c++" 2>/dev/null | sort -V | tail -1)"
if [ ! -x "$CL" ] || [ -z "$CXXV" ]; then echo "need: brew install llvm mingw-w64" >&2; exit 2; fi

files=()
if [ "${1:-}" = "--changed" ]; then
    while IFS= read -r f; do files+=("$f"); done < <( { git diff --name-only HEAD; git ls-files --others --exclude-standard; } |
        grep -E '\.(c|cpp)$' | grep -E '^(src|tl|DemonWare)/' | grep -v '^src/web/' | sort -u)
else
    files=("$@")
fi
[ ${#files[@]} -eq 0 ] && { echo "no files"; exit 0; }

COMMON=(--target=i686-w64-windows-gnu -fsyntax-only -fms-extensions -fdeclspec -malign-double -mlong-double-64 -w -ferror-limit=0
    --sysroot="$MW" -nostdlibinc
    -I. -Isrc -Isrc/libs -Isrc/libs/libtomcrypt-1.17/src/headers -Isrc/libs/libtommath-1.0 -Isrc/libs/libvpx-1.5.0/include
    -DWIN32 -D_CONSOLE -DBO1_MP -D_CRT_SECURE_NO_WARNINGS -D_WINSOCK_NO_DEPRECATED_NO_WARNINGS -DNDEBUG)
CXX=(-x c++ -std=c++17 -Wno-c++20-extensions -fpermissive -Wno-c++11-narrowing -nostdinc++ -isystem "$INC/c++/$CXXV" -isystem "$INC/c++/$CXXV/i686-w64-mingw32")
fail=0
for f in "${files[@]}"; do
    [ -f "$f" ] || { echo "== $f (deleted, skipped)"; continue; }
    case "$f" in *.c) lang=(-x c -std=c11);; *) lang=("${CXX[@]}");; esac
    # C++ library headers first: <cmath> etc. #include_next the C headers that follow them
    out=$("$CL" "${COMMON[@]}" "${lang[@]}" -isystem "$ROOT/tools/syntax-check/include" -isystem "$INC" "$f" 2>&1)
    n=$(grep -c "error:" <<<"$out")
    echo "== $f errors=$n"
    if [ "$n" -gt 0 ]; then
        fail=1
        if [ "${FULL:-0}" = 1 ]; then grep -A3 "error:" <<<"$out"
        else grep "error:" <<<"$out" | sed -E 's/^([^:]+:[0-9]+):[0-9]+: error: /\1: /' | head -"${TOP:-8}"; fi
    fi
done
exit $fail
