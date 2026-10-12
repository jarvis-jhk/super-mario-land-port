# Interactive ROM report

Generate the standalone report using the v1.1 ROM and existing execution analysis:

```sh
python3 tools/rom_report.py
```

Open `build/rom-report.html` directly in a browser. No server, external libraries,
fonts, or network access are required. The report embeds ROM bytes and therefore
belongs under the ignored `build/` directory.

The square has 256 × 256 pixels, one per ROM byte. Banks occupy horizontal
quarters. Hover a function or focus its card to show its exact recovered body in
white and calling instructions in orange; orange wins if the two overlap. Click
a card to pin the selection. Expand it for source comments, byte ranges, aliases,
coverage, entry counts, incoming edges, static callees, exits, and all recovered
instructions. Hover a byte to see its address, value, classification, source, and
function owners. Click a function byte to jump to its card.

Task colors are inferred from source names/comments and the documented audio
range. Unclassified bytes remain unknown. Function recovery stops at other
entries and unresolved computed jumps, so an entry is not necessarily a complete
high-level routine. Static cross-bank targets assume the current bank and are
provisional; runtime edges preserve the actual recorded bank. Interrupt events
have no call instruction to highlight. Static incoming edges have a count of zero
and a `static` kind; recorded counts are specific to the supplied runs.

Input paths can be overridden with `--rom`, `--disasm`, `--coverage`,
`--callgraph`. `--output` can choose a path only under ignored `build/`. The disassembly must match the ROM, and coverage
must have exactly one flag byte per ROM byte.
