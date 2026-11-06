"""Reproducible independent encoder/oracle matrix; no external encoder needed.

Generated streams/PCM/logs live only in ignored tests/out/flacx. Owner originals
are read in place. The unchanged pinned flac_fixtures.py supplies the 21 originals.
"""
from pathlib import Path
import hashlib
import os
import random
import re
import runpy
import struct
import subprocess
import sys

OUT = Path('tests/out/flacx')
OUT.mkdir(parents=True, exist_ok=True)
ENC = runpy.run_path('tests/flac_fixtures.py')
ORIGINAL_SUB = ENC['frame'].__globals__['subframe']
TIMES = {'compact': [0.0, 0], 'reference': [0.0, 0]}


def run(exe, mode, path=None, output=None, extra=(), success=True):
    args = [str(OUT / exe), mode]
    if path is not None:
        args.append(str(path))
    if output is not None:
        args.append(str(output))
    p = subprocess.run(args + list(map(str, extra)), capture_output=True, text=True)
    if success and p.returncode:
        raise RuntimeError(' '.join(args) + '\n' + p.stdout + p.stderr)
    stderr = re.sub(r'^open-file high water \d+ \(limit 4\)\n?', '', p.stderr, flags=re.M)
    if stderr:
        raise RuntimeError('unexpected stderr (includes sanitizer errors): ' + stderr)
    if mode in TIMES:
        t = re.search(r'frames=(\d+).*frame_us=([\d.]+)', p.stdout)
        if t:
            n, us = int(t[1]), float(t[2])
            TIMES[mode][0] += n * us
            TIMES[mode][1] += n
    return p


def verify(path, expected=None, exe='host4608', split=0):
    got = OUT / 'compact.pcm'
    ref = OUT / 'reference.pcm'
    a = run(exe, 'compact', path, got, [split])
    b = run('host', 'reference', path, ref)
    pcm = got.read_bytes()
    assert pcm and pcm == ref.read_bytes(), str(path) + ': current decoder differs'
    if expected:
        assert pcm == Path(expected).read_bytes(), str(path) + ': encoder differs'
    print('PASS exact', path.name, 'md5=' + hashlib.md5(pcm).hexdigest(), a.stdout.strip(), b.stdout.strip(), flush=True)


def emit(name, channels, sub=None, rate=44100, bps=16, bs=None, assign=None, transform=None):
    bs = bs or len(channels[0])
    if sub is not None:
        ENC['frame'].__globals__['subframe'] = sub
    try:
        frame = ENC['frame'](channels, 0, bs, rate, bps, 0, False,
                             assign if assign is not None else (0 if len(channels) == 1 else 1), ['verbatim'] * 2)
    finally:
        ENC['frame'].__globals__['subframe'] = ORIGINAL_SUB
    if transform:
        frame = transform(frame)
    info = ENC['streaminfo'](max(16, bs), max(16, bs), rate, len(channels), bps, bs)
    path = OUT / (name + '.flac')
    path.write_bytes(b'fLaC' + ENC['block'](0, True, info) + frame)
    expected = path.with_suffix('.pcm')
    expected.write_bytes(b''.join(struct.pack('<' + 'h' * len(channels), *v) for v in zip(*channels)))
    return path, expected


def lpc_sub(order, po, escape, precision):
    coefs = [((1 << (precision - 1)) - 1) * (-1 if j & 1 else 1) for j in range(order)]
    def encode(bits, x, bps, kind, bs):
        bits.put(0, 1); bits.put(31 + order, 6); bits.put(0, 1)
        for v in x[:order]: bits.signed(v, bps)
        bits.put(precision - 1, 4); bits.signed(15, 5)
        for c in coefs: bits.signed(c, precision)
        residual = [x[i] - (sum(coefs[j] * x[i-j-1] for j in range(order)) >> 15) for i in range(order, bs)]
        bits.put(0 if escape else 1, 2); bits.put(po, 4)
        at = 0
        for p in range(1 << po):
            count = (bs >> po) - (order if p == 0 else 0)
            vals = residual[at:at+count]; at += count
            if escape:
                raw = max([1] + [(v.bit_length()+1 if v >= 0 else (~v).bit_length()+1) for v in vals])
                assert raw <= 31
                bits.put(15, 4); bits.put(raw, 5)
                for v in vals: bits.signed(v, raw)
            else:
                bits.put(16, 5)
                for v in vals:
                    u = 2*v if v >= 0 else -2*v-1
                    bits.put(0, u >> 16); bits.put(1, 1); bits.put(u & 65535, 16)
    return encode


def reject(path, expected=None, exe='host', split=0):
    p = run(exe, 'compact', path, OUT/'rejected.pcm', [split], success=False)
    assert p.returncode and 'REJECT ' in p.stdout and 'published=0' in p.stdout, (path, p.stdout)
    if expected:
        assert expected in p.stdout, (path, p.stdout, expected)
    print('PASS reject', path.name, p.stdout.strip(), flush=True)


