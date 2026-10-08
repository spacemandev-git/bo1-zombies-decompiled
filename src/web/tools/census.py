#!/usr/bin/env python3
"""Compile census for the web build: which engine translation units compile with Emscripten.

    python3 src/web/tools/census.py [--build build/web] [--jobs N] [--filter REGEX] [--top N] [--full]

Runs every compile command of <build>/compile_commands.json (cmake --preset web writes it) with -fsyntax-only, in
parallel, and stores each TU's diagnostics in <build>/census/<source path>.log. Prints a table per top-level folder
(TUs / compiling), the most frequent normalized error messages, and writes <build>/census/summary.json.
Exit code 0 when every selected TU compiles.
"""
import argparse, collections, concurrent.futures as cf, json, os, re, shlex, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))

def area(rel):
    parts = rel.split('/')
    if parts[0] == 'src' and len(parts) > 2:
        if parts[1] in ('libs', 'web') and len(parts) > 3:
            return parts[1] + '/' + parts[2]
        return parts[1]
    return parts[0]

def norm(msg):
    msg = re.sub(r"'[^']*'", "'X'", msg)
    msg = re.sub(r'\d+', 'N', msg)
    return msg.strip()

WARN = None

def run(entry, outdir):
    cmd = shlex.split(entry['command']) if 'command' in entry else list(entry['arguments'])
    if WARN:
        cmd = [a for a in cmd if a != '-w'] + ['-Wno-everything'] + WARN
    out = []
    skip = False
    for i, a in enumerate(cmd):
        if skip:
            skip = False
            continue
        if a == '-o':
            skip = True
            continue
        if a == '-c':
            continue
        out.append(a)
    out.append('-fsyntax-only')
    out.append('-ferror-limit=0')
    p = subprocess.run(out, cwd=entry['directory'], capture_output=True, text=True)
    rel = os.path.relpath(entry['file'], ROOT)
    log = os.path.join(outdir, rel + '.log')
    os.makedirs(os.path.dirname(log), exist_ok=True)
    with open(log, 'w') as f:
        f.write(p.stderr)
    errors = [l for l in p.stderr.splitlines() if ': error: ' in l or l.startswith('error:')]
    if WARN:
        errors = [l for l in p.stderr.splitlines() if ': warning: ' in l]
    return rel, p.returncode, errors

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--build', default=os.path.join(ROOT, 'build', 'web'))
    ap.add_argument('--jobs', type=int, default=os.cpu_count())
    ap.add_argument('--filter', default=None)
    ap.add_argument('--top', type=int, default=40)
    ap.add_argument('--full', action='store_true', help='list every failing TU with its first error')
    ap.add_argument('--warn', default=None, help='replace -w with these clang warning flags (comma separated), e.g. '
                    '-Wcast-function-type-strict, and list the warnings (audits)')
    args = ap.parse_args()
    global WARN
    WARN = args.warn.split(',') if args.warn else None
    cc = json.load(open(os.path.join(args.build, 'compile_commands.json')))
    if args.filter:
        rx = re.compile(args.filter)
        cc = [e for e in cc if rx.search(os.path.relpath(e['file'], ROOT))]
    outdir = os.path.join(args.build, 'census')
    results = {}
    with cf.ThreadPoolExecutor(args.jobs) as ex:
        for rel, rc, errors in ex.map(lambda e: run(e, outdir), cc):
            results[rel] = (rc, errors)
    per_area = collections.OrderedDict()
    msgs = collections.Counter()
    msg_files = collections.defaultdict(set)
    for rel in sorted(results):
        rc, errors = results[rel]
        a = area(rel)
        t = per_area.setdefault(a, [0, 0])
        t[0] += 1
        if rc == 0:
            t[1] += 1
        for e in errors:
            m = norm(e.split(': error: ', 1)[-1]) if not WARN else e.split(ROOT + '/', 1)[-1]
            msgs[m] += 1
            msg_files[m].add(rel)
    total = sum(t[0] for t in per_area.values())
    ok = sum(t[1] for t in per_area.values())
    print('%-28s %6s %9s' % ('area', 'TUs', 'compiling'))
    for a, (n, k) in sorted(per_area.items()):
        print('%-28s %6d %9d%s' % (a, n, k, '' if n == k else '   (%d failing)' % (n - k)))
    print('%-28s %6d %9d' % ('TOTAL', total, ok))
    print('\nmost frequent errors (count / files):')
    for m, c in msgs.most_common(args.top):
        print('%6d %4d  %s' % (c, len(msg_files[m]), m[:180]))
    if args.full:
        print('\nfailing TUs:')
        for rel in sorted(results):
            rc, errors = results[rel]
            if rc:
                print('  %s (%d): %s' % (rel, len(errors), errors[0][:200] if errors else '?'))
    json.dump({rel: {'ok': rc == 0, 'errors': len(err)} for rel, (rc, err) in results.items()},
              open(os.path.join(outdir, 'summary.json'), 'w'), indent=1, sort_keys=True)
    return 0 if ok == total else 1

if __name__ == '__main__':
    sys.exit(main())
