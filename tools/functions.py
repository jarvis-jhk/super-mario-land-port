#!/usr/bin/env python3
"""Function inventory of the original game.

Entry points come from the dynamic call graph (emurun --callgraph: call, rst
and jp-hl targets, interrupt vectors), plus labels of the community
disassembly that are called statically. Each function's body is found by
following the control flow from its entry (conditional branches, local
jumps) until ret/reti, a jump to another entry (tail call) or a computed
jump. Only bytes executed in the recorded runs are trusted as code, so the
result also tells how much of each function is covered by tests.

Usage: functions.py ROM DISASM CALLGRAPH COVERAGE OUT.txt [OUT.json]
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sm83  # noqa: E402
import symbols  # noqa: E402


def off_of(bank, addr):
    return addr if addr < 0x4000 else bank * 0x4000 + addr - 0x4000


def main():
    rom = open(sys.argv[1], "rb").read()
    asm = symbols.load(rom, sys.argv[2])
    cov = open(sys.argv[4], "rb").read()
    names = {}
    for name, (bank, addr) in asm.symbols.items():
        if addr < 0x8000 and "." not in name:
            names.setdefault((bank if addr >= 0x4000 else 0, addr), name)

    edges = []
    for line in open(sys.argv[3]):
        kind, a, b, count = line.split()
        fb, fa = (int(x, 16) for x in a.split(":"))
        tb, ta = (int(x, 16) for x in b.split(":"))
        edges.append((kind, (fb, fa), (tb, ta), int(count)))

    entries = {}
    for kind, src, dst, count in edges:
        if kind in ("call", "rst", "jphl", "irq"):
            e = entries.setdefault(dst, {"callers": {}, "kinds": set(), "count": 0})
            e["kinds"].add(kind)
            e["count"] += count
    for kind, src, dst, count in edges:
        if kind in ("call", "rst", "jphl") and dst in entries:
            pass
    entry_set = set(entries)

    # body of each function
    owner = {}
    funcs = {}
    for ent in sorted(entry_set):
        bank, addr = ent
        seen, todo = set(), [addr]
        callees, exits, uncovered = set(), set(), 0
        while todo:
            pc = todo.pop()
            if pc in seen or pc >= 0x8000:
                continue
            if pc != addr and (bank if pc >= 0x4000 else 0, pc) in entry_set:
                exits.add(("tail", pc))
                continue
            seen.add(pc)
            off = off_of(bank, pc)
            insn = sm83.decode(rom[off:off + 3] + b"\0\0", pc)
            if not (cov[off] & 1):
                uncovered += 1
            nxt = pc + insn.length
            if insn.flow in (sm83.JP, sm83.JR):
                tgt = insn.target
                tkey = (bank if tgt >= 0x4000 else 0, tgt)
                if tkey in entry_set and tgt != addr:
                    exits.add(("tail", tgt))
                else:
                    todo.append(tgt)
                if insn.cond:
                    todo.append(nxt)
            elif insn.flow == sm83.CALL:
                callees.add(insn.target)
                todo.append(nxt)
            elif insn.flow == sm83.RST:
                if insn.target == 0x28:
                    exits.add(("jumptable", pc))
                else:
                    todo.append(nxt)
            elif insn.flow in (sm83.RET,):
                if insn.cond:
                    todo.append(nxt)
            elif insn.flow in (sm83.RETI, sm83.JPHL, sm83.ILLEGAL):
                exits.add(("end", pc))
            else:
                todo.append(nxt)
        funcs[ent] = {"size": len(seen), "uncovered": uncovered, "callees": sorted(callees),
                      "exits": sorted(exits)}
        for pc in seen:
            owner.setdefault((bank if pc >= 0x4000 else 0, pc), ent)

    for kind, src, dst, count in edges:
        if kind in ("call", "rst", "jphl") and dst in entries:
            caller = owner.get(src, src)
            entries[dst]["callers"][caller] = entries[dst]["callers"].get(caller, 0) + count

    out = open(sys.argv[5], "w")
    out.write("# bank:addr name  insns uncovered  calls  [kinds]  callers -> callees\n")
    total = 0
    for ent in sorted(funcs):
        f = funcs[ent]
        total += f["size"]
        nm = names.get(ent, "")
        callers = ",".join("%02X:%04X" % c for c in sorted(entries[ent]["callers"]))
        out.write("%02X:%04X %-28s %4d %3d %8d [%s] <- %s -> %s\n" % (
            ent[0], ent[1], nm, f["size"], f["uncovered"], entries[ent]["count"],
            ",".join(sorted(entries[ent]["kinds"])), callers,
            ",".join("%04X" % c for c in f["callees"])))
    out.close()
    print("%d functions, %d instructions in bodies (shared tails counted per function)" % (len(funcs), total))
    if len(sys.argv) > 6:
        json.dump({"%02X:%04X" % k: dict(v, name=names.get(k, ""),
                                          callers=["%02X:%04X" % c for c in entries[k]["callers"]])
                   for k, v in funcs.items()}, open(sys.argv[6], "w"), indent=1)


if __name__ == "__main__":
    main()
