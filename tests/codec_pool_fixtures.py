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
   if p.exists() and rate!=48000:continue
   output=str(p)
   if exe.lower().endswith('.exe') and sys.platform!='win32':output=subprocess.check_output(['wslpath','-w',str(p.resolve())],text=True).strip()
   cmd=[exe,'-v','error','-f','lavfi','-i',f'sine=frequency=997:sample_rate={rate}:duration={1 if rate==48000 else .2}','-ac',str(channels),'-c:a','libmp3lame']
   cmd+=['-q:a','5'] if mode=='vbr' else ['-b:a','128k' if rate>=32000 else '32k']
   subprocess.run(cmd+['-y',output],check=True)

for channels in (1,2):
 p=out/f'48000-{channels}.m4a'
 # Refresh older 0.2-second cached fixtures before measuring tone quality.
 output=str(p)
 if exe.lower().endswith('.exe') and sys.platform!='win32':output=subprocess.check_output(['wslpath','-w',str(p.resolve())],text=True).strip()
 subprocess.run([exe,'-v','error','-f','lavfi','-i','sine=frequency=997:sample_rate=48000:duration=1','-ac',str(channels),'-c:a','aac','-y',output],check=True)

# A longer CBR file with no Xing/Info frame, for the production seek task.
p=Path('tests/out/miniaac_seek/fixtures/seek.mp3')
output=str(p)
if exe.lower().endswith('.exe') and sys.platform!='win32':output=subprocess.check_output(['wslpath','-w',str(p.resolve())],text=True).strip()
subprocess.run([exe,'-v','error','-f','lavfi','-i','sine=frequency=997:sample_rate=44100:duration=2','-ac','2','-c:a','libmp3lame','-b:a','128k','-write_xing','0','-y',output],check=True)

# Stereo separation and non-block-aligned EOF on the production WAV/FIR path.
import wave,math,struct
with wave.open(str(out/'48000-tone.wav'),'wb') as w:
 w.setparams((2,2,48000,0,'NONE','not compressed'))
 w.writeframes(b''.join(struct.pack('<hh',round(12000*math.sin(2*math.pi*997*i/48000)),round(6000*math.sin(2*math.pi*1499*i/48000))) for i in range(48000+137)))
