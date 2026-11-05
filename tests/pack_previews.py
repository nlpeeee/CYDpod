"""Recompress the host preview PNGs for the repository (standard library only).

The host recorder writes dependency-free PNGs with stored (uncompressed) deflate blocks. This copies
them with zlib level 9; pixels are untouched. Usage:
    python -I tests/pack_previews.py tests/out/text-preview docs/research/cy85-text-quality
"""
import struct
import sys
import zlib
from pathlib import Path


def chunks(data):
    at = 8
    while at < len(data):
        size, kind = struct.unpack(">I4s", data[at:at + 8])
        yield kind, data[at + 8:at + 8 + size]
        at += 12 + size


def chunk(kind, body):
    return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))


def pack(src, dst):
    data = src.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit(f"{src}: not a PNG")
    header, idat = b"", b""
    for kind, body in chunks(data):
        if kind == b"IHDR":
            header = body
        elif kind == b"IDAT":
            idat += body
    raw = zlib.decompress(idat)
    dst.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))
    return len(data), dst.stat().st_size


def main():
    source, target = Path(sys.argv[1]), Path(sys.argv[2])
    target.mkdir(parents=True, exist_ok=True)
    before = after = 0
    for png in sorted(source.glob("*.png")):
        b, a = pack(png, target / png.name)
        before += b
        after += a
    print(f"{len(list(source.glob('*.png')))} PNGs, {before} -> {after} bytes")


if __name__ == "__main__":
    main()
