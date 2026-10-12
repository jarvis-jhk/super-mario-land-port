#!/usr/bin/env python3
"""Recover label addresses from the community disassembly without rgbds.

The disassembly in reference/disasm (kaspermeerts/supermarioland) assembles
to exactly our ROM (v1.1), but needs RGBDS 0.3.5 which is not available here.
This script walks its source files in link order and only computes sizes:
instruction lengths are taken from the ROM itself (tools/sm83.py decodes the
bytes at the current address, and the mnemonic is cross-checked against the
source line), data directives are sized from the text. The result is a
symbol file in rgbds .sym format:

    BB:AAAA LabelName

Usage: symbols.py ROM DISASM_DIR OUT.sym
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sm83  # noqa: E402

FILES = ["bank0.asm", "bank1.asm", "bank2.asm", "bank3.asm", "music.asm",
         "levels/enemy_locations.asm"]


class Asm:
    def __init__(self, rom, root):
        self.rom = rom
        self.root = root
        self.symbols = {}      # name -> (bank, addr)
        self.equ = {}          # constants
        self.macros = {}
        self.bank = 0
        self.addr = 0
        self.section_kind = "rom"
        self.last_global = ""
        self.errors = 0
        self.checked = 0
        self.included = set()
        self.macro_count = 0
        # (bank, addr, size, kind, text, comment, where) for listings
        self.lines = []
        self.pending_comment = ""

    def record(self, size, kind, text, where):
        if self.section_kind == "rom":
            self.lines.append((self.bank, self.addr, size, kind, text, self.pending_comment, where))
        self.pending_comment = ""

    # ---- expressions -------------------------------------------------
    def value(self, expr):
        e = expr.strip()
        e = re.sub(r"\$([0-9A-Fa-f]+)", lambda m: str(int(m.group(1), 16)), e)
        e = re.sub(r"%([01]+)", lambda m: str(int(m.group(1), 2)), e)
        e = re.sub(r"\bHIGH\(", "_high(", e)
        e = re.sub(r"\bLOW\(", "_low(", e)

        def sym(m):
            name = m.group(0)
            if name in ("_high", "_low"):
                return name
            if name.startswith("."):
                name = self.last_global + name
            if name in self.equ:
                return str(self.equ[name])
            if name in self.symbols:
                return str(self.symbols[name][1])
            raise KeyError(name)
        e = re.sub(r"\.?[A-Za-z_][A-Za-z0-9_#]*(\.[A-Za-z0-9_]+)?", sym, e)
        return int(eval(e, {"_high": lambda v: (v >> 8) & 0xFF, "_low": lambda v: v & 0xFF}))

    def rom_offset(self, addr=None):
        a = self.addr if addr is None else addr
        return a if a < 0x4000 else self.bank * 0x4000 + (a - 0x4000)

    # ---- directives ----------------------------------------------------
    def section(self, line):
        m = re.search(r'SECTION\s+"[^"]*"\s*,\s*(\w+)(?:\[\$?([0-9A-Fa-f]+)\])?(?:\s*,\s*BANK\[(\d+)\])?', line)
        kind, addr, bank = m.group(1), m.group(2), m.group(3)
        if kind == "ROM0":
            self.bank, self.addr, self.section_kind = 0, int(addr, 16) if addr else 0, "rom"
        elif kind == "ROMX":
            self.bank = int(bank) if bank else 1
            self.addr = int(addr, 16) if addr else 0x4000
            self.section_kind = "rom"
        elif kind == "WRAM0":
            self.bank, self.addr, self.section_kind = 0, int(addr, 16) if addr else 0xC000, "ram"
        elif kind == "HRAM":
            self.bank, self.addr, self.section_kind = 0, int(addr, 16) if addr else 0xFF80, "ram"
        else:
            raise ValueError("unknown section " + line)

    def define(self, name):
        if name.startswith("."):
            name = self.last_global + name
        else:
            self.last_global = name
        self.symbols[name] = (self.bank, self.addr)

    def data_size(self, directive, args):
        if directive == "db":
            n = 0
            for part in split_args(args):
                n += len(part) - 2 if part.startswith('"') else 1
            return n
        if directive == "dw":
            return 2 * len(split_args(args))
        if directive == "ds":
            return self.value(args) if args.strip() else 1
        raise ValueError(directive)

    def incbin(self, args):
        parts = split_args(args)
        name = parts[0].strip('"')
        if name == "baserom.gb":
            return self.value(parts[2])
        return os.path.getsize(os.path.join(self.root, name))

    # ---- instructions --------------------------------------------------
    def instruction(self, text, lineinfo):
        if self.section_kind != "rom":
            raise ValueError("instruction outside ROM: " + text)
        off = self.rom_offset()
        insn = sm83.decode(self.rom[off:off + 3], self.addr)
        want = text.split()[0].lower()
        got = insn.text.split()[0]
        aliases = {"ldi": "ld", "ldd": "ld", "ldh": "ld", "jp": "jp"}
        if aliases.get(want, want) != aliases.get(got, got):
            self.errors += 1
            if self.errors < 20:
                print("mismatch %02X:%04X: source '%s' rom '%s' (%s)" %
                      (self.bank, self.addr, text, insn.text, lineinfo), file=sys.stderr)
        self.checked += 1
        self.record(insn.length, "code", text, lineinfo)
        self.addr += insn.length

    # ---- driver ----------------------------------------------------------
    def run_file(self, path):
        self.pending_comment = ""
        full = os.path.join(self.root, path)
        lines = open(full, encoding="utf-8", errors="replace").read().splitlines()
        self.run_lines(lines, path)

    def run_lines(self, lines, path, macro_args=None):
        i = 0
        cond_stack = []
        while i < len(lines):
            raw = lines[i]
            info = "%s:%d" % (path, i + 1)
            i += 1
            line = strip_comment(raw)
            if ";" in raw and macro_args is None:
                c = comment_of(raw)
                if c:
                    self.pending_comment = (self.pending_comment + " " + c).strip()
            if macro_args is not None:
                line = line.replace("\\@", "_m%d" % self.macro_count)
                for k, v in enumerate(macro_args, 1):
                    line = line.replace("\\%d" % k, v)
            if not line.strip():
                continue
            s = line.strip()
            # conditional assembly (only used in macros here)
            if re.match(r"(?i)IF\b", s):
                cond_stack.append(bool(self.value(s[2:])))
                continue
            if re.match(r"(?i)ELIF\b", s):
                cond_stack[-1] = "done" if cond_stack[-1] else bool(self.value(s[4:]))
                continue
            if re.match(r"(?i)ELSE\b", s):
                cond_stack[-1] = "done" if cond_stack[-1] else True
                continue
            if re.match(r"(?i)ENDC\b", s):
                cond_stack.pop()
                continue
            if cond_stack and cond_stack[-1] is not True:
                continue
            # macro definition
            m = re.match(r"(\w+):\s*MACRO\b", s)
            if m:
                body = []
                while not re.match(r"\s*ENDM\b", lines[i]):
                    body.append(lines[i])
                    i += 1
                i += 1
                self.macros[m.group(1)] = body
                continue
            m = re.match(r"(\S+)\s+EQU\s+(.+)", s)
            if m:
                self.equ[m.group(1)] = self.value(m.group(2))
                self.pending_comment = ""
                continue
            if s.upper().startswith("INCLUDE"):
                name = s.split('"')[1]
                if name not in self.included:
                    self.included.add(name)
                    self.run_file(name)
                continue
            if s.upper().startswith("SECTION"):
                self.section(s)
                self.pending_comment = ""
                continue
            # labels (possibly followed by an instruction on the same line)
            m = re.match(r"^(\.?[A-Za-z_][\w.]*)\s*(::?)\s*(.*)$", s)
            if m and (m.group(2) or raw[:1] not in " \t"):
                self.define(m.group(1))
                s = m.group(3).strip()
                if not s:
                    continue
            elif re.match(r"^\.?[A-Za-z_][\w.]*$", s) and raw[:1] not in " \t":
                self.define(s)
                continue
            word = s.split()[0]
            rest = s[len(word):].strip()
            lw = word.lower()
            if lw in ("db", "dw", "ds"):
                n = self.data_size(lw, rest)
                self.record(n, "data", s, info)
                self.addr += n
            elif word.upper() == "INCBIN":
                n = self.incbin(rest)
                self.record(n, "incbin", s, info)
                self.addr += n
            elif word in self.macros:
                args = split_args(rest)
                if word in ("Noise", "Notes"):
                    n = len(args) if word == "Noise" else len(args) // 2
                    self.record(n, "data", s, info)
                    self.addr += n
                else:
                    self.macro_count += 1
                    self.run_lines(self.macros[word], "%s(macro %s)" % (info, word), args)
            elif word.upper() in ("REPT", "ENDR", "SHIFT", "FAIL", "EXPORT", "GLOBAL", "CHARMAP", "PRINTT"):
                if word.upper() == "CHARMAP":
                    continue
                raise ValueError("unsupported directive at %s: %s" % (info, s))
            else:
                self.instruction(s, info)


def strip_comment(line):
    out, q = [], False
    for ch in line:
        if ch == '"':
            q = not q
        if ch == ";" and not q:
            break
        out.append(ch)
    return "".join(out).rstrip()


def comment_of(line):
    q = False
    for i, ch in enumerate(line):
        if ch == '"':
            q = not q
        if ch == ";" and not q:
            return line[i + 1:].strip()
    return ""


def split_args(s):
    parts, cur, q = [], "", False
    for ch in s:
        if ch == '"':
            q = not q
        if ch == "," and not q:
            parts.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip():
        parts.append(cur.strip())
    return parts


def load(rom, root):
    """Run the size-only assembler; returns the Asm object (symbols, lines)."""
    asm = Asm(rom, root)
    for f in ["charmap.asm", "gbhw.asm", "sound_constants.asm"]:
        asm.included.add(f)
        asm.run_file(f)
    for f in FILES:
        asm.last_global = ""
        asm.run_file(f)
    return asm


def main():
    rom = open(sys.argv[1], "rb").read()
    asm = Asm(rom, sys.argv[2])
    for f in ["charmap.asm", "gbhw.asm", "sound_constants.asm"]:
        asm.included.add(f)
        asm.run_file(f)
    for f in FILES:
        asm.last_global = ""
        asm.run_file(f)
    with open(sys.argv[3], "w") as out:
        out.write("; generated by tools/symbols.py from reference/disasm\n")
        for name, (bank, addr) in sorted(asm.symbols.items(), key=lambda kv: (kv[1], kv[0])):
            out.write("%02X:%04X %s\n" % (bank, addr, name))
    print("%d symbols, %d instructions checked, %d mismatches" %
          (len(asm.symbols), asm.checked, asm.errors))
    return 1 if asm.errors else 0


if __name__ == "__main__":
    sys.exit(main())
