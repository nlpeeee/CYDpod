"""Make esp32-libs 3.3.10-cydpod-bt from Espressif's 3.3.10 package.

Compares the stock and trim ESP-IDF builds from build-libs.ps1. Every library
that differs between them is replaced in a copy of Espressif's package by the
trim build; libraries that are identical stay Espressif's binaries. sdkconfig
and both flash-mode sdkconfig.h files get exactly the stock->trim key changes.
Linker scripts are compared too; a difference stops the script so that it can
be reviewed rather than silently replaced (Espressif's scripts also carry
Arduino component fragments this project does not build).

usage: python make-libs.py <repo root> <out dir>
"""
import hashlib, json, os, re, shutil, sys

root, out = sys.argv[1], sys.argv[2]
stock_pkg = os.path.join(root, '.deps', 'arduino', 'packages', 'esp32', 'tools', 'esp32-libs', '3.3.10')
if os.path.isdir(stock_pkg + '.stock'): stock_pkg += '.stock'
work = os.path.join(root, '.tools', 'bt-build')
S, T = os.path.join(work, 'out', 'stock'), os.path.join(work, 'out', 'trim')

def sha(p):
    h = hashlib.sha256()
    with open(p, 'rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''): h.update(b)
    return h.hexdigest()

def libs(build):
    d = os.path.join(build, 'lib')
    return {n: os.path.join(d, n) for n in os.listdir(d) if n.endswith('.a')}

STAMP = re.compile(rb'^(?:[A-Z][a-z]{2} [ \d]\d \d{4} )?\d\d:\d\d:\d\d$')
def timestamp_only(a, b):
    """Same size, and every printable string that differs is a __DATE__/__TIME__ stamp."""
    x, y = open(a, 'rb').read(), open(b, 'rb').read()
    if len(x) != len(y): return False
    sx, sy = set(re.findall(rb'[\x20-\x7e]{5,}', x)), set(re.findall(rb'[\x20-\x7e]{5,}', y))
    return all(STAMP.match(s) for s in sx ^ sy)

if os.path.exists(out): sys.exit('exists: ' + out)
report = {'base': stock_pkg, 'replaced': [], 'identical': 0, 'changed_not_shipped': [], 'shipped_not_built': 0}
sl, tl = libs(S), libs(T)
shipped = set(os.listdir(os.path.join(stock_pkg, 'lib')))
shutil.copytree(stock_pkg, out)
for name in sorted(set(sl) | set(tl)):
    a, b = sl.get(name), tl.get(name)
    if a and b and (sha(a) == sha(b) or timestamp_only(a, b)):
        report['identical'] += 1; continue
    if name not in shipped:
        report['changed_not_shipped'].append(name); continue
    if not b: sys.exit('trim build lost shipped library ' + name)
    shutil.copy2(b, os.path.join(out, 'lib', name))
    report['replaced'].append({'lib': name, 'stockBuild': sha(a) if a else None, 'trim': sha(b),
                               'espressif': sha(os.path.join(stock_pkg, 'lib', name))})
report['shipped_not_built'] = len(shipped - set(sl))

for ld in ('memory.ld', 'sections.ld'):
    a = os.path.join(S, 'ld', ld); b = os.path.join(T, 'ld', ld)
    body = lambda p: [l for l in open(p, encoding='utf-8') if 'Generated from:' not in l]  # build-path comment
    if body(a) != body(b): sys.exit('linker script differs, review needed: ' + ld)

def defines(path):
    d = {}
    for line in open(path, encoding='utf-8'):
        m = re.match(r'#define (CONFIG_\w+) (.*)', line.rstrip('\n'))
        if m: d[m.group(1)] = m.group(2)
    return d
ds, dt = defines(os.path.join(S, 'config', 'sdkconfig.h')), defines(os.path.join(T, 'config', 'sdkconfig.h'))
removed = sorted(k for k in ds if k not in dt)
added = sorted(k for k in dt if k not in ds)
changed = sorted(k for k in ds if k in dt and ds[k] != dt[k])
report['sdkconfig_h'] = {'removed': removed, 'added': {k: dt[k] for k in added}, 'changed': {k: [ds[k], dt[k]] for k in changed}}
for variant in ('dio_qspi', 'qio_qspi'):
    p = os.path.join(out, variant, 'include', 'sdkconfig.h')
    lines = open(p, encoding='utf-8').read().split('\n')
    keep = []
    for line in lines:
        m = re.match(r'#define (CONFIG_\w+) ', line)
        if m and m.group(1) in removed: continue
        if m and m.group(1) in changed: line = '#define %s %s' % (m.group(1), dt[m.group(1)])
        keep.append(line)
    text = '\n'.join(keep).rstrip('\n') + '\n' + ''.join('#define %s %s\n' % (k, dt[k]) for k in added)
    open(p, 'w', encoding='utf-8', newline='\n').write(text)

def cfg(path):
    d = {}
    for line in open(path, encoding='utf-8'):
        line = line.rstrip('\n')
        m = re.match(r'(CONFIG_\w+)=(.*)', line) or re.match(r'# (CONFIG_\w+) is not set', line)
        if m: d[m.group(1)] = m.group(2) if m.lastindex == 2 else None
    return d
cs, ct = cfg(os.path.join(S, 'sdkconfig')), cfg(os.path.join(T, 'sdkconfig'))
delta = {k: ct.get(k) for k in set(cs) | set(ct) if cs.get(k, 'missing') != ct.get(k, 'missing')}
report['sdkconfig_delta'] = dict(sorted(delta.items()))
p = os.path.join(out, 'sdkconfig')
lines, seen = [], set()
for line in open(p, encoding='utf-8').read().split('\n'):
    m = re.match(r'(CONFIG_\w+)=', line) or re.match(r'# (CONFIG_\w+) is not set', line)
    if m and m.group(1) in delta:
        k = m.group(1); seen.add(k)
        if k not in ct: continue
        line = '%s=%s' % (k, ct[k]) if ct[k] is not None else '# %s is not set' % k
    lines.append(line)
lines += ['%s=%s' % (k, v) if v is not None else '# %s is not set' % k for k, v in sorted(delta.items()) if k not in seen and k in ct]
open(p, 'w', encoding='utf-8', newline='\n').write('\n'.join(lines).rstrip('\n') + '\n')
with open(os.path.join(out, 'versions.txt'), 'a', encoding='utf-8', newline='\n') as f:
    f.write('cydpod-bt: trimmed Bluedroid, tools/bluedroid/defconfig.cydpod-bt sha256 %s\n' %
            sha(os.path.join(root, 'tools', 'bluedroid', 'defconfig.cydpod-bt')))
    pdir = os.path.join(root, 'tools', 'bluedroid', 'patches')
    for name in sorted(n for n in os.listdir(pdir) if n.endswith('.patch')) if os.path.isdir(pdir) else []:
        f.write('cydpod-bt-patch: %s sha256 %s\n' % (name, sha(os.path.join(pdir, name))))
json.dump(report, open(os.path.join(root, '.tools', 'logs', 'bluedroid-make-libs.json'), 'w'), indent=1)
print('replaced', len(report['replaced']), 'libs:', ', '.join(r['lib'] for r in report['replaced']))
print('identical', report['identical'], 'changed-not-shipped', report['changed_not_shipped'], 'shipped-not-built', report['shipped_not_built'])
print('sdkconfig.h removed %d added %d changed %d' % (len(removed), len(added), len(changed)))
