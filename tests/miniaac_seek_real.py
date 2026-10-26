"""Read-only representative local music, explicit optional hardware-independent test."""
from pathlib import Path
import subprocess,sys
folder=Path(sys.argv[1]); limit=int(sys.argv[2]) if len(sys.argv)>2 else 5
files=sorted(folder.glob('*.m4a'))[:limit]
if not files: raise SystemExit('SKIP no local media (not a pass)')
subprocess.run(['tests/out/miniaac_seek/test',*[str(p) for p in files]],check=True)
