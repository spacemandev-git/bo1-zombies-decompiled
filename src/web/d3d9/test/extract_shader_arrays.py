#!/usr/bin/env python3
"""extract_shader_arrays.py - pull D3D9 shader token arrays (SM 2.0+, "const DWORD name[] = {0xfffe0300, ...,
0x0000ffff};") out of C sources into <out-dir>/<vs|ps>_<n>_<name>.bin files for the translation corpus
(`CORPUS=<out-dir> src/web/d3d9/test/run_unit_tests.sh`). Used by fetch_wine_corpus.sh on Wine's d3d9 tests.

    python3 -I extract_shader_arrays.py <out-dir> <file.c> [...]
"""
import re, sys, os, struct
out = sys.argv[1]
os.makedirs(out, exist_ok=True)
n = 0
seen = set()
for path in sys.argv[2:]:
    src = open(path, encoding='latin-1').read()
    for m in re.finditer(r'(?:static\s+)?const\s+DWORD\s+(\w+)\[\]\s*=\s*\{(.*?)\};', src, re.S):
        name, body = m.group(1), m.group(2)
        body = re.sub(r'/\*.*?\*/', '', body, flags=re.S)
        body = re.sub(r'//[^\n]*', '', body)
        toks = []
        ok = True
        for t in body.replace('\n', ' ').split(','):
            t = t.strip()
            if not t:
                continue
            try:
                toks.append(int(t.rstrip('uUlL'), 0) & 0xffffffff)
            except ValueError:
                ok = False
                break
        if not ok or len(toks) < 2:
            continue
        ver = toks[0]
        if (ver & 0xffff0000) not in (0xfffe0000, 0xffff0000) or ((ver >> 8) & 0xff) < 2 or toks[-1] != 0xffff:
            continue
        key = tuple(toks)
        if key in seen:
            continue
        seen.add(key)
        kind = 'vs' if (ver & 0xffff0000) == 0xfffe0000 else 'ps'
        fname = '%s_%03d_%s.bin' % (kind, n, name)
        open(os.path.join(out, fname), 'wb').write(struct.pack('<%dI' % len(toks), *toks))
        n += 1
print(n, 'shaders extracted')
