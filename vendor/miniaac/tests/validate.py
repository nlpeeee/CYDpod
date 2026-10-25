"""End-to-end checks. Generated audio stays in --work; no music is distributed.
Requires Python 3, numpy, ffmpeg, ffprobe, and the built miniaac_decode executable.
"""
import argparse
import json
import subprocess
from pathlib import Path
import numpy as np

def run(args):
    return subprocess.run([str(x) for x in args], capture_output=True, text=True)

def checked(args):
    p = run(args)
    if p.returncode:
        raise AssertionError(f"{args}\n{p.stdout}\n{p.stderr}")
    return p.stdout

CONTAINERS = {b'moov', b'trak', b'mdia', b'minf', b'stbl', b'edts'}

def boxes(data, begin=0, end=None, parents=()):
    if end is None: end = len(data)
    at = begin
    while at < end:
        size = int.from_bytes(data[at:at+4], 'big')
        kind = bytes(data[at+4:at+8])
        header = 8
        if size == 1:
            size = int.from_bytes(data[at+8:at+16], 'big'); header = 16
        if not size: size = end-at
        assert size >= header and at+size <= end
        yield kind, at, size, header, parents
        if kind in CONTAINERS:
            yield from boxes(data, at+header, at+size, parents+(at,))
        at += size

def locate(data, kind):
    return next(x for x in boxes(data) if x[0] == kind)

def set32(data, at, value):
    data[at:at+4] = value.to_bytes(4, 'big')

def change_box(data, kind, replacement):
    _, at, size, _, parents = locate(data, kind)
    delta = len(replacement)-size
    for parent in parents:
        set32(data, parent, int.from_bytes(data[parent:parent+4], 'big')+delta)
    return data[:at]+replacement+data[at+size:]

