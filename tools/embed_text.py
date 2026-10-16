#!/usr/bin/env python3
"""Writes a text file as a C string constant:  embed_text.py IN NAME OUT.h"""
import sys

src, name, out = sys.argv[1:4]
text = open(src, encoding="utf-8").read()
lines = []
for line in text.splitlines(True):
    esc = "".join(c if 32 <= ord(c) < 127 and c not in '"\\' else
                  ("\\" + c if c in '"\\' else "\\n" if c == "\n" else "?") for c in line)
    lines.append('    "%s"' % esc)
with open(out, "w") as f:
    f.write("/* Generated from %s by tools/embed_text.py. */\n" % src)
    f.write("static const char %s[] =\n%s;\n" % (name, "\n".join(lines) or '    ""'))
