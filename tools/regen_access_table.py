#!/usr/bin/env python3
"""Regenerate src/core/access_table.c from fresh runs of the original.

Run after changing src/game/shared.h (rebuild build/emurun_s first: the
Makefile does not track header dependencies, so use `make -B`). The classes
of an instruction are the union over these runs, in parallel:

  * the console-verified TAS (build/resync11.movie) on agb, cgb and dmg;
  * the attract-mode demo (no input, 30000 frames);
  * tracked game-over, complete ending and pause/reset regression movies;
  * the other TAS inputs in reference/tas (they desync, but still execute
    real game code: game over, other routes).

Fresh classes replace the old ones (merging with `emurun --access-table`
would keep stale bits, e.g. RAM for an address that has become shared).
No previous generated table is required; unobserved sites are omitted.
The result is local-only and must never be added to the public index.

    python3 tools/regen_access_table.py [--jobs N]
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TABLE = os.path.join(ROOT, "src/core/access_table.c")
ENTRY = re.compile(r"\{ 0x(\w+), 0x(\w+), (\d+) \}")


def load(path):
    with open(path) as f:
        return {(int(b, 16), int(a, 16)): int(c) for b, a, c in ENTRY.findall(f.read())}


def runs():
    movie = ["--movie", "build/resync11.movie"]
    yield [*movie, "--model", "agb"]
    yield [*movie, "--model", "cgb"]
    yield [*movie, "--model", "dmg"]
    yield ["--model", "agb", "--polls", "30000", "--frames", "30000"]
    for name in ("game-over", "ending", "pause-reset"):
        yield ["--model", "agb", "--movie", "verify/movies/" + name + ".movie"]
    for fin in ("2091.fin", "resync10.fin"):
        yield ["--model", "agb", "--frame-input", "reference/tas/" + fin, "176"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--jobs", type=int, default=min(10, os.cpu_count() or 1))
    args = ap.parse_args()
    with tempfile.TemporaryDirectory() as tmp:
        def one(i_run):
            i, run = i_run
            out = os.path.join(tmp, "t%d.c" % i)
            subprocess.run(["build/emurun_s", "rom/sml.gb", *run, "--access-table", out],
                           cwd=ROOT, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            return load(out)
        with ThreadPoolExecutor(args.jobs) as pool:
            tables = list(pool.map(one, enumerate(runs())))
    fresh = {}
    for t in tables:
        for k, v in t.items():
            fresh[k] = fresh.get(k, 0) | v
    merged = fresh

    header = '/* Generated locally from the player ROM; do not publish. */\n#include "shadow.h"\n\n'
    lines = ["    { 0x%02X, 0x%04X, %u },\n" % (b, a, c)
             for (b, a), c in sorted(merged.items()) if not (b > 0 and a < 0x4000)]
    with tempfile.NamedTemporaryFile("w", dir=os.path.join(ROOT, "build"),
                                     prefix="access-table-", suffix=".c", delete=False) as f:
        tmp_table = f.name
        f.write(header + "const AccessEntry shadow_access_table[] = {\n" + "".join(lines) +
                "};\nconst int shadow_access_table_size = %d;\n" % len(lines))
    os.replace(tmp_table, TABLE)
    mixed = sorted(k for k, v in merged.items() if v & 4 and v & 3)
    print("%d fresh entries; shared and RAM/hardware mixed: %s" % (
        len(lines),
        " ".join("%02X:%04X=%d" % (b, a, merged[(b, a)]) for b, a in mixed)))


if __name__ == "__main__":
    sys.exit(main())
