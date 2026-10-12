#!/usr/bin/env python3
"""Compare two per-poll logs (verify/common/polllog.h) and report the first
poll where they differ.

Usage: compare_polls.py A.log B.log

Time is compared relative to each log's first poll, so runners with
different time origins can be compared. Exit status 0 if identical.
"""
import sys


def load(path):
    rows = []
    for line in open(path):
        p = line.split()
        rows.append((int(p[0]), int(p[1]), p[2], p[3], p[4], p[5], p[6] if len(p) > 6 else "-"))
    return rows


def main():
    a, b = load(sys.argv[1]), load(sys.argv[2])
    if not a or not b:
        print("empty log")
        return 2
    t0a, t0b = a[0][1], b[0][1]
    names = ["time", "wram", "hram", "vram", "oam", "apu"]
    for ra, rb in zip(a, b):
        va = (ra[1] - t0a,) + ra[2:]
        vb = (rb[1] - t0b,) + rb[2:]
        if va != vb:
            diffs = [n for n, x, y in zip(names, va, vb) if x != y]
            print("first difference at poll %d: %s" % (ra[0], ", ".join(diffs)))
            if "time" in diffs:
                print("  time since first poll: %d vs %d (delta %+d T-cycles)" % (va[0], vb[0], vb[0] - va[0]))
            return 1
    if len(a) != len(b):
        print("identical for %d polls, lengths differ (%d vs %d)" % (min(len(a), len(b)), len(a), len(b)))
        return 1
    print("identical: %d polls" % len(a))
    return 0


if __name__ == "__main__":
    sys.exit(main())
