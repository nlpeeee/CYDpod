"""Stage only the selected FLAC core; --legacy retains the pinned reader overlay."""
from pathlib import Path
import re
import sys
root = Path(__file__).resolve().parent.parent
dest = Path(sys.argv[1])
dest.mkdir(parents=True, exist_ok=True)
if '--legacy' not in sys.argv[2:]:
    for name in ('flac_core.c', 'flac_bits.h', 'miniflac.h'):
        (dest / name).unlink(missing_ok=True)
    print('PASS compact FLAC staged; legacy core/header excluded')
    raise SystemExit(0)
for name in ('flac_compact.c', 'flac_compact.h'):
    (dest / name).unlink(missing_ok=True)
src = (root / 'vendor/miniflac/miniflac.h').read_text()
reader = (root / 'firmware/cydpod_player/app/flac_bits.h').read_text()
for name in ('fill', 'read'):
    pattern = r'MINIFLAC_PRIVATE\n(?:int|uint64_t)\nminiflac_bitreader_' + name + r'\([^\n]+\) \{.*?\n\}'
    if name == 'fill':
        replacement = reader[:reader.index('// Wide metadata fields')]
    else:
        replacement = reader[reader.index('// Wide metadata fields'):]
    src, count = re.subn(pattern, lambda _: replacement, src, flags=re.S)
    if count != 1:
        raise SystemExit('miniflac overlay mismatch: ' + name)
dest = Path(sys.argv[1])
dest.mkdir(parents=True, exist_ok=True)
(dest / 'miniflac.h').write_text(src)
print('PASS pinned miniflac + app reader overlay staged')
