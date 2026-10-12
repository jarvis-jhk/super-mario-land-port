#!/usr/bin/env python3
"""Tile several PNG screenshots into one image (for quick visual review).

Usage: montage.py OUT.png [--cols N] IN1.png IN2.png ...
Only reads the simple 8-bit RGB PNGs written by verify/common/png.c.
"""
import struct
import sys
import zlib


def read_png(path):
    d = open(path, "rb").read()
    i, idat = 8, b""
    while i < len(d):
        n = struct.unpack(">I", d[i:i + 4])[0]
        t = d[i + 4:i + 8]
        if t == b"IHDR":
            w, h = struct.unpack(">II", d[i + 8:i + 16])
        elif t == b"IDAT":
            idat += d[i + 8:i + 8 + n]
        i += 12 + n
    raw = zlib.decompress(idat)
    return w, h, [raw[y * (1 + w * 3) + 1:(y + 1) * (1 + w * 3)] for y in range(h)]


def main():
    args = sys.argv[1:]
    out = args.pop(0)
    cols = 4
    if args and args[0] == "--cols":
        cols = int(args[1])
        args = args[2:]
    imgs = [read_png(p) for p in args]
    w = max(i[0] for i in imgs)
    h = max(i[1] for i in imgs)
    rows = (len(imgs) + cols - 1) // cols
    W, H = cols * (w + 4), rows * (h + 4)
    canvas = [bytearray(b"\x40" * W * 3) for _ in range(H)]
    for k, (iw, ih, lines) in enumerate(imgs):
        ox, oy = (k % cols) * (w + 4), (k // cols) * (h + 4)
        for y in range(ih):
            canvas[oy + y][ox * 3:(ox + iw) * 3] = lines[y]
    raw = b"".join(b"\x00" + bytes(r) for r in canvas)

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))

    with open(out, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


if __name__ == "__main__":
    main()
