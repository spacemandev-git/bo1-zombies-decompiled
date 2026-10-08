#!/usr/bin/env python3
"""Function pointer casts that change the WebAssembly signature (they trap when called: "function signature mismatch").

    python3 src/web/tools/census.py --warn=-Wcast-function-type-strict >/dev/null   # writes build/web/census/**.log
    python3 src/web/tools/fnptr_audit.py [--all]

clang's -Wcast-function-type-strict reports every cast between different C function types. Most are harmless in
wasm, where every pointer, int, enum, bool, char and short is an i32 (qsort comparators taking 'T *' instead of
'const void *', 'char *' vs 'const char *'). This keeps the casts whose wasm signatures differ: other parameter
count, i64 / f32 / f64 where the other side has something else, or a return value on one side only. Those calls work
on x86 (cdecl tolerates it) and trap on the web. --all prints the harmless ones too.
"""
import collections, glob, os, re, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
CAST = re.compile(r"^(.*?):(\d+):\d+: warning: cast from '(.*)' to '(.*)' converts to incompatible function type")

def split_params(s):
    out, depth, cur = [], 0, ''
    for ch in s:
        if ch in '(<[':
            depth += 1
        elif ch in ')>]':
            depth -= 1
        if ch == ',' and depth == 0:
            out.append(cur.strip())
            cur = ''
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out

def wasm_type(t):
    t = re.sub(r'__attribute__\(\(.*?\)\)', '', t).replace('const', '').replace('volatile', '').strip()
    if t in ('void', ''):
        return None
    if '*' in t or '&' in t or '[' in t:
        return 'i32'
    if re.search(r'\b(long long|__int64|int64_t|uint64_t|ULONGLONG|LONGLONG|unsigned long long|DWORD64|UINT64|INT64|scr_entref_t)\b', t):
        return 'i64' if 'scr_entref_t' not in t else 'i32'
    if re.fullmatch(r'(float|FLOAT)', t):
        return 'f32'
    if re.fullmatch(r'(double|long double)', t):
        return 'f64' if t == 'double' else 'f128'
    if re.search(r'\b(struct|class|union)\b', t) or (t[:1].isupper() and t not in ('BOOL', 'DWORD', 'UINT', 'INT', 'LONG', 'ULONG', 'WORD', 'BYTE', 'HRESULT', 'HANDLE', 'LPVOID', 'SIZE_T')):
        return 'i32?'   # a struct by value (passed by pointer in the wasm C ABI) or an enum / typedef: i32 either way
    return 'i32'

def signature(fn):
    m = re.match(r'^(.*?)\s*\(\*\)\((.*)\)\s*(__attribute__.*)?$', fn.strip())
    if not m:
        return None
    ret, params = m.group(1), m.group(2)
    ps = [] if params.strip() in ('', 'void') else split_params(params)
    variadic = ps and ps[-1] == '...'
    if variadic:
        ps = ps[:-1]
    norm = lambda x: None if x is None else ('i32' if x == 'i32?' else x)
    return norm(wasm_type(ret)), tuple(norm(wasm_type(p)) for p in ps), bool(variadic)

def main():
    show_all = '--all' in sys.argv
    seen = set()
    bad = collections.defaultdict(list)
    harmless = 0
    for log in glob.glob(os.path.join(ROOT, 'build', 'web', 'census', '**', '*.log'), recursive=True):
        for line in open(log, errors='ignore'):
            m = CAST.match(line.strip())
            if not m:
                continue
            path, ln, a, b = m.groups()
            key = (path, ln, a, b)
            if key in seen:
                continue
            seen.add(key)
            sa, sb = signature(a), signature(b)
            rel = os.path.relpath(path, ROOT)
            if sa is None or sb is None:
                bad[rel].append((ln, a, b, 'unparsed'))
            elif sa != sb:
                bad[rel].append((ln, a, b, '%s vs %s' % (sa, sb)))
            else:
                harmless += 1
                if show_all:
                    print('harmless %s:%s %s -> %s' % (rel, ln, a, b))
    n = 0
    for rel in sorted(bad):
        for ln, a, b, why in sorted(bad[rel], key=lambda r: int(r[0])):
            n += 1
            print('%s:%s\n    %s\n -> %s\n    (%s)' % (rel, ln, a, b, why))
    print('\n%d casts change the wasm signature, %d are harmless' % (n, harmless), file=sys.stderr)

if __name__ == '__main__':
    main()
