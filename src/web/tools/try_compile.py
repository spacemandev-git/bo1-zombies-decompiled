#!/usr/bin/env python3
"""Syntax-check files that are not (yet) in the web build with the web build's flags.

    python3 src/web/tools/try_compile.py src/win32/win_tasks.cpp [...]

Uses the compile command of src/qcommon/msg.cpp from build/web/compile_commands.json with the source file swapped.
"""
import json, os, shlex, subprocess, sys
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
cc = json.load(open(os.path.join(ROOT, 'build', 'web', 'compile_commands.json')))
ref = next(e for e in cc if e['file'].endswith('src/qcommon/msg.cpp'))
cmd = shlex.split(ref['command'])
rc = 0
for f in sys.argv[1:]:
    path = os.path.abspath(f)
    out = []
    skip = False
    for a in cmd:
        if skip:
            skip = False
            continue
        if a == '-o':
            skip = True
            continue
        if a == '-c':
            continue
        if a.endswith('src/qcommon/msg.cpp'):
            a = path
        out.append(a)
    out += ['-fsyntax-only', '-ferror-limit=0']
    p = subprocess.run(out, cwd=ref['directory'], capture_output=True, text=True)
    errs = [l for l in p.stderr.splitlines() if 'error' in l]
    print('== %s errors=%d' % (f, len(errs)))
    for e in errs[:int(os.environ.get('TOP', '12'))]:
        print('   ' + e.replace(ROOT + '/', ''))
    rc |= p.returncode
sys.exit(1 if rc else 0)
