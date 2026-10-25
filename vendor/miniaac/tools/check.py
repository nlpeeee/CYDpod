"""One-command host check: build, API checks, generated audio and optional sample.
Run from a compiler-enabled shell with CMake, FFmpeg and numpy installed.
All generated files are confined to --build (default: ./build).
"""
import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--build',type=Path,default=Path('build'))
    ap.add_argument('--sample',type=Path)
    ap.add_argument('--cmake',default='cmake')
    ap.add_argument('--generator')
    args=ap.parse_args(); root=Path(__file__).resolve().parents[1]
    build=args.build.resolve()
    def checked(cmd): subprocess.run([str(x) for x in cmd],check=True)
    configure=[args.cmake,'-S',root,'-B',build,'-DCMAKE_BUILD_TYPE=Release']
    if args.generator: configure+=['-G',args.generator]
    checked(configure)
    checked([args.cmake,'--build',build,'--config','Release','-j','4'])
    cmake=Path(shutil.which(args.cmake) or args.cmake)
    ctest=cmake.with_name('ctest.exe' if sys.platform=='win32' else 'ctest')
    checked([ctest,'--test-dir',build,'-C','Release','--output-on-failure'])
    suffix='.exe' if sys.platform=='win32' else ''
    exe=build/('miniaac_decode'+suffix)
    if not exe.exists(): exe=build/'Release'/exe.name
    command=[sys.executable,root/'tests/validate.py','--decoder',exe,'--work',build/'validation']
    if args.sample: command+=['--sample',args.sample.resolve()]
    p=subprocess.run([str(x) for x in command],capture_output=True,text=True)
    if p.returncode:
        print(p.stdout); print(p.stderr,file=sys.stderr)
        raise SystemExit(p.returncode)
    result=json.loads(p.stdout)
    print(f"PASS: API checks; {result['passed_audio_cases']} full audio comparisons; "
          f"{result['passed_negative_cases']} rejection cases.")
    print('Evidence:',build/'validation/validation.json')

if __name__=='__main__': main()
