"""Stage the pinned miniflac header with the app-owned reader overlay only."""
from pathlib import Path
import re
import sys
root = Path(__file__).resolve().parent.parent
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
