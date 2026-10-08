#!/usr/bin/env python3
"""check_engine_compile.py - syntax-check engine TUs that use Direct3D against the shim headers, with the exact
flags of the web build (build/web/compile_commands.json from `cmake --preset web`).

    python3 src/web/d3d9/test/check_engine_compile.py [path-substring ...]   (default: gfx_d3d, mjpeg, web/d3d9)

Prints per-file error counts and the most frequent error messages. D3D-related errors are the shim's business;
others belong to whoever owns the file / the compat layer.
"""
import json
import os
import re
import shlex
import subprocess
import sys
from collections import Counter
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', '..'))
CC = os.path.join(ROOT, 'build', 'web', 'compile_commands.json')


def syntax_cmd(entry):
    args = entry['arguments'] if 'arguments' in entry else shlex.split(entry['command'])
    out, skip = [], False
    for a in args:
        if skip:
            skip = False
            continue
        if a == '-o':
            skip = True
            continue
        if a == '-c':
            continue
        out.append(a)
    out.insert(1, '-fsyntax-only')
    out.append('-ferror-limit=0')
    return out


def run(entry):
    p = subprocess.run(syntax_cmd(entry), cwd=entry['directory'], capture_output=True, text=True)
    errs = [l for l in p.stderr.splitlines() if ' error: ' in l]
    return entry['file'], errs


def main():
    if not os.path.exists(CC):
        print('no %s: run cmake --preset web first' % CC)
        return 2
    pats = sys.argv[1:] or ['/gfx_d3d/', '/mjpeg/', '/web/d3d9/']
    entries = [e for e in json.load(open(CC)) if any(p in e['file'] for p in pats)]
    total = Counter()
    bad = 0
    with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as ex:
        for f, errs in ex.map(run, entries):
            if errs:
                bad += 1
                print('== %s: %d errors' % (os.path.relpath(f, ROOT), len(errs)))
                for e in errs[:4]:
                    print('   ' + re.sub(r'^.*? error: ', '', e))
                for e in errs:
                    total[re.sub(r'^.*? error: ', '', e)] += 1
    print('%d files checked, %d with errors' % (len(entries), bad))
    for msg, n in total.most_common(25):
        print('%5d  %s' % (n, msg))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
