#!/usr/bin/env python3
"""Convert TAS movie files to a per-emulator-frame input list.

Output format ("frame input", .fin): one line per emulator frame with the
joypad byte in hex (same bit layout as the game's hJoyHeld:
A=01 B=02 SELECT=04 START=08 RIGHT=10 LEFT=20 UP=40 DOWN=80).

Supported inputs:
  *.vbm   VisualBoyAdvance(-rr) movies (power-on start only)
  *.bk2   BizHawk movies (Input Log.txt, Up|Down|Left|Right|Start|Select|B|A)

Usage: tas_convert.py MOVIE OUT.fin
"""
import struct
import sys
import zipfile


def from_vbm(data):
    assert data[:4] == b"VBM\x1a", "not a VBM file"
    frames = struct.unpack("<I", data[0x0C:0x10])[0]
    if data[0x14] & 1:
        raise SystemExit("VBM starts from a savestate; unsupported")
    ctrl_off = struct.unpack("<I", data[0x3C:0x40])[0]
    out = []
    for i in range(frames):
        v = struct.unpack("<H", data[ctrl_off + 2 * i: ctrl_off + 2 * i + 2])[0]
        out.append(v & 0xFF)
    return out


def from_bk2(path):
    """BizHawk movie: a zip (possibly gzip-wrapped) with 'Input Log.txt'.
    Each frame line looks like |UDLRSsBAP| with '.' for released buttons."""
    import gzip
    import io
    raw = open(path, "rb").read()
    if raw[:2] == b"\x1f\x8b":
        raw = gzip.decompress(raw)
    with zipfile.ZipFile(io.BytesIO(raw)) as z:
        log = z.read("Input Log.txt").decode()
    order = [0x40, 0x80, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01]  # U D L R Start Select B A
    out = []
    for line in log.splitlines():
        if not line.startswith("|"):
            continue
        cells = line.split("|")[1]
        if len(cells) < 8:
            continue
        if len(cells) > 8 and cells[8] != ".":
            raise SystemExit("movie uses Power (reset) at frame %d; unsupported" % len(out))
        v = 0
        for bit, ch in zip(order, cells[:8]):
            if ch != ".":
                v |= bit
        out.append(v)
    return out


def main():
    src, dst = sys.argv[1], sys.argv[2]
    if src.endswith(".vbm"):
        frames = from_vbm(open(src, "rb").read())
    elif src.endswith(".bk2"):
        frames = from_bk2(src)
    else:
        raise SystemExit("unknown movie type")
    with open(dst, "w") as f:
        f.write("# frame input converted from %s\n" % src.split("/")[-1])
        for v in frames:
            f.write("%02X\n" % v)
    print("%d frames" % len(frames))


if __name__ == "__main__":
    main()
