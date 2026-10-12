"""SM83 (Game Boy CPU) instruction decoder for the analysis tools.

decode(rom_bytes_at_pc) -> Insn with mnemonic text, length, control flow kind,
branch target and memory access information. Pure Python, no dependencies.
"""
from collections import namedtuple

R8 = ["b", "c", "d", "e", "h", "l", "[hl]", "a"]
R16 = ["bc", "de", "hl", "sp"]
R16STK = ["bc", "de", "hl", "af"]
CC = ["nz", "z", "nc", "c"]
ALU = ["add a,", "adc a,", "sub", "sbc a,", "and", "xor", "or", "cp"]
CBROT = ["rlc", "rrc", "rl", "rr", "sla", "sra", "swap", "srl"]

# flow kinds
NONE, JP, JR, CALL, RET, RETI, RST, JPHL, HALT, STOP, ILLEGAL = range(11)

Insn = namedtuple("Insn", "text length flow cond target mem")
# mem: None, or (kind, mode, addr) with kind 'r'/'w'/'rw', mode 'abs'
# (addr known), 'hl', 'bc', 'de', 'c' (FF00+C), 'sp'


def decode(b, pc):
    """b: bytes starting at pc (at least 3 bytes, pad with 0)."""
    op = b[0]
    n8 = b[1] if len(b) > 1 else 0
    n16 = (b[1] | (b[2] << 8)) if len(b) > 2 else 0
    x, y, z = op >> 6, (op >> 3) & 7, op & 7

    def ins(text, length, flow=NONE, cond=None, target=None, mem=None):
        return Insn(text, length, flow, cond, target, mem)

    if op == 0xCB:
        cb = n8
        cx, cy, cz = cb >> 6, (cb >> 3) & 7, cb & 7
        mem = ("rw" if cx != 1 else "r", "hl", None) if cz == 6 else None
        if cx == 0:
            return ins("%s %s" % (CBROT[cy], R8[cz]), 2, mem=mem)
        name = ["", "bit", "res", "set"][cx]
        return ins("%s %d, %s" % (name, cy, R8[cz]), 2, mem=mem)
    if x == 1:
        if op == 0x76:
            return ins("halt", 1, HALT)
        mem = None
        if y == 6:
            mem = ("w", "hl", None)
        elif z == 6:
            mem = ("r", "hl", None)
        return ins("ld %s, %s" % (R8[y], R8[z]), 1, mem=mem)
    if x == 2:
        return ins("%s %s" % (ALU[y], R8[z]), 1, mem=("r", "hl", None) if z == 6 else None)

    rel = pc + 2 + (n8 - 256 if n8 >= 128 else n8)
    rel &= 0xFFFF
    t = {
        0x00: ("nop", 1),
        0x08: ("ld [$%04x], sp" % n16, 3, NONE, None, None, ("w", "abs", n16)),
        0x10: ("stop", 2, STOP),
        0x18: ("jr $%04x" % rel, 2, JR, None, rel),
        0x07: ("rlca", 1), 0x0F: ("rrca", 1), 0x17: ("rla", 1), 0x1F: ("rra", 1),
        0x27: ("daa", 1), 0x2F: ("cpl", 1), 0x37: ("scf", 1), 0x3F: ("ccf", 1),
        0x02: ("ld [bc], a", 1, NONE, None, None, ("w", "bc", None)),
        0x12: ("ld [de], a", 1, NONE, None, None, ("w", "de", None)),
        0x22: ("ld [hl+], a", 1, NONE, None, None, ("w", "hl", None)),
        0x32: ("ld [hl-], a", 1, NONE, None, None, ("w", "hl", None)),
        0x0A: ("ld a, [bc]", 1, NONE, None, None, ("r", "bc", None)),
        0x1A: ("ld a, [de]", 1, NONE, None, None, ("r", "de", None)),
        0x2A: ("ld a, [hl+]", 1, NONE, None, None, ("r", "hl", None)),
        0x3A: ("ld a, [hl-]", 1, NONE, None, None, ("r", "hl", None)),
        0xC3: ("jp $%04x" % n16, 3, JP, None, n16),
        0xC9: ("ret", 1, RET),
        0xD9: ("reti", 1, RETI),
        0xCD: ("call $%04x" % n16, 3, CALL, None, n16),
        0xE9: ("jp hl", 1, JPHL),
        0xE0: ("ldh [$ff%02x], a" % n8, 2, NONE, None, None, ("w", "abs", 0xFF00 | n8)),
        0xF0: ("ldh a, [$ff%02x]" % n8, 2, NONE, None, None, ("r", "abs", 0xFF00 | n8)),
        0xE2: ("ld [c], a", 1, NONE, None, None, ("w", "c", None)),
        0xF2: ("ld a, [c]", 1, NONE, None, None, ("r", "c", None)),
        0xEA: ("ld [$%04x], a" % n16, 3, NONE, None, None, ("w", "abs", n16)),
        0xFA: ("ld a, [$%04x]" % n16, 3, NONE, None, None, ("r", "abs", n16)),
        0xE8: ("add sp, %d" % (n8 - 256 if n8 >= 128 else n8), 2),
        0xF8: ("ld hl, sp%+d" % (n8 - 256 if n8 >= 128 else n8), 2),
        0xF9: ("ld sp, hl", 1),
        0xF3: ("di", 1), 0xFB: ("ei", 1),
        0xF1: ("pop af", 1, NONE, None, None, ("r", "sp", None)),
        0xF5: ("push af", 1, NONE, None, None, ("w", "sp", None)),
    }
    if op in t:
        e = t[op]
        return ins(*e)
    if x == 0:
        if z == 1:
            if y & 1:
                return ins("add hl, %s" % R16[y >> 1], 1)
            return ins("ld %s, $%04x" % (R16[y >> 1], n16), 3)
        if z == 3:
            return ins("%s %s" % ("dec" if y & 1 else "inc", R16[y >> 1]), 1)
        if z in (4, 5):
            return ins("%s %s" % ("inc" if z == 4 else "dec", R8[y]), 1,
                       mem=("rw", "hl", None) if y == 6 else None)
        if z == 6:
            return ins("ld %s, $%02x" % (R8[y], n8), 2, mem=("w", "hl", None) if y == 6 else None)
        if z == 0 and y >= 4:
            return ins("jr %s, $%04x" % (CC[y - 4], rel), 2, JR, CC[y - 4], rel)
    if x == 3:
        if z == 0 and y < 4:
            return ins("ret %s" % CC[y], 1, RET, CC[y])
        if z == 2 and y < 4:
            return ins("jp %s, $%04x" % (CC[y], n16), 3, JP, CC[y], n16)
        if z == 4 and y < 4:
            return ins("call %s, $%04x" % (CC[y], n16), 3, CALL, CC[y], n16)
        if z == 1 and not (y & 1):
            return ins("pop %s" % R16STK[y >> 1], 1, mem=("r", "sp", None))
        if z == 5 and not (y & 1):
            return ins("push %s" % R16STK[y >> 1], 1, mem=("w", "sp", None))
        if z == 6:
            return ins("%s $%02x" % (ALU[y], n8), 2)
        if z == 7:
            return ins("rst $%02x" % (y * 8), 1, RST, None, y * 8)
    return ins("db $%02x ; illegal" % op, 1, ILLEGAL)


def fallthrough(insn, pc):
    return (pc + insn.length) & 0xFFFF


def is_terminal(insn):
    """Instruction after which execution does not fall through."""
    if insn.flow in (JP, JR, RET, JPHL) and insn.cond is None:
        return True
    return insn.flow in (RETI, ILLEGAL)
