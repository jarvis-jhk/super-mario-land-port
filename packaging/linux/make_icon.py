#!/usr/bin/env python3
"""Writes the application icon (original pixel art, no game graphics):
a hill and a coin on a four-shade green screen, 16x16 pixels scaled up.

    make_icon.py OUT.png [SIZE]      (SIZE: multiple of 16, default 256)
"""
import struct
import sys
import zlib

SHADES = [(0xE0, 0xF8, 0xD0), (0x88, 0xC0, 0x70), (0x34, 0x68, 0x56), (0x08, 0x18, 0x20)]
ART = [
    "3333333333333333",
    "3000000000000003",
    "3000000000033003",
    "3000000000322303",
    "3000000003212303",
    "3000000003212303",
    "3000000000322303",
    "3000000000033003",
    "3000001100000003",
    "3000011110000003",
    "3000111111000003",
    "3001111211100003",
    "3011112111110003",
    "3222222222222223",
    "3232323232323233",
    "3333333333333333",
]


def write_png(path, size):
    k = size // 16
    rows = []
    for line in ART:
        row = b"".join(bytes(SHADES[int(c)]) * k for c in line)
        rows.extend([b"\0" + row] * k)
    raw = zlib.compress(b"".join(rows), 9)

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", raw))
        f.write(chunk(b"IEND", b""))


if __name__ == "__main__":
    size = int(sys.argv[2]) if len(sys.argv) > 2 else 256
    if size % 16:
        sys.exit("size must be a multiple of 16")
    write_png(sys.argv[1], size)
