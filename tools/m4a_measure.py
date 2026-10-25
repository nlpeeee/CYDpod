# Read-only measurement of M4A box sizes: python -I tools/m4a_measure.py <dir>
import os, struct, sys
CONT = {b'moov', b'trak', b'mdia', b'minf', b'stbl', b'edts', b'dinf', b'udta', b'ilst'}
def walk(f, pos, end, depth, out, top=False):
    while pos + 8 <= end:
        f.seek(pos); h = f.read(16)
        size, typ = struct.unpack('>I4s', h[:8]); hdr = 8
        if size == 1: size = struct.unpack('>Q', h[8:16])[0]; hdr = 16
        elif size == 0: size = end - pos
        if size < hdr: break
        out['depth'] = max(out['depth'], depth)
        if typ == b'meta' and depth >= 1: walk(f, pos + hdr + 4, pos + size, depth + 1, out)
        elif typ in CONT: walk(f, pos + hdr, pos + size, depth + 1, out)
        elif typ == b'stsd': out['stsd'] = size - hdr
        elif typ == b'stsz':
            f.seek(pos + hdr); v, ss, n = struct.unpack('>III', f.read(12))
            out['fixed'] = ss; out['count'] = n; out['stszbox'] = size - hdr
            if ss == 0:
                t = struct.unpack('>%dI' % n, f.read(4 * n)); out['max'] = max(t); out['min'] = min(t)
        elif typ == b'mdat': out['mdat_at'] = pos
        elif typ == b'moov': pass
        if typ in (b'moov', b'udta', b'meta', b'free', b'ftyp', b'mdat') and depth <= 1:
            out.setdefault('top', []).append((typ.decode('latin1'), size))
        pos += size
d = sys.argv[1]
print('file\tcount\tmaxframe\tminframe\tstsd\tmoov\tudta/meta\tdepth\tmoov_first')
for n in sorted(os.listdir(d)):
    if not n.lower().endswith('.m4a'): continue
    o = {'depth': 0}
    with open(os.path.join(d, n), 'rb') as f:
        f.seek(0, 2); walk(f, 0, f.tell(), 0, o)
    top = {t: s for t, s in o.get('top', [])}
    print('\t'.join(str(x) for x in [n[:40], o.get('count'), o.get('max'), o.get('min'), o.get('stsd'), top.get('moov'), top.get('udta', top.get('meta')), o['depth'], 'moov' in top and o['mdat_at'] > 0]))
