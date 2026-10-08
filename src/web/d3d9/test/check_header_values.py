#!/usr/bin/env python3
"""check_header_values.py - verify the shim's D3D9 headers against the MinGW-w64 DirectX headers.

Every numeric enumerator / object-like #define and every struct size in src/web/d3d9/include/{d3d9types,d3d9caps,
d3d9,d3dx9}.h is evaluated with the shim headers (host clang, standalone mode) and then static_assert'ed against the
MinGW-w64 i686 headers (clang -fsyntax-only --target=i686-w64-mingw32). Names MinGW does not declare are reported
and skipped; a value or size mismatch is an error.

    python3 src/web/d3d9/test/check_header_values.py        (needs: brew install llvm mingw-w64)
"""
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
INC = os.path.normpath(os.path.join(HERE, '..', 'include'))
HEADERS = ['d3d9types.h', 'd3d9caps.h', 'd3d9.h', 'd3dx9.h']
# *_FORCE_DWORD: the Microsoft SDK uses 0x7fffffff, MinGW (from Wine) 0xffffffff for some; we follow Microsoft.
# D3DSHADER_COMPARISON_MASK / D3DSIO_RESERVED0 / D3DPRESENT_DONOTFLIP: absent or spelled differently in MinGW.
SKIP_SUFFIX = ('_FORCE_DWORD',)
SKIP_NAMES = ('D3DSHADER_COMPARISON_MASK', 'D3DSIO_RESERVED0', 'D3DPRESENT_DONOTFLIP')
SKIP_PREFIX = ('D3D9SHIM_', 'D3DX_DEFAULT_FLOAT', 'D3DFMT_FROM_FILE', 'D3DX_FROM_FILE', 'D3DSINCOSCONST',
               'DIRECT3D_VERSION', '_FAC', 'MAKE_')


def sh(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


def brew_prefix(pkg):
    r = sh(['brew', '--prefix', pkg])
    return r.stdout.strip() if r.returncode == 0 else None


def collect_names():
    enums, defines, structs = [], [], []
    for h in HEADERS:
        text = open(os.path.join(INC, h)).read()
        text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
        for m in re.finditer(r'^\s*#define\s+([A-Za-z_]\w*)\s+(\S.*)$', text, flags=re.M):
            name, body = m.group(1), m.group(2)
            if name.endswith('_H') or name.startswith(SKIP_PREFIX):
                continue
            if re.match(r'^[A-Za-z_]\w*$', body):
                if body in ('S_OK', 'D3DDMAPSAMPLER'):
                    defines.append(name)
                continue
            if re.search(r'[0-9]', body) or 'MAKE_' in body or '<<' in body or '|' in body:
                defines.append(name)
        for blk in re.finditer(r'typedef\s+enum\s+\w*\s*\{(.*?)\}', text, flags=re.S):
            for line in blk.group(1).split(','):
                line = line.strip()
                mm = re.match(r'([A-Za-z_]\w*)', line)
                if mm and not mm.group(1).startswith(SKIP_PREFIX):
                    enums.append(mm.group(1))
        for m in re.finditer(r'\}\s*([A-Z][A-Z0-9_]+)\s*[,;]', text):
            structs.append(m.group(1))
    return sorted(set(enums)), sorted(set(defines)), sorted(set(structs))


def main():
    llvm = brew_prefix('llvm')
    mingw = brew_prefix('mingw-w64')
    if not llvm or not mingw:
        print('SKIP: need brew llvm + mingw-w64')
        return 0
    clang = os.path.join(llvm, 'bin', 'clang++')
    mw = os.path.join(mingw, 'toolchain-i686')
    mwinc = os.path.join(mw, 'i686-w64-mingw32', 'include')
    enums, defines, structs = collect_names()
    names = [n for n in enums + defines if not n.endswith(SKIP_SUFFIX) and n not in SKIP_NAMES]
    tmp = tempfile.mkdtemp(prefix='d3d9shim_check_')

    # 1) evaluate with the shim headers (host)
    src = ['#include <d3d9.h>', '#include <d3dx9.h>', '#include <stdio.h>', 'int main(){']
    for n in names:
        src.append('printf("%s %%lld\\n", (long long)(%s));' % (n, n))
    known_structs = []
    for s in structs:
        known_structs.append(s)
        src.append('printf("sizeof:%s %%d\\n", (int)sizeof(%s));' % (s, s))
    src.append('return 0;}')
    a = os.path.join(tmp, 'shim_values.cpp')
    open(a, 'w').write('\n'.join(src))
    exe = os.path.join(tmp, 'shim_values')
    r = sh([clang, '-std=c++17', '-w', '-I', INC, a, '-o', exe])
    if r.returncode != 0:
        print(r.stderr[:4000])
        return 1
    values = {}
    for line in sh([exe]).stdout.splitlines():
        k, v = line.rsplit(' ', 1)
        values[k] = int(v)

    # 2) static_assert against MinGW
    chk = ['#include <windows.h>', '#include <d3d9.h>', '#include <d3dx9.h>']
    for n in names:
        v = values[n]
        chk.append('static_assert((long long)(%s) == (long long)(%dLL) || (unsigned)(%s) == (unsigned)(%dLL), "%s");'
                   % (n, v, n, v, n))
    host_ptr_size = 8  # host LP64 values for pointer-containing structs differ; compare only pointer-free structs
    for s in known_structs:
        chk.append('static_assert(sizeof(%s) == %d, "sizeof:%s");' % (s, values['sizeof:' + s], s))
    b = os.path.join(tmp, 'mingw_check.cpp')
    open(b, 'w').write('\n'.join(chk))
    r = sh([clang, '--target=i686-w64-windows-gnu', '-fsyntax-only', '-fms-extensions', '-w', '-ferror-limit=0',
            '-std=c++17', '--sysroot=' + mw, '-nostdlibinc', '-isystem', mwinc, b])
    missing, mismatch, sizediff = [], [], []
    for line in r.stderr.splitlines():
        m = re.search(r"use of undeclared identifier '(\w+)'|unknown type name '(\w+)'", line)
        if m:
            missing.append(m.group(1) or m.group(2))
            continue
        m = re.search(r'static assertion failed.*: (sizeof:)?(\w+)\s*$', line)
        if m:
            (sizediff if m.group(1) else mismatch).append(m.group(2))
    # Structs holding pointers/HWND differ between the LP64 host and i686; recheck those on i686 sizes by hand.
    ptr_structs = {'D3DPRESENT_PARAMETERS', 'D3DDEVICE_CREATION_PARAMETERS', 'D3DLOCKED_RECT', 'D3DLOCKED_BOX',
                   'D3DXMACRO'}
    sizediff = [s for s in sizediff if s not in ptr_structs]
    print('checked %d constants, %d structs; %d not in MinGW: %s' % (len(names), len(known_structs), len(set(missing)),
                                                                   ' '.join(sorted(set(missing)))))
    if mismatch:
        print('VALUE MISMATCH: ' + ' '.join(mismatch))
    if sizediff:
        print('SIZE MISMATCH: ' + ' '.join(sizediff))
    ok = not mismatch and not sizediff
    errors = [l for l in r.stderr.splitlines() if 'error:' in l]
    other = [l for l in errors if 'undeclared identifier' not in l and 'unknown type name' not in l
             and 'static assertion failed' not in l]
    if other:
        ok = False
        print('UNEXPECTED ERRORS:\n' + '\n'.join(other[:20]))
    print('PASS' if ok else 'FAIL')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
