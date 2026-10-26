"""Generated-only audio; structural variants use tail moov without moving mdat."""
from pathlib import Path
import sys, subprocess, struct, shutil
w=Path(sys.argv[1]);w.mkdir(parents=True,exist_ok=True)
def generate(name,duration,front=False,fmt='mp4'):
 p=w/name
 if p.exists(): return
 exe=shutil.which('ffmpeg') or shutil.which('ffmpeg.exe')
 if not exe: raise SystemExit('FFmpeg required to generate synthetic seek fixtures')
 output=str(p)
 if exe.lower().endswith('.exe') and sys.platform!='win32':
  output=subprocess.check_output(['wslpath','-w',str(p.resolve())],text=True).strip()
 cmd=[exe,'-v','error','-f','lavfi','-i',f'aevalsrc=0.3*sin(2*PI*(440+170*sin(t))*t)|0.25*sin(2*PI*997*t):s=44100:d={duration}','-c:a','aac','-b:a','128k']
 if front: cmd+=['-movflags','+faststart']
 subprocess.run(cmd+['-f',fmt,'-y',output],check=True)
generate('tail.m4a',12);generate('front.m4a',12,True);generate('long.m4a',1200);generate('raw.aac',1,fmt='adts')
containers={b'moov',b'trak',b'mdia',b'minf',b'stbl',b'edts'}
def box(t,p): return struct.pack('>I',8+len(p))+t+p
def walk(data,changes):
 result=b'';at=0
 while at<len(data):
  n,t=struct.unpack_from('>I4s',data,at);p=data[at+8:at+n];assert n>=8
  if t in changes: t,p=changes[t](p)
  elif t in containers: p=walk(p,changes)
  result+=box(t,p);at+=n
 return result
base=(w/'tail.m4a').read_bytes()
def co64(p):
 n=struct.unpack_from('>I',p,4)[0];values=struct.unpack_from('>'+str(n)+'I',p,8)
 return b'co64',p[:8]+b''.join(struct.pack('>Q',v) for v in values)
(w/'co64.m4a').write_bytes(walk(base,{b'stco':co64}))
# Gather packet sizes and original first payload offset.
tables={}
def capture(t):
 def f(p):tables[t]=p;return t,p
 return f
walk(base,{t:capture(t) for t in (b'stco',b'stsz')})
sizes=list(struct.unpack_from('>'+str(struct.unpack_from('>I',tables[b'stsz'],8)[0])+'I',tables[b'stsz'],12))
offset=struct.unpack_from('>I',tables[b'stco'],8)[0]
def variant(name,counts):
 offsets=[];at=offset;sample=0;runs=[];previous=0
 for chunk,count in enumerate(counts,1):
  offsets.append(at);at+=sum(sizes[sample:sample+count]);sample+=count
  if count!=previous:runs.append((chunk,count,1));previous=count
 assert sample==len(sizes)
 changes={b'stco':lambda p:(b'stco',p[:4]+struct.pack('>I',len(offsets))+b''.join(struct.pack('>I',x) for x in offsets)),b'stsc':lambda p:(b'stsc',p[:4]+struct.pack('>I',len(runs))+b''.join(struct.pack('>III',*x) for x in runs))}
 (w/name).write_bytes(walk(base,changes))
variant('one-chunk.m4a',[len(sizes)])
variant('multi-run.m4a',[3,5,2,7,len(sizes)-17])
print('PASS generated seek fixtures: front/tail moov, co64, multiple stsc runs, one chunk, 20 minutes, ADTS')

# A valid fixed-size sample table: repeat one encoded packet; no extra index.
packet=base[offset:offset+sizes[0]]
fixed={b'mdat':lambda p:(b'mdat',packet*len(sizes)),
 b'stco':lambda p:(b'stco',p[:4]+struct.pack('>II',1,offset)),
 b'stsc':lambda p:(b'stsc',p[:4]+struct.pack('>IIII',1,1,len(sizes),1)),
 b'stsz':lambda p:(b'stsz',p[:4]+struct.pack('>II',len(packet),len(sizes)))}
(w/'fixed-size.m4a').write_bytes(walk(base,fixed))

# Worst-case variable-size lookup: one chunk spanning a 20-minute file.
base=(w/'long.m4a').read_bytes();tables.clear()
walk(base,{t:capture(t) for t in (b'stco',b'stsz')})
sizes=list(struct.unpack_from('>'+str(struct.unpack_from('>I',tables[b'stsz'],8)[0])+'I',tables[b'stsz'],12))
offset=struct.unpack_from('>I',tables[b'stco'],8)[0]
variant('long-one-chunk.m4a',[len(sizes)])

# Corrupt lookup and mapping tables; rejection must remain bounded and named.
base=(w/'tail.m4a').read_bytes()
(w/'bad-offset.bad').write_bytes(walk(base,{b'stco':lambda p:(b'stco',p[:8]+b'\0'*(len(p)-8))}))
(w/'bad-map.bad').write_bytes(walk(base,{b'stsc':lambda p:(b'stsc',p[:8]+b'\0'*4+p[12:])}))
