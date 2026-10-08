#!/usr/bin/env python3
"""List the engine symbols the web link cannot resolve (before emscripten turns undefined functions into imports).

    python3 src/web/tools/undefined.py [--build build/web] [--by-object] [--grep REGEX]

nm's every object of the web build, subtracts what the objects define and what Emscripten's system libraries (libc,
libc++, compiler-rt, ...) define, and prints the remaining undefined symbols (demangled) with the objects that use
them. Data symbols in this list are link errors; functions become imports that abort when called.
"""
import argparse, collections, glob, os, re, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
EMSDK = os.environ.get('EMSDK', os.path.expanduser('~/emsdk'))
NM = os.path.join(EMSDK, 'upstream', 'bin', 'llvm-nm')
SYSLIB = os.path.join(EMSDK, 'upstream', 'emscripten', 'cache', 'sysroot', 'lib', 'wasm32-emscripten')

def nm(paths):
    out = subprocess.run([NM, '--format=just-symbols', '-A', '--defined-only'] + paths, capture_output=True, text=True).stdout
    return out

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--build', default=os.path.join(ROOT, 'build', 'web'))
    ap.add_argument('--by-object', action='store_true')
    ap.add_argument('--grep', default=None)
    args = ap.parse_args()
    objs = sorted(glob.glob(os.path.join(args.build, 'CMakeFiles', 'BO1Zombies.dir', '**', '*.o'), recursive=True))
    defined = set()
    undefined = collections.defaultdict(set)
    for i in range(0, len(objs), 200):
        chunk = objs[i:i + 200]
        out = subprocess.run([NM, '-A', '-P'] + chunk, capture_output=True, text=True).stdout
        for line in out.splitlines():
            # path: name type value size
            m = re.match(r'^(.*?\.o): (\S+) (\S)', line)
            if not m:
                continue
            path, name, t = m.groups()
            if t in 'Uu':
                undefined[name].add(os.path.relpath(path, os.path.join(args.build, 'CMakeFiles', 'BO1Zombies.dir')))
            elif t not in 'w':
                defined.add(name)
    libs = [p for p in glob.glob(os.path.join(SYSLIB, '*.a')) if re.search(r'lib(c|c\+\+|c\+\+abi|compiler_rt|dlmalloc|mimalloc|wasmfs|sockets|stubs|GL|html5|noexit|unwind|standalonewasm)', os.path.basename(p))]
    out = subprocess.run([NM, '-P', '--defined-only'] + libs, capture_output=True, text=True).stdout
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 2 and parts[1] not in 'Uuw':
            defined.add(parts[0])
    missing = {n: o for n, o in undefined.items() if n not in defined and not n.startswith('__') and not n.startswith('emscripten_') and not n.startswith('_emscripten')}
    names = sorted(missing)
    dem = subprocess.run([os.path.join(EMSDK, 'upstream', 'bin', 'llvm-cxxfilt')], input='\n'.join(names), capture_output=True, text=True).stdout.splitlines()
    rows = sorted(zip(dem, names), key=lambda r: r[0])
    if args.grep:
        rx = re.compile(args.grep)
        rows = [r for r in rows if rx.search(r[0]) or any(rx.search(o) for o in missing[r[1]])]
    if args.by_object:
        by = collections.defaultdict(list)
        for d, n in rows:
            for o in missing[n]:
                by[o].append(d)
        for o in sorted(by):
            print(o)
            for d in sorted(by[o]):
                print('    ' + d)
    else:
        for d, n in rows:
            users = sorted(missing[n])
            print('%-70s %s%s' % (d[:70], ', '.join(users[:3]), ' +%d' % (len(users) - 3) if len(users) > 3 else ''))
    print('\n%d undefined symbols' % len(rows), file=sys.stderr)

if __name__ == '__main__':
    main()
