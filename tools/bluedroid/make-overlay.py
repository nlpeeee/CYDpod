"""Pack the trimmed-Bluedroid files as an overlay for Espressif's esp32-libs 3.3.10.

Compares Espressif's package (esp32-libs/3.3.10.stock when switched, else
3.3.10) with the trimmed package from make-libs.py and writes:
  - <out>/esp32-libs-3.3.10-cydpod-bt.zip: only the files that differ, at
    their package-relative paths;
  - the "bluedroidOverlay" entry in dependencies.lock.json: the archive hash,
    and for every file the stock and the trimmed SHA-256.
tools/release/Restore-Bluedroid.py applies it to a restored build kit.

usage: python make-overlay.py <repo root> <out dir>
"""
import hashlib, json, os, sys, zipfile

root, out = sys.argv[1], sys.argv[2]
libs = os.path.join(root, '.deps', 'arduino', 'packages', 'esp32', 'tools', 'esp32-libs')
stock = os.path.join(libs, '3.3.10.stock') if os.path.isdir(os.path.join(libs, '3.3.10.stock')) else os.path.join(libs, '3.3.10')
trim = os.path.join(root, '.tools', 'esp32-libs-cydpod-bt')
NAME = 'esp32-libs-3.3.10-cydpod-bt.zip'

def sha(p):
    h = hashlib.sha256()
    with open(p, 'rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''): h.update(b)
    return h.hexdigest()

def files(base):
    for d, _, names in os.walk(base):
        for n in names:
            p = os.path.join(d, n)
            yield os.path.relpath(p, base).replace(os.sep, '/'), p

s, t = dict(files(stock)), dict(files(trim))
assert set(s) == set(t), 'packages differ in their file lists'
changed = sorted(r for r in s if sha(s[r]) != sha(t[r]))
assert 'lib/libbt.a' in changed and 'versions.txt' in changed, changed
os.makedirs(out, exist_ok=True)
archive = os.path.join(out, NAME)
with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
    for r in changed:
        info = zipfile.ZipInfo(r, date_time=(2026, 1, 1, 0, 0, 0))
        info.compress_type = zipfile.ZIP_DEFLATED
        z.writestr(info, open(t[r], 'rb').read())
patches = os.path.join(root, 'tools', 'bluedroid', 'patches')
entry = {
    'name': 'esp32-libs-cydpod-bt', 'base': 'esp32-libs 3.3.10', 'archive': NAME, 'sha256': sha(archive),
    'espIdf': 'v5.5.4 735507283d', 'toolchain': 'esp-14.2.0_20260121',
    'defconfigSha256': sha(os.path.join(root, 'tools', 'bluedroid', 'defconfig.cydpod-bt')),
    'patches': {n: sha(os.path.join(patches, n)) for n in sorted(os.listdir(patches))},
    'files': {r: {'stock': sha(s[r]), 'cydpod': sha(t[r])} for r in changed},
    'howBuilt': 'tools/bluedroid/README.md',
}
lock_path = os.path.join(root, 'dependencies.lock.json')
lock = json.load(open(lock_path, encoding='utf-8'))
lock['bluedroidOverlay'] = entry
open(lock_path, 'w', encoding='utf-8', newline='\n').write(json.dumps(lock, indent=2, ensure_ascii=False) + '\n')
print('PASS overlay', NAME, entry['sha256'][:16], len(changed), 'files:', ', '.join(changed))