def custom_bad(name):
    def encode(bits, x, bps, kind, bs):
        if name == 'order-beyond-data':
            bits.put(0, 1); bits.put(63, 6); bits.put(0, 1)
            return
        if name in ('qlp-precision16', 'negative-shift'):
            bits.put(0, 1); bits.put(32, 6); bits.put(0, 1); bits.signed(0, bps)
            bits.put(15 if name == 'qlp-precision16' else 3, 4)
            bits.signed(-1 if name == 'negative-shift' else 0, 5)
            return
        if name == 'wasted-overflow':
            bits.put(0, 1); bits.put(1, 6); bits.put(1, 1); bits.put(0, 17); bits.put(1, 1)
            return
        order = 4 if name == 'partition-overflow' else 0
        bits.put(0, 1); bits.put(8 + order, 6); bits.put(0, 1)
        for _ in range(order): bits.signed(0, bps)
        bits.put(2 if name == 'reserved-residual' else 1 if name == 'rice-overflow' else 0, 2)
        bits.put(8 if name == 'partition-overflow' else 9 if name == 'partition9' else 0, 4)
        if name in ('invalid-rice-escape', 'truncated-rice-escape'):
            bits.put(15, 4); bits.put(31, 5)
            if name == 'invalid-rice-escape': bits.signed(1 << 30, 31)
        elif name == 'rice-overflow':
            bits.put(30, 5); bits.put(0, 3); bits.put(1, 1); bits.put((1 << 30) - 1, 30)
    return encode


