#!/usr/bin/env python3
"""Extract test font locally from the player's fetched Gambatte reference.

The third-party test-ROM artwork stays ignored; it is not game artwork.
"""
import re
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = (root / 'reference/gambatte/test/testrunner.cpp').read_text()
body = source.split('tiles[0x10 * 8 * 8] = {', 1)[1].split('};', 1)[0]
pixels = [pixel for line in body.splitlines() if re.fullmatch(r'[\sO_,]+', line)
          for pixel in re.findall(r'[O_]', line)]
if len(pixels) != 16 * 8 * 8:
    raise SystemExit('Unexpected reference font format; no output written')
rows = [sum((pixels[i + x] == 'O') << (7 - x) for x in range(8))
        for i in range(0, len(pixels), 8)]
output = root / 'verify/tests/hwtest_glyphs.h'
output.write_text('/* Local-only Gambatte test font; do not publish. */\n'
                  'static const unsigned char hwtest_glyphs[16][8] = {\n' +
                  ''.join('    { ' + ', '.join('0x%02X' % b for b in rows[i:i+8]) + ' },\n'
                          for i in range(0, len(rows), 8)) + '};\n')
