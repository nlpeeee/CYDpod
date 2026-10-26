"""Verify pristine MiniAAC and its overlay; stage without changing vendor/."""
from pathlib import Path
import hashlib, json, shutil, subprocess, os
r=Path(__file__).resolve().parent.parent
src=r/'vendor/miniaac'; out=r/'.tools/staging/miniaac-patched'
inventory=json.loads((r/'tools/miniaac-files.sha256.json').read_text(encoding='utf-8-sig'))
actual={p.relative_to(src).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in src.rglob('*') if p.is_file()}
if actual!=inventory: raise SystemExit('MiniAAC pristine inventory mismatch')
patches=r/'patches/miniaac'
manifest=json.loads((patches/'manifest.json').read_text())
if set(manifest)!=set(p.name for p in patches.glob('*.patch')): raise SystemExit('Overlay inventory mismatch')
for name,digest in manifest.items():
    if hashlib.sha256((patches/name).read_bytes().replace(b'\r\n',b'\n')).hexdigest()!=digest: raise SystemExit('Overlay hash mismatch: '+name)
if out.is_symlink() or out.resolve().parent!=(r/'.tools/staging').resolve(): raise SystemExit('Unsafe staging path')
if out.exists(): shutil.rmtree(out)
shutil.copytree(src,out)
# Checkout may mix CRLF/LF; normalize only the disposable staged patch targets.
for relative in ("src/miniaac.c", "src/miniaac.h"):
    target=out/relative; target.write_bytes(target.read_bytes().replace(b"\r\n",b"\n"))
env=dict(os.environ, GIT_CEILING_DIRECTORIES=str(out.parent))
for name in sorted(manifest):
    patch=(patches/name).read_bytes().replace(b'\r\n',b'\n')
    subprocess.run(['git','apply','--check','-'],input=patch,cwd=out,env=env,check=True)
    subprocess.run(['git','apply','-'],input=patch,cwd=out,env=env,check=True)
print('PASS pristine MiniAAC + verified seek overlay staged')
