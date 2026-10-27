"""Generate short synthetic MP3 corpus using the already installed ffmpeg."""
from pathlib import Path
import shutil,subprocess,sys
out=Path('tests/out/codec_pool');out.mkdir(parents=True,exist_ok=True)
exe=shutil.which('ffmpeg') or shutil.which('ffmpeg.exe')
if not exe: raise SystemExit('Existing FFmpeg required')
for rate in (48000,44100,32000,24000,22050,16000,12000,11025,8000):
 for channels in (1,2):
  for mode in ('cbr','vbr'):
   p=out/f'{rate}-{channels}-{mode}.mp3'
   if p.exists():continue
   output=str(p)
   if exe.lower().endswith('.exe') and sys.platform!='win32':output=subprocess.check_output(['wslpath','-w',str(p.resolve())],text=True).strip()
   cmd=[exe,'-v','error','-f','lavfi','-i',f'sine=frequency=997:sample_rate={rate}:duration=0.2','-ac',str(channels),'-c:a','libmp3lame']
   cmd+=['-q:a','5'] if mode=='vbr' else ['-b:a','128k' if rate>=32000 else '32k']
   subprocess.run(cmd+['-y',output],check=True)

for channels in (1,2):
 p=out/f'48000-{channels}.m4a'
 if not p.exists():
  output=str(p)
  if exe.lower().endswith('.exe') and sys.platform!='win32':output=subprocess.check_output(['wslpath','-w',str(p.resolve())],text=True).strip()
  subprocess.run([exe,'-v','error','-f','lavfi','-i','sine=frequency=997:sample_rate=48000:duration=0.2','-ac',str(channels),'-c:a','aac','-y',output],check=True)

# A longer CBR file with no Xing/Info frame, for the production seek task.
p=Path('tests/out/miniaac_seek/fixtures/seek.mp3')
output=str(p)
if exe.lower().endswith('.exe') and sys.platform!='win32':output=subprocess.check_output(['wslpath','-w',str(p.resolve())],text=True).strip()
subprocess.run([exe,'-v','error','-f','lavfi','-i','sine=frequency=997:sample_rate=44100:duration=2','-ac','2','-c:a','libmp3lame','-b:a','128k','-write_xing','0','-y',output],check=True)

