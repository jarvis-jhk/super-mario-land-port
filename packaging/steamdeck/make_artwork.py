#!/usr/bin/env python3
"""Writes the Steam library artwork for the Steam Deck installer: original
pixel art (four-shade green hills, coins and the title in the port's own
5x7 font from src/platform/text.c), no game graphics or Nintendo artwork.

    make_artwork.py OUTDIR
      portrait.png 600x900, landscape.png 920x430, hero.png 1920x620,
      logo.png (transparent), icon.png 256x256
"""
import os
import re
import struct
import sys
import zlib

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
SHADES = [(0xE0, 0xF8, 0xD0, 255), (0x88, 0xC0, 0x70, 255), (0x34, 0x68, 0x56, 255), (0x08, 0x18, 0x20, 255)]
CLEAR = (0, 0, 0, 0)
DROP = "gpqy"


def load_font():
    src = open(os.path.join(ROOT, "src", "platform", "text.c")).read()
    rows = re.findall(r"\{((?:0x[0-9a-f]{2}, ){6}0x[0-9a-f]{2})\}", src)
    assert len(rows) == 95, len(rows)
    return {chr(32 + i): [int(v, 16) for v in r.split(", ")] for i, r in enumerate(rows)}


FONT = load_font()


class Image:
    def __init__(self, w, h, fill):
        self.w, self.h = w, h
        self.px = [list(fill) for _ in range(w * h)]

    def rect(self, x, y, w, h, c):
        for yy in range(max(0, y), min(self.h, y + h)):
            row = yy * self.w
            for xx in range(max(0, x), min(self.w, x + w)):
                self.px[row + xx] = c

    def text(self, s, x, y, k, c):
        for i, ch in enumerate(s):
            drop = 2 if ch in DROP else 0
            for r, bits in enumerate(FONT[ch]):
                for col in range(5):
                    if bits & (0x10 >> col):
                        self.rect(x + (i * 6 + col) * k, y + (r + drop) * k, k, k, c)

    def save(self, path):
        raw = b"".join(b"\0" + b"".join(bytes(self.px[y * self.w + x]) for x in range(self.w))
                       for y in range(self.h))

        def chunk(kind, data):
            body = kind + data
            return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))

        with open(path, "wb") as f:
            f.write(b"\x89PNG\r\n\x1a\n")
            f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", self.w, self.h, 8, 6, 0, 0, 0)))
            f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
            f.write(chunk(b"IEND", b""))


def text_width(s, k):
    return (len(s) * 6 - 1) * k


def title(img, lines, cx, y, k, shadow=True):
    """Centred lines of text with a dark drop shadow."""
    for i, s in enumerate(lines):
        x = cx - text_width(s, k) // 2
        yy = y + i * 10 * k
        if shadow:
            img.text(s, x + k // 2, yy + k // 2, k, SHADES[1])
        img.text(s, x, yy, k, SHADES[3])


def coin(img, x, y, k):
    art = ["..33..", ".3223.", "321123", "321123", "321123", "321123", ".3223.", "..33.."]
    for r, line in enumerate(art):
        for c, v in enumerate(line):
            if v != ".":
                img.rect(x + c * k, y + r * k, k, k, SHADES[int(v)])


def scene(w, h, k):
    """Sky, two hills, a row of coins and a brick ground band."""
    img = Image(w, h, SHADES[0])
    ground = h - 6 * k
    for hx, hw in ((w // 5, w // 3), (w * 3 // 4, w // 4)):
        top = ground - hw // 2
        for y in range(top, ground, k):
            half = int((y - top + k) * 1.0)
            img.rect(hx - half, y, 2 * half, k, SHADES[1])
        img.rect(hx - k, top + 3 * k, 2 * k, 2 * k, SHADES[2])
    img.rect(0, ground, w, h - ground, SHADES[2])
    for x in range(0, w, 8 * k):
        for row in range(0, h - ground, 4 * k):
            off = 4 * k if (row // (4 * k)) % 2 else 0
            img.rect(x + off, ground + row, k, 4 * k, SHADES[3])
        img.rect(x, ground, 8 * k, k // 2 or 1, SHADES[3])
    return img


def main(out):
    os.makedirs(out, exist_ok=True)
    # portrait capsule 600x900
    img = scene(600, 900, 10)
    title(img, ["SUPER", "MARIO", "LAND"], 300, 120, 14)
    for i in range(5):
        coin(img, 90 + i * 90, 600, 10)
    img.text("C PORT", 300 - text_width("C PORT", 6) // 2, 700, 6, SHADES[3])
    img.save(os.path.join(out, "portrait.png"))
    # landscape capsule 920x430
    img = scene(920, 430, 6)
    title(img, ["SUPER MARIO LAND"], 460, 110, 9)
    for i in range(7):
        coin(img, 250 + i * 60, 240, 6)
    img.save(os.path.join(out, "landscape.png"))
    # hero 1920x620 (no title: Steam draws the logo over it)
    img = scene(1920, 620, 10)
    for i in range(9):
        coin(img, 560 + i * 90, 260, 10)
    img.save(os.path.join(out, "hero.png"))
    # logo, transparent
    logo = Image(text_width("SUPER MARIO LAND", 10) + 10, 80, CLEAR)
    title(logo, ["SUPER MARIO LAND"], logo.w // 2, 5, 10)
    logo.save(os.path.join(out, "logo.png"))
    # icon: the application icon motif
    icon = Image(256, 256, SHADES[0])
    icon.rect(0, 0, 256, 256, SHADES[3])
    icon.rect(16, 16, 224, 224, SHADES[0])
    for y in range(128, 208, 16):
        half = (y - 112)
        icon.rect(128 - 32 - half, y, 2 * half, 16, SHADES[1])
    icon.rect(16, 208, 224, 32, SHADES[2])
    coin(icon, 160, 40, 10)
    icon.save(os.path.join(out, "icon.png"))


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else ".")
