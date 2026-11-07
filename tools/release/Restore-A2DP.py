"""Apply the pinned 0.5.1 library overlay to a restored Windows build kit."""
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import sys
from zipfile import ZipFile

source = Path(sys.argv[1]).resolve(strict=True)
archive = Path(__file__).resolve().parent / 'ESP32-A2DP-1.8.12.zip'
library = next(item for item in json.loads((source/'dependencies.lock.json').read_text(encoding='utf-8'))['libraries'] if item['name']=='ESP32-A2DP')
assert library['version']=='1.8.12'
assert hashlib.sha256(archive.read_bytes()).hexdigest()==library['sha256'], 'A2DP archive does not match source lock'
target = source/'.deps/sketchbook/libraries/ESP32-A2DP'
resolved = target.resolve()
assert source in resolved.parents and resolved.name=='ESP32-A2DP' and not target.is_symlink(), 'Unsafe library overlay target'
with ZipFile(archive) as package:
    roots={PurePosixPath(item.filename).parts[0] for item in package.infolist()}
    assert len(roots)==1 and next(iter(roots))=='ESP32-A2DP-'+library['commit']
    files=[]
    for item in package.infolist():
        parts=PurePosixPath(item.filename).parts[1:]
        assert '..' not in parts and not PurePosixPath(item.filename).is_absolute()
        if item.is_dir(): continue
        dest=target.joinpath(*parts)
        assert resolved in dest.resolve().parents
        files.append((item,dest))
    if target.exists(): shutil.rmtree(target)
    for item,dest in files:
        dest.parent.mkdir(parents=True,exist_ok=True)
        dest.write_bytes(package.read(item))
print('PASS pinned ESP32-A2DP 1.8.12 overlay restored.')