def compare(exe, src, work, label):
    pcm = work/(label+'.pcm')
    ref = work/(label+'.reference.pcm')
    stats = json.loads(checked([exe, src, pcm]))
    checked(['ffmpeg','-v','error','-i',src,'-map','0:a:0','-f','s16le','-y',ref])
    x = np.fromfile(pcm, dtype='<i2').astype(np.float64)
    y = np.fromfile(ref, dtype='<i2').astype(np.float64)
    # FFmpeg can retain final AAC-frame padding; M4A output honours stts/elst.
    assert len(x) <= len(y) and len(y)-len(x) < 1024*stats['channels'], (label,len(x),len(y))
    n = len(x); assert n
    error = x-y[:n]
    energy = float(np.dot(y[:n],y[:n])); noise = float(np.dot(error,error))
    snr = float(10*np.log10(energy/max(noise,1e-20)))
    assert snr > 60, (label,snr)
    assert stats['total_component_ram'] <= 48*1024 and stats['max_read'] <= 2048
    if stats['declared_frames'] != 2**64-1:
        assert stats['frames'] == stats['declared_frames']
    stats.update(snr_db=snr,rmse_lsb=float(np.sqrt(np.mean(error*error))),
                 max_error_lsb=float(np.max(np.abs(error))),reference_frames=len(y)//stats['channels'])
    return stats

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--decoder',required=True,type=Path)
    ap.add_argument('--work',required=True,type=Path)
    ap.add_argument('--sample',type=Path)
    args=ap.parse_args(); w=args.work; w.mkdir(parents=True,exist_ok=True)
    exe=args.decoder.resolve(); results={}; negatives={}
    configs=[('stereo-tail',48000,2,3,False,'m4a'),
             ('stereo-front',44100,2,3,True,'m4a'),
             ('mono',32000,1,3,False,'m4a'),
             ('adts',48000,2,3,False,'aac'),
             ('long-20-min',8000,1,1200,False,'m4a')]
    for label,rate,ch,duration,front,ext in configs:
        src=w/(label+'.'+ext)
        cmd=['ffmpeg','-v','error','-f','lavfi','-i',
             f'sine=frequency=997:sample_rate={rate}:duration={duration}',
             '-ac',ch,'-c:a','aac','-b:a','128k' if ch==2 else '32k']
        if front: cmd+=['-movflags','+faststart']
        if ext=='aac': cmd+=['-f','adts']
        checked(cmd+['-y',src])
        results[label]=compare(exe,src,w,label)

    # Convert a tail-moov stco table to co64 without moving audio payload.
    base=bytearray((w/'stereo-tail.m4a').read_bytes())
    _,at,size,h,_=locate(base,b'stco')
    count=int.from_bytes(base[at+h+4:at+h+8],'big')
    offsets=[int.from_bytes(base[at+h+8+i*4:at+h+12+i*4],'big') for i in range(count)]
    payload=bytes(base[at+h:at+h+8])+b''.join(x.to_bytes(8,'big') for x in offsets)
    replacement=(8+len(payload)).to_bytes(4,'big')+b'co64'+payload
    co64=change_box(base.copy(),b'stco',replacement)
    (w/'co64.m4a').write_bytes(co64)
    results['co64']=compare(exe,w/'co64.m4a',w,'co64')

    # Extended-size top-level box.
    # Use size==1 encoding: prepending free shifts the whole file, so append instead.
    extended=(1).to_bytes(4,'big')+b'free'+(16).to_bytes(8,'big')
    (w/'extended.m4a').write_bytes(base+extended)
    results['extended-box']=compare(exe,w/'extended.m4a',w,'extended')

    # Fixed-size stsz: repeat one valid compressed frame, one packet per chunk.
    # Keep the media payload in place so its offsets need no relocation.
    _,sz,_,sh,_=locate(base,b'stsz')
    # Use a steady-tone packet, rather than the almost silent encoder-priming
    # packet (whose tiny energy makes SNR an unsuitable quality metric).
    first_sizes=[int.from_bytes(base[sz+sh+12+i*4:sz+sh+16+i*4],'big') for i in range(4)]
    packet_size=first_sizes[3]
    first_packet=offsets[0]+sum(first_sizes[:3])
    fixed=change_box(base.copy(),b'stsz',
        (20).to_bytes(4,'big')+b'stsz'+bytes(4)+packet_size.to_bytes(4,'big')+(3).to_bytes(4,'big'))
    fixed=change_box(fixed,b'stco',(28).to_bytes(4,'big')+b'stco'+bytes(4)+(3).to_bytes(4,'big')+
                     first_packet.to_bytes(4,'big')*3)
    fixed=change_box(fixed,b'stsc',(28).to_bytes(4,'big')+b'stsc'+bytes(4)+(1).to_bytes(4,'big')+
                     (1).to_bytes(4,'big')*3)
    fixed=change_box(fixed,b'stts',(24).to_bytes(4,'big')+b'stts'+bytes(4)+(1).to_bytes(4,'big')+
                     (3).to_bytes(4,'big')+(1024).to_bytes(4,'big'))
    # Discard the old edit list for this synthetic three-frame fixture.
    _,ed,_,_,_=locate(fixed,b'edts');fixed[ed+4:ed+8]=b'free'
    (w/'fixed-size.m4a').write_bytes(fixed)
    results['fixed-size']=compare(exe,w/'fixed-size.m4a',w,'fixed-size')

    # 64-bit version-1 headers/edit list, using tail-moov to preserve offsets.
    v1=base.copy()
    for kind in (b'mvhd',b'mdhd'):
        _,at,size,h,_=locate(v1,kind);old=bytes(v1[at+h:at+size])
        payload=b'\x01'+old[1:4]+bytes(4)+old[4:8]+bytes(4)+old[8:12]+old[12:16]+bytes(4)+old[16:20]+old[20:]
        v1=change_box(v1,kind,(len(payload)+8).to_bytes(4,'big')+kind+payload)
    _,at,size,h,_=locate(v1,b'elst');old=bytes(v1[at+h:at+size])
    payload=b'\x01'+old[1:8]+bytes(4)+old[8:12]+bytes(4)+old[12:16]+old[16:]
    v1=change_box(v1,b'elst',(len(payload)+8).to_bytes(4,'big')+b'elst'+payload)
    (w/'v1.m4a').write_bytes(v1)
    results['version-1']=compare(exe,w/'v1.m4a',w,'version-1')

    # Add CRC words to an ADTS frame stream. CRC verification is not performed.
    adts=(w/'adts.aac').read_bytes(); crc=bytearray(); at=0
    while at<len(adts):
        length=((adts[at+3]&3)<<11)|(adts[at+4]<<3)|(adts[at+5]>>5)
        packet=bytearray(adts[at:at+length]); packet[1]&=254; new=length+2
        packet[3]=(packet[3]&252)|(new>>11); packet[4]=(new>>3)&255
        packet[5]=(packet[5]&31)|((new&7)<<5)
        crc+=packet[:7]+bytes(2)+packet[7:]; at+=length
    (w/'crc.aac').write_bytes(crc)
    results['adts-crc']=compare(exe,w/'crc.aac',w,'crc')

    def reject(label,data,code):
        src=w/(label+'.m4a'); src.write_bytes(data)
        p=run([exe,src])
        assert p.returncode==1 and f'({code})' in p.stderr, (label,p.returncode,p.stdout,p.stderr)
        negatives[label]=p.stderr.strip()

    reject('truncated',base[:-20],-4)
    broken=change_box(base.copy(),b'stsd',(16).to_bytes(4,'big')+b'stsd'+bytes(4)+(1).to_bytes(4,'big'))
    reject('missing-sample-description',broken,-4)
    broken=base.copy();set32(broken,0,7);reject('short-box',broken,-4)
    _,at,_,h,_=locate(base,b'stco')
    broken=base.copy();set32(broken,at+h+8,len(base)+1);reject('invalid-offset',broken,-4)
    broken=bytearray(co64);_,ca,_,ch,_=locate(broken,b'co64')
    broken[ca+ch+8:ca+ch+16]=(2**32+first_packet).to_bytes(8,'big')
    reject('invalid-64bit-offset',broken,-4)
    _,at,_,h,_=locate(base,b'stsz')
    broken=base.copy();set32(broken,at+h+12,2049);reject('oversized',broken,-6)
    broken=base.copy();set32(broken,at+h+12,0);reject('zero-packet',broken,-4)
    _,at,_,h,_=locate(base,b'stsc')
    broken=base.copy();set32(broken,at+h+8,0);reject('bad-mapping',broken,-4)
    broken=base.copy();at=broken.index(b'mp4a');broken[at:at+4]=b'enca';reject('encrypted',broken,-5)
    # Locate FFmpeg's ASC (the preceding descriptor's variable-length size may vary).
    broken=base.copy();at=broken.index(bytes.fromhex('119056e500'))
    broken[at]=(5<<3)|(broken[at]&7);reject('he-aac',broken,-5)
    broken=base.copy();broken[at+1]|=4;reject('960-samples',broken,-5)
    broken=bytearray(adts);broken[2]&=63;reject('adts-main',broken,-5)
    broken=bytearray(adts);broken[6]|=1;reject('adts-multi-block',broken,-5)
    reject('moof',base+(8).to_bytes(4,'big')+b'moof',-5)
    # Actual fragmented MP4 emitted by FFmpeg, not just a marker.
    frag=w/'fragmented.m4a'
    checked(['ffmpeg','-v','error','-i',w/'stereo-tail.m4a','-c:a','copy',
             '-movflags','frag_keyframe+empty_moov','-y',frag])
    reject('fragmented',frag.read_bytes(),-5)
    # HE-AAC's in-band SBR fill must be rejected even without ASC signalling.
    raw_bits='110'+'0001'+'11010000'+'111'
    raw_bits+='0'*((-len(raw_bits))%8)
    raw=int(raw_bits,2).to_bytes(len(raw_bits)//8,'big')
    length=len(raw)+7
    header=bytes([255,241,0x50,0x80|(length>>11),(length>>3)&255,
                  ((length&7)<<5)|31,252])
    reject('in-band-sbr',header+raw,-5)
    if args.sample:
        results['user-sample']=compare(exe,args.sample,w,'user-sample')
    assert len({x['workspace'] for x in results.values()})==1
    report={'passed_audio_cases':len(results),'passed_negative_cases':len(negatives),
            'audio':results,'negative':negatives}
    (w/'validation.json').write_text(json.dumps(report,indent=2))
    print(json.dumps(report,indent=2))

if __name__=='__main__': main()