def main():
    print(run('host', 'kernel').stdout.strip(), flush=True)
    rows = [r.split('\t') for r in Path('tests/out/flac/manifest.tsv').read_text().splitlines()]
    count = production = 0
    for name, status, *_ in rows:
        if status != 'ok': continue
        path = Path('tests/out/flac') / (name + '.flac')
        large = '4608' in name
        verify(path, path.with_suffix('.pcm'), exe='host4608' if large else 'host')
        count += 1; production += not large
        if large: reject(path, 'flac_block_size')
    for order, po in [(8, 8), (12, 8), (32, 6)]:
        for assignment in [1, 8, 9, 10]:
            for escape in [False, True]:
                for precision in [12, 15]:
                    rng = random.Random(order * 100 + assignment)
                    channels = [[rng.randrange(-32768, 32768) for _ in range(4096)] for _ in range(2)]
                    channels[0][:64] = [32767, -32768] * 32
                    channels[1][:64] = [-32768, 32767] * 32
                    path, want = emit(f'o{order}-p{po}-a{assignment}-e{int(escape)}-q{precision}', channels,
                                      lpc_sub(order, po, escape, precision), assign=assignment)
                    verify(path, want, exe='host')
                    count += 1; production += 1
    samples = Path(os.environ.get('CYDPOD_TESTSAMPLES', '/mnt/e/dev/CYDpod/testsamples'))
    for name in ['Arctic Monkeys - Suck It and See - 08 - Reckless Serenade.flac', '04 - CAKE - Short Skirt - Long Jacket.flac']:
        path = samples/name
        # STREAMINFO max block: both owners are 4096 in the plan, despite its
        # conservative use of the 4608 prototype executable for both originals.
        raw = path.open('rb')
        with raw:
            head = raw.read(12)
        maximum = int.from_bytes(head[10:12], 'big')
        exe = 'host' if maximum <= 4096 else 'host4608'
        verify(path, exe=exe)
        production += maximum <= 4096
        if maximum > 4096: reject(path, 'flac_block_size')
        count += 1
    assert count == 71
    print(f'PASS matrix 71 exact files (21 repo + 48 added + 2 owners); production4096={production}, compatibility4608={71-production}', flush=True)
    reject(samples/'Sample_BeeMoved_96kHz24bit.flac', 'flac_bits')
    for name in ['reject-24bit', 'reject-block8192', 'reject-3ch', 'reject-frame-8192', 'truncated', 'corrupt-frame', 'reject-96k']:
        reject(Path('tests/out/flac')/(name+'.flac'))
    # Empty first partition order32: preserve the known miniflac oracle gap.
    channels = [[(i * 97 + c * 3) % 32768 for i in range(4096)] for c in range(2)]
    path, want = emit('oracle-empty-first-order32', channels, lpc_sub(32, 7, False, 12))
    run('host', 'compact', path, OUT/'gap.pcm')
    assert (OUT/'gap.pcm').read_bytes() == want.read_bytes()
    old = run('host', 'reference', path, OUT/'gap-current.pcm', success=False)
    assert (OUT/'gap-current.pcm').read_bytes() == b''
    print('PASS oracle gap: compact equals encoder PCM, pinned current decoder emits zero PCM; extension to RFC strict geometry', old.stdout.strip(), flush=True)
    # Wasted bits in all stereo assignments (including negative 17-bit side).
    def wasted(bits, x, bps, kind, bs):
        bits.put(0, 1); bits.put(1, 6); bits.put(1, 1); bits.put(0, 1); bits.put(1, 1)
        for value in x: bits.signed(value // 4, bps-2)
    for assignment in [1, 8, 9, 10]:
        channels = [[(-32768 if (i+c)&1 else 32760) for i in range(64)] for c in range(2)]
        path, want = emit('wasted-a'+str(assignment), channels, wasted, assign=assignment)
        verify(path, want, exe='host', split=1)
    # Constant and wasted predictor subframes, including both side17 directions.
    def constant(bits, x, bps, kind, bs):
        assert len(set(x)) == 1
        bits.put(0, 8); bits.signed(x[0], bps)
    path, want = emit('constant', [[-32768]*64, [32767]*64], constant, assign=10)
    verify(path, want, exe='host', split=1)
    def wasted_fixed(bits, x, bps, kind, bs):
        x = [v // 4 for v in x]
        bits.put(0, 1); bits.put(10, 6); bits.put(1, 1); bits.put(0, 1); bits.put(1, 1)
        for v in x[:2]: bits.signed(v, bps-2)
        residual = [x[i]-2*x[i-1]+x[i-2] for i in range(2,bs)]
        ENC['rice'](bits, residual, 2, bs, 0)
    path, want = emit('wasted-fixed', [[(i*4)-128 for i in range(64)]], wasted_fixed)
    verify(path, want, exe='host', split=1)
    # Legal escape-width zero, Rice2 escape, and every accepted pool rate.
    for method in [0, 1]:
        def zeros(bits, x, bps, kind, bs):
            bits.put(0, 1); bits.put(8, 6); bits.put(0, 1)
            bits.put(method, 2); bits.put(0, 4); bits.put(31 if method else 15, 5 if method else 4); bits.put(0, 5)
        path, want = emit('zero-escape'+str(method), [[0]*64], zeros)
        verify(path, want, exe='host')
    for rate in [8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000]:
        path, want = emit('rate'+str(rate), [[i-32 for i in range(64)]], rate=rate)
        verify(path, want, exe='host', split=7)
    # 8-bit rejection; none of these streams reaches a PCM publication.
    path, _ = emit('reject-8bit', [[0]*64], bps=8)
    reject(path, 'flac_bits')
    for name in ['partition-overflow', 'partition9', 'qlp-precision16', 'negative-shift', 'order-beyond-data',
                 'invalid-rice-escape', 'truncated-rice-escape', 'rice-overflow', 'reserved-residual', 'wasted-overflow']:
        path, _ = emit(name, [[0] * (16 if name == 'order-beyond-data' else 64)], custom_bad(name))
        reject(path, split=1)
    for name, transform in [
        ('bad-sync', lambda f: bytes([f[0] ^ 1]) + f[1:]),
        ('bad-crc', lambda f: f[:-1]+bytes([f[-1]^1])),
        ('block-size-zero', lambda f: f[:2]+bytes([f[2]&15])+f[3:]),
        ('truncated-last-frame', lambda f: f[:-4])]:
        path, _ = emit(name, [[0]*64], transform=transform)
        reject(path, split=1)
    base = Path('tests/out/flac/44100-2-4096.flac')
    for split in [1, 2, 7, 2047]: verify(base, base.with_suffix('.pcm'), exe='host', split=split)
    print(run('host','restart',base).stdout.strip(), flush=True)
    print(run('host','hostile',base).stdout.strip(), flush=True)
    corrupt = Path('tests/out/flac/corrupt-frame.flac')
    run('host','compact',corrupt,OUT/'resync.pcm',[7,'recover'])
    assert (OUT/'resync.pcm').read_bytes() == corrupt.with_suffix('.pcm').read_bytes()
    print('PASS bounded CRC resync equals encoder with corrupt frame removed', flush=True)
    for key, (us, frames) in TIMES.items(): print(f'PASS timing sanity {key} weighted_us/frame={us/frames:.2f} frames={frames}',flush=True)
    print('PASS FLACX host complete: matrix, LPC, CRC gate, split input, restart, rejects, resync',flush=True)


if __name__ == '__main__':
    if '--sanitize' in sys.argv:
        for path in OUT.glob('*.flac'):
            if path.name.startswith(('o8-', 'o12-', 'o32-')):
                if '-a10-' not in path.name: continue
                run('sanitize', 'compact', path, OUT/'sanitized.pcm', [7])
            elif path.name.startswith(('rate', 'wasted-a', 'wasted-fixed', 'zero-', 'oracle-', 'constant')):
                run('sanitize', 'compact', path, OUT/'sanitized.pcm', [1])
            else:
                reject(path, exe='sanitize', split=1)
        for name in ['reject-24bit', 'reject-block8192', 'reject-3ch', 'reject-frame-8192', 'truncated', 'corrupt-frame', 'reject-96k']:
            reject(Path('tests/out/flac')/(name+'.flac'), exe='sanitize', split=7)
        print('PASS sanitizer fixtures: all explicit malformed cases plus valid stereo17/LPC/escape/wasted/rates',flush=True)
    else:
        main()
