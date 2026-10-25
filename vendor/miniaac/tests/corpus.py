"""Read-only music-corpus validation. Only two temporary PCM files are kept.
The resulting JSON contains file-relative names and measurements, no audio.
Usage: python tests/corpus.py --decoder build/miniaac_decode --root /music --work build/corpus
"""
import argparse
import json
import subprocess
import time
from pathlib import Path
import numpy as np

def run(cmd):
    return subprocess.run([str(x) for x in cmd],capture_output=True,text=True)

def pcm_error(actual,reference,channels):
    n=actual.stat().st_size//2; m=reference.stat().st_size//2
    if n>m or m-n>=1024*channels:
        raise AssertionError(f'PCM length mismatch: {n} vs {m} values')
    energy=0.;noise=0.;maximum=0.;count=0
    with actual.open('rb') as a, reference.open('rb') as b:
        while True:
            raw=a.read(512*1024)
            if not raw: break
            x=np.frombuffer(raw,dtype='<i2').astype(np.float64)
            y=np.frombuffer(b.read(len(raw)),dtype='<i2').astype(np.float64)
            e=x-y
            energy+=float(np.sum(y*y));noise+=float(np.sum(e*e))
            maximum=max(maximum,float(np.max(np.abs(e))));count+=len(x)
    if not count: raise AssertionError('No PCM returned')
    snr=float(10*np.log10(max(energy,1e-20)/max(noise,1e-20)))
    rmse=float(np.sqrt(noise/count))
    if snr<=60 and rmse>1: raise AssertionError(f'Low PCM agreement: SNR={snr:.2f}dB, RMS={rmse:.2f} LSB')
    return {'snr_db':snr,'rmse_lsb':rmse,'max_error_lsb':maximum,'reference_frames':m//channels}

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--decoder',type=Path,required=True)
    ap.add_argument('--root',type=Path,required=True)
    ap.add_argument('--work',type=Path,required=True)
    args=ap.parse_args(); work=args.work.resolve();work.mkdir(parents=True,exist_ok=True)
    files=sorted(p for p in args.root.rglob('*') if p.suffix.lower() in {'.m4a','.aac'} and p.is_file())
    rows=[];start=time.monotonic()
    actual=work/'_miniaac.pcm';reference=work/'_reference.pcm'
    for i,src in enumerate(files,1):
        row={'file':str(src.relative_to(args.root))}
        try:
            probe=run(['ffprobe','-v','error','-select_streams','a:0','-show_entries',
                       'stream=codec_name,profile,sample_rate,channels,duration,bit_rate','-of','json',src])
            if probe.returncode: raise AssertionError(probe.stderr)
            stream=json.loads(probe.stdout)['streams'][0];row['stream']=stream
            p=run([args.decoder.resolve(),src,actual])
            if p.returncode: raise AssertionError(p.stderr.strip() or p.stdout.strip())
            stats=json.loads(p.stdout);row.update(stats)
            if stats['rate']!=int(stream['sample_rate']) or stats['channels']!=stream['channels']:
                raise AssertionError('Source/decoded rate or channel mismatch')
            if stats['declared_frames']!=2**64-1 and stats['frames']!=stats['declared_frames']:
                raise AssertionError('Declared/decoded frame mismatch')
            ref=run(['ffmpeg','-v','error','-i',src,'-map','0:a:0','-f','s16le','-y',reference])
            if ref.returncode: raise AssertionError(ref.stderr)
            row.update(pcm_error(actual,reference,stats['channels']))
            row['passed']=True
        except Exception as exc:
            row['passed']=False;row['error']=str(exc)
            print(f'FAIL {i}/{len(files)}: {row["file"]}: {exc}',flush=True)
        finally:
            for p in (actual,reference):
                if p.exists(): p.unlink()
        rows.append(row)
        passed=sum(r['passed'] for r in rows)
        report={'files':len(files),'tested':len(rows),'passed':passed,'failed':len(rows)-passed,
                'elapsed_seconds':round(time.monotonic()-start,3),'tracks':rows}
        (work/'corpus-report.json').write_text(json.dumps(report,indent=2))
        if i%10==0 or i==len(files):
            print(f'{i}/{len(files)} checked: {passed} passed, {len(rows)-passed} failed; {time.monotonic()-start:.1f}s',flush=True)
    print('Report:',work/'corpus-report.json',flush=True)
    if any(not r['passed'] for r in rows): raise SystemExit(1)

if __name__=='__main__': main()
