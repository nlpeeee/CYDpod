"""Worst-case static stack depth from GCC -fcallgraph-info=su (.ci) files.

usage: python -I tools/stack_depth.py <dir with .ci files> <root> [<root> ...]
(a root is matched against the demangled function signature, e.g. "void decodeTask(void*)")

Frame sizes are the compiler's static sizes (windowed Xtensa: the entry frame already includes the register save area).
Callees without a .ci (precompiled libraries: newlib, VFS, FAT, FreeRTOS, Arduino core) count as 0 and are listed so the
caller can add an allowance. Indirect calls and recursion are reported, not followed.
"""
import os, re, sys

NODE = re.compile(r'node: \{ title: "([^"]*)" label: "([^"]*)"')
EDGE = re.compile(r'edge: \{ sourcename: "([^"]*)" targetname: "([^"]*)"')
SIZE = re.compile(r'(\d+) bytes \((static|dynamic[^)]*)\)')

def sym(title):
    return title.rsplit(':', 1)[-1] if ':' in title else title

size, name, calls = {}, {}, {}
for dirpath, _, files in os.walk(sys.argv[1]):
    for fn in files:
        if not fn.endswith('.ci'): continue
        for line in open(os.path.join(dirpath, fn), encoding='utf-8', errors='replace'):
            m = NODE.match(line)
            if m:
                s = sym(m.group(1)); label = m.group(2).split('\\n')
                z = SIZE.search(m.group(2))
                size[s] = max(size.get(s, 0), int(z.group(1)) if z else 0)
                name[s] = label[0]
                continue
            m = EDGE.match(line)
            if m: calls.setdefault(sym(m.group(1)), set()).add(sym(m.group(2)))

memo, missing, indirect = {}, set(), set()
def depth(s, stack=()):
    if s in memo: return memo[s]
    if s in stack: return (0, [])
    if s not in size:
        missing.add(s); return (0, [s])
    best = (0, [])
    for c in calls.get(s, ()):
        if c.startswith('__indirect'): indirect.add(s); continue
        d = depth(c, stack + (s,))
        if d[0] > best[0] or (d[0] == best[0] and not best[1]): best = d
    memo[s] = (size[s] + best[0], [s] + best[1])
    return memo[s]

for root in sys.argv[2:]:
    cands = [s for s in size if s == root or root in name.get(s, '')]
    if not cands: print(root, 'NOT FOUND'); continue
    for s in sorted(cands)[:4]:
        total, path = depth(s)
        print('%-60s %5d B  frame %d' % (name.get(s, s)[:60], total, size[s]))
        acc = 0
        for p in path:
            acc += size.get(p, 0)
            print('    %5d  %5d  %s' % (size.get(p, 0), acc, name.get(p, p)[:100]))
print('external (no .ci, counted 0):', ', '.join(sorted(m for m in missing if not m.startswith('__')))[:900])
print('functions with indirect calls on a followed path:', len(indirect))
