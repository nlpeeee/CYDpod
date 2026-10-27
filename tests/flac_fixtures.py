"""Generate FLAC fixtures with a tiny pure-Python encoder (no flac/ffmpeg needed).

Writes tests/out/flac/<name>.flac, <name>.pcm (expected 16-bit interleaved PCM, little endian)
and tests/out/flac/manifest.tsv:  name  status  rate  channels  frames  detail
status: ok | reject | skip (corrupt frame dropped, rest exact) | truncated (error after prefix).
"""
import os, struct, sys

OUT = 'tests/out/flac'
os.makedirs(OUT, exist_ok=True)

def crc8(data):
    c = 0
    for b in data:
        c ^= b
        for _ in range(8):
            c = ((c << 1) ^ 0x07) & 0xFF if c & 0x80 else (c << 1) & 0xFF
    return c

def crc16(data):
    c = 0
    for b in data:
        c ^= b << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x8005) & 0xFFFF if c & 0x8000 else (c << 1) & 0xFFFF
    return c

class Bits:
    def __init__(self): self.v = 0; self.n = 0
    def put(self, value, bits):
        self.v = (self.v << bits) | (value & ((1 << bits) - 1)); self.n += bits
    def signed(self, value, bits): self.put(value & ((1 << bits) - 1), bits)
    def align(self):
        if self.n % 8: self.put(0, 8 - self.n % 8)
    def bytes(self):
        self.align(); return self.v.to_bytes(self.n // 8, 'big')

def utf8(n):
    if n < 0x80: return bytes([n])
    for count, lead in ((2, 0xC0), (3, 0xE0), (4, 0xF0), (5, 0xF8), (6, 0xFC), (7, 0xFE)):
        if n < 1 << (5 * count + 1 if count < 7 else 36):
            out = [0x80 | ((n >> (6 * i)) & 0x3F) for i in range(count - 2, -1, -1)]
            return bytes([lead | (n >> (6 * (count - 1)))] + out)
    raise ValueError

def rice(bits, residual, order, blocksize, poorder):
    bits.put(0, 2); bits.put(poorder, 4)
    parts = 1 << poorder; at = 0
    for p in range(parts):
        n = (blocksize >> poorder) - (order if p == 0 else 0)
        chunk = residual[at:at + n]; at += n
        best = None
        for k in range(15):
            cost = sum(((v << 1) ^ (v >> 63)) >> k for v in map(lambda x: x, chunk)) + n * (k + 1)
            if best is None or cost < best[0]: best = (cost, k)
        k = best[1]; bits.put(k, 4)
        for v in chunk:
            u = (v << 1) if v >= 0 else ((-v << 1) - 1)
            bits.put(0, u >> k); bits.put(1, 1); bits.put(u & ((1 << k) - 1), k) if k else None

def subframe(bits, x, bps, kind, blocksize):
    if kind == 'constant' and len(set(x)) == 1:
        bits.put(0, 8); bits.signed(x[0], bps); return
    if kind.startswith('fixed'):
        order = int(kind[5:])
        res = list(x[:order])
        d = list(x)
        for _ in range(order): d = [d[i] - d[i - 1] for i in range(1, len(d))]
        bits.put(0, 1); bits.put(8 + order, 6); bits.put(0, 1)
        for v in x[:order]: bits.signed(v, bps)
        po = 2 if blocksize % 4 == 0 and blocksize // 4 > order else 0
        rice(bits, d, order, blocksize, po); return
    if kind == 'lpc2':
        coefs = [2, -1]; shift = 0; order = 2
        bits.put(0, 1); bits.put(32 + order - 1, 6); bits.put(0, 1)
        for v in x[:order]: bits.signed(v, bps)
        bits.put(3, 4); bits.put(shift, 5)
        for c in coefs: bits.signed(c, 4)
        d = [x[i] - ((coefs[0] * x[i - 1] + coefs[1] * x[i - 2]) >> shift) for i in range(order, len(x))]
        rice(bits, d, order, blocksize, 0); return
    bits.put(0, 1); bits.put(1, 6); bits.put(0, 1)  # verbatim
    for v in x: bits.signed(v, bps)

BS_CODE = {192: 1, 576: 2, 1152: 3, 2304: 4, 4608: 5, 256: 8, 512: 9, 1024: 10, 2048: 11, 4096: 12, 8192: 13}
RATE_CODE = {44100: 9, 48000: 10, 96000: 11, 32000: 8}

def frame(chans, start, bs, rate, bps, number, variable, assign, kinds, strategy_bs_code=True, lie_bs=None, rate_code=True):
    hdr = Bits()
    hdr.put(0x3FFE, 14); hdr.put(0, 1); hdr.put(1 if variable else 0, 1)
    code = BS_CODE.get(bs) if strategy_bs_code else None
    if code is None:
        code = 6 if bs <= 256 else 7
    if lie_bs: code = BS_CODE[lie_bs]
    hdr.put(code, 4)
    hdr.put(RATE_CODE.get(rate, 0) if rate_code else 0, 4)
    hdr.put(assign, 4)
    hdr.put({16: 4, 24: 6}[bps] if bps in (16, 24) else 0, 3); hdr.put(0, 1)
    h = hdr.bytes() + utf8(start if variable else number)
    if code == 6: h += bytes([bs - 1])
    if code == 7: h += struct.pack('>H', bs - 1)
    if code == 0 and rate_code is None: pass
    h += bytes([crc8(h)])
    sub = Bits()
    ch = [c[start:start + bs] for c in chans]
    if assign == 8: ch = [ch[0], [a - b for a, b in zip(ch[0], ch[1])]]
    elif assign == 9: ch = [[a - b for a, b in zip(ch[0], ch[1])], ch[1]]
    elif assign == 10: ch = [[(a + b) >> 1 for a, b in zip(ch[0], ch[1])], [a - b for a, b in zip(ch[0], ch[1])]]
    for i, x in enumerate(ch):
        side = (assign == 8 and i == 1) or (assign == 9 and i == 0) or (assign == 10 and i == 1)
        subframe(sub, x, bps + (1 if side else 0), kinds[i % len(kinds)], bs)
    body = h + sub.bytes()
    return body + struct.pack('>H', crc16(body))

def block(btype, last, data):
    return bytes([(0x80 if last else 0) | btype]) + len(data).to_bytes(3, 'big') + data

def streaminfo(minb, maxb, rate, ch, bps, total):
    v = (rate << 44) | ((ch - 1) << 41) | ((bps - 1) << 36) | total
    return struct.pack('>HH', minb, maxb) + b'\x00\x00\x00\x00\x00\x00' + v.to_bytes(8, 'big') + bytes(16)

def signal(rate, ch, n, seed):
    state = seed; out = [[] for _ in range(ch)]
    import math
    for i in range(n):
        for c in range(ch):
            state = (state * 1103515245 + 12345) & 0x7FFFFFFF
            noise = (state >> 8) % 601 - 300
            v = int(12000 * math.sin(2 * math.pi * (997 + 130 * c) * i / rate)) + noise
            if 2000 <= i < 2400: v = 0                       # constant subframes
            if 3000 <= i < 3200: v = 32767 if (i + c) % 2 == 0 else -32768  # full-scale, exercises side channel
            out[c].append(max(-32768, min(32767, v)))
    return out

def build(name, rate, ch, bs, n, status='ok', seekpoints=0, tags=0, variable=False, bps=16, sbs=None, mutate=None, assigns=None, detail='', lie=None, streaminfo_max=None, streaminfo_min=None):
    data = signal(rate, ch, n, 7 + len(name))
    kinds_cycle = ['fixed2', 'lpc2', 'fixed1', 'verbatim', 'fixed3', 'fixed0', 'fixed4', 'constant']
    frames = []; offsets = []; pos = 0; idx = 0
    sizes = []
    while pos < n:
        b = min(bs, n - pos)
        if variable and idx % 2: b = min(max(16, bs // 2 + (idx * 37) % 300), n - pos)
        sizes.append((pos, b)); pos += b; idx += 1
    for i, (start, b) in enumerate(sizes):
        assign = 0 if ch == 1 else (assigns or [1, 8, 9, 10])[i % len(assigns or [1, 8, 9, 10])]
        kinds = [kinds_cycle[(i + j) % len(kinds_cycle)] for j in range(2)]
        if b != bs and not variable: kinds = ['fixed2', 'verbatim']
        raw = frame(data, start, b, rate, bps, i, variable, assign, kinds, strategy_bs_code=(b == bs or variable) and b in BS_CODE,
                    lie_bs=lie if i == 0 else None, rate_code=(i % 2 == 0))
        frames.append(raw)
    blocks = []
    mins = min(b for _, b in sizes[:-1]) if len(sizes) > 1 else sizes[0][1]
    maxs = max(b for _, b in sizes)
    if not variable: mins = maxs = bs
    si = streaminfo(streaminfo_min or mins, streaminfo_max or maxs, rate, ch, bps, n)
    meta = [(0, si)]
    if seekpoints:
        table = b''; off = 0; taken = 0
        for i, (start, b) in enumerate(sizes):
            if i % seekpoints == 0: table += struct.pack('>QQH', start, off, b)
            off += len(frames[i])
        table += struct.pack('>QQH', 0xFFFFFFFFFFFFFFFF, 0, 0)  # placeholder entry
        meta.append((3, table))
    if tags:
        vendor = b'cydpod-test'; comment = b'COMMENT=' + b'x' * tags
        meta.append((4, struct.pack('<I', len(vendor)) + vendor + struct.pack('<I', 1) + struct.pack('<I', len(comment)) + comment))
        pic = struct.pack('>II', 3, 9) + b'image/png' + struct.pack('>I', 3) + b'cov' + struct.pack('>IIIII', 1, 1, 24, 0, tags * 2) + b'\x89' * (tags * 2)
        meta.append((6, pic)); meta.append((1, bytes(2048)))
    if bps != 16:  # 24-bit fixtures use verbatim only; keep the encoder honest
        pass
    out = b'fLaC' + b''.join(block(t, i == len(meta) - 1, d) for i, (t, d) in enumerate(meta)) + b''.join(frames)
    pcm = bytearray()
    for i in range(n):
        for c in range(ch): pcm += struct.pack('<h', data[c][i] if bps == 16 else data[c][i])
    frame_info = (sizes, [len(f) for f in frames])
    if mutate: out, pcm = mutate(out, pcm, frame_info, ch, len(out) - sum(len(f) for f in frames))
    open(f'{OUT}/{name}.flac', 'wb').write(out)
    open(f'{OUT}/{name}.pcm', 'wb').write(bytes(pcm))
    return (name, status, rate, ch, len(pcm) // (2 * ch), detail)

rows = []
# Positive matrix: 44.1/48 kHz, mono/stereo, block 1152/2048/4096, different stereo modes and subframe types.
for rate in (44100, 48000):
    for ch in (1, 2):
        for bs in (1152, 2048, 4096):
            rows.append(build(f'{rate}-{ch}-{bs}', rate, ch, bs, bs * 3 + 517))
rows.append(build('stereo-tags-table', 44100, 2, 4096, 4096 * 12 + 100, seekpoints=3, tags=40000, detail='seektable+big tags'))
rows.append(build('notable-long', 48000, 2, 4096, 4096 * 40 + 11, detail='no seektable'))
rows.append(build('table-long', 48000, 2, 2048, 2048 * 90 + 33, seekpoints=16, detail='seektable'))
rows.append(build('variable-long', 44100, 2, 2304, 44100 * 5, variable=True, detail='variable block size, sample numbers'))
rows.append(build('fixed-minmax', 44100, 2, 2048, 2048 * 60 + 33, streaminfo_min=1024, detail='fixed strategy, STREAMINFO min != max, no seektable'))
rows.append(build('mono-long', 44100, 1, 1152, 44100 * 3, seekpoints=40, detail='mono'))
# Rejects.
rows.append(build('reject-24bit', 44100, 2, 1152, 3000, status='reject:flac_bits', bps=24))
rows.append(build('reject-block8192', 44100, 2, 8192, 8192 * 2 + 5, status='reject:flac_block_size'))
rows.append(build('reject-3ch', 44100, 2, 1152, 3000, status='reject:flac_channels',
                  mutate=lambda o, p, i, ch, m: (o[:20] + bytes([o[20] & 0xF1 | (2 << 1)]) + o[21:], p)))
rows.append(build('reject-96k', 96000, 2, 2048, 2048 * 2, status='reject:flac_sample_rate'))
rows.append(build('reject-frame-8192', 44100, 2, 4096, 4096 * 2, status='stop:flac_block_size', lie=8192, streaminfo_max=4096))
# Truncated final frame, corrupt middle frame (CRC-16 mismatch), corrupt then clean end.
def trunc(o, p, info, ch, meta):
    return o[:-40], p
rows.append(build('truncated', 44100, 2, 1152, 1152 * 6 + 100, status='truncated:flac_truncated', mutate=trunc))
def corrupt(o, p, info, ch, meta):
    sizes, lens = info; k = 3
    at = len(o) - sum(lens) + sum(lens[:k])
    o = bytearray(o); o[at + lens[k] // 2] ^= 0x55
    start, b = sizes[k]
    p = bytearray(p); del p[start * 2 * ch:(start + b) * 2 * ch]
    return bytes(o), p
rows.append(build('corrupt-frame', 48000, 2, 1152, 1152 * 8 + 100, status='skip', mutate=corrupt))
with open(f'{OUT}/manifest.tsv', 'w') as m:
    for r in rows: m.write('\t'.join(map(str, r)) + '\n')
print(f'generated {len(rows)} FLAC fixtures in {OUT}')
