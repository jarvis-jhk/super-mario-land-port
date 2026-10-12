#!/usr/bin/env python3
"""Diff two raw state dumps (WRAM 8K, HRAM 127, VRAM 8K, OAM 160 bytes, as
written by --dump in emurun / gbref / the port). Prints differing addresses
with the disassembly's label names where known.

Usage: diff_dumps.py A.bin B.bin [max]
"""
import os
import re
import sys

REGIONS = [("WRAM", 0xC000, 0x2000), ("HRAM", 0xFF80, 0x7F), ("VRAM", 0x8000, 0x2000), ("OAM", 0xFE00, 0xA0)]


def load_labels():
    labels = {}
    here = os.path.dirname(os.path.abspath(__file__))
    for name in ("wram.asm", "hram.asm"):
        path = os.path.join(here, "..", "reference", "disasm", name)
        if not os.path.exists(path):
            continue
        for line in open(path):
            m = re.match(r"(\w+)::\s*;\s*\$?([0-9A-Fa-f]{4})", line)
            if m:
                labels[int(m.group(2), 16)] = m.group(1)
    return labels


def main():
    a = open(sys.argv[1], "rb").read()
    b = open(sys.argv[2], "rb").read()
    limit = int(sys.argv[3]) if len(sys.argv) > 3 else 40
    labels = load_labels()
    off = 0
    shown = 0
    for name, base, size in REGIONS:
        diffs = [i for i in range(size) if off + i < min(len(a), len(b)) and a[off + i] != b[off + i]]
        if diffs:
            print("%s: %d bytes differ" % (name, len(diffs)))
            for i in diffs:
                if shown >= limit:
                    break
                addr = base + i
                print("  %04X %-28s %02X  %02X" % (addr, labels.get(addr, ""), a[off + i], b[off + i]))
                shown += 1
        off += size


if __name__ == "__main__":
    main()
