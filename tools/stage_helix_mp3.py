"""Stage only the pinned MP3 C core, byte-for-byte; never edit Helix sources."""
from pathlib import Path
import hashlib,json,shutil
r=Path(__file__).resolve().parents[1]
src=r/'.deps/sketchbook/libraries/arduino-libhelix/src/libhelix-mp3'
out=r/'.tools/staging/helix-mp3-core'
expected=json.loads((r/'tools/helix-mp3-files.sha256.json').read_text())
actual={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in src.iterdir() if p.is_file()}
if actual!=expected:raise SystemExit('Pinned Helix MP3 inventory mismatch')
if out.is_symlink() or out.resolve().parent!=(r/'.tools/staging').resolve():raise SystemExit('Unsafe MP3 staging path')
if out.exists():shutil.rmtree(out)
(out/'src/libhelix-mp3').mkdir(parents=True)
for name in expected:
 if name!='buffers.c':shutil.copy2(src/name,out/'src/libhelix-mp3'/name)
(out/'library.properties').write_text('name=CYDpodHelixMP3Core\nversion=0.9.4\nauthor=RealNetworks\nmaintainer=CYDpod\nsentence=Pinned MP3-only C core\nparagraph=Byte-identical core, buffers.c excluded in favour of app pool slices\ncategory=Signal Input/Output\narchitectures=*\n')
(out/'src/cydpod_helix_mp3.h').write_text('#pragma once\n#include "libhelix-mp3/mp3dec.h"\n')
for name in expected:
 if name!='buffers.c':assert hashlib.sha256((out/'src/libhelix-mp3'/name).read_bytes()).hexdigest()==expected[name]
print('PASS pinned Helix MP3-only core staged unchanged; heap buffers.c excluded')
