"""Apply the pinned trimmed-Bluedroid overlay to a restored Windows build kit.

Checks the overlay archive and every replaced stock file against
dependencies.lock.json, keeps Espressif's package as esp32-libs/3.3.10.stock,
and leaves the trimmed package both live (esp32-libs/3.3.10) and in
.tools/esp32-libs-cydpod-bt, so tools/bluedroid/use-libs.ps1 can switch back.
"""
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import sys
from zipfile import ZipFile

def sha(data):
    return hashlib.sha256(data).hexdigest()

source = Path(sys.argv[1]).resolve(strict=True)
entry = json.loads((source/'dependencies.lock.json').read_text(encoding='utf-8'))['bluedroidOverlay']
archive = Path(__file__).resolve().parent/entry['archive']
assert sha(archive.read_bytes())==entry['sha256'], 'Bluedroid overlay archive does not match source lock'
libs = source/'.deps/arduino/packages/esp32/tools/esp32-libs'
live, stock, trim = libs/'3.3.10', libs/'3.3.10.stock', source/'.tools/esp32-libs-cydpod-bt'
assert live.is_dir() and not live.is_symlink(), 'esp32-libs 3.3.10 is missing'
assert not stock.exists() and not trim.exists(), 'Bluedroid overlay already applied'
with ZipFile(archive) as package:
    names = sorted(item.filename for item in package.infolist() if not item.is_dir())
    assert names==sorted(entry['files']), 'overlay archive and lock list different files'
    for name in names:
        parts = PurePosixPath(name).parts
        assert '..' not in parts and not PurePosixPath(name).is_absolute()
        assert sha((live.joinpath(*parts)).read_bytes())==entry['files'][name]['stock'], 'unexpected stock file '+name
        assert sha(package.read(name))==entry['files'][name]['cydpod'], 'overlay file mismatch '+name
    shutil.copytree(live, trim)
    for name in names:
        trim.joinpath(*PurePosixPath(name).parts).write_bytes(package.read(name))
live.rename(stock)
shutil.copytree(trim, live)
print('PASS trimmed Bluedroid overlay applied (%d files); Espressif package kept as 3.3.10.stock.' % len(names))
