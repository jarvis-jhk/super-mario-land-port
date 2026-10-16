# Public-tree audit (M0 bootstrap)

The public tree contains the C translation, hardware model, reference harness,
build tooling and authored documentation. No ROM, executable, extracted artwork,
savestate, disassembly, coverage dump or generated ROM report belongs in it.
The separate visibility gate must review this evidence before changing privacy.

| File or group | Verdict and handling |
| --- | --- |
| tools/rom_report.py | Generator code only. It reads the local ROM and embeds all its bytes, decoded instructions and coverage in its output. Output is restricted to ignored build/. |
| tools/rom_report.html | Empty HTML/JS template with __REPORT_DATA__ placeholder; no ROM bytes, embedded images or generated data. Keep. |
| docs/ROM_REPORT.md | Authored instructions describing the local report; no ROM dump or artwork. Keep. |
| src/core/access_table.c | ROM-run-derived bank/PC/access-class table, formerly 2584 rows. Removed from public tracking; regenerate from the local ROM with tools/regen_access_table.py. Fresh generation has 2583 rows and needs no private seed. The omitted stale row was RAM-only; the default classification is RAM. |
| src/core/shadow.c | SM83 instruction-cycle semantics built algorithmically, not a copied ROM instruction stream or timing dump. Actual opcodes/operands come from the ROM. Keep. |
| verify/movies/ending.movie | Run-length-encoded controller input, extended through the ending. No ROM bytes, initial savestate, audio or graphics. Keep. |
| verify/movies/game-over.movie | Authored start/walk input. No ROM or snapshot. Keep. |
| verify/movies/pause-reset.movie | Authored input to pause/reset/restart. No ROM or snapshot. Keep. |
| verify/fntest/state-cases.txt | Authored synthetic RAM inputs for differential tests; both implementations start from a locally generated oracle snapshot. No copied snapshot or ROM/VRAM payload. Function names, RAM fields and small state constants describe test behavior. Keep. |
| verify/fntest/coverage-cases.txt, coverage-required.txt, state-required.txt | Authored synthetic inputs and code-level test assertions: register arguments, slot/tile state constants and individual branch instruction addresses. Channel data and sprite layouts still come from the player's ROM. No copied ROM bytes, sprite data, snapshot or generated coverage report. Keep; actual coverage output stays local. |
| verify/tests/hwtest_glyphs.h | Artwork from the third-party Gambatte test ROMs, not Mario; conservatively local-only/gitignored. tools/regen_hwtest_glyphs.py extracts it from the fetched reference, byte-identically. |
| src/game/player.c | Copied four-byte stomp reward table removed; read it from the ROM at runtime. |
| src/game/level_floaties.c | Sprite/score lookup values formerly copied from ROM instructions removed; read operands at runtime. Arrays contain instruction addresses only. |
| src/game/menu.c, credits.c, level_start.c | Arrays of level codes, ending positions and checkpoint comparisons replaced by ROM operand reads; branch events remain identical. |
| src/game/enemies.c command_test_order | Names of translated operations in the order C must report branch outcomes; executable control flow, not an asset/ROM payload table. Keep. |
| src/game/ram.h, shared.h, constants.h and registries | Handwritten translated memory layout, synchronization contracts, semantic constants and function bindings. No copied data blocks, graphics, levels, songs or bytecode. Keep as program code. |
| src/hw/ and verify/emu/ tables | Generic hardware/SM83 semantics and generated working buffers; no boot ROM byte array. Keep. |
| src/core/rom_id.c | SHA1 fingerprints for identification, not ROM content. Keep. |
| tools/listing.py, symbols.py, functions.py, sm83.py | Analysis/decoder code only. Disassembly and analysis inputs/outputs remain local in ignored reference/ or build/. |
| packaging/linux/make_icon.py | Authored hill/coin icon, no extracted game sprite or branded artwork. Keep generator; no artwork binary tracked. |
| verify/common/png.c, sha1.c, frontend settings/palettes | Standard file/checksum algorithms and original presentation values, not ROM content. Keep. |
| docs/ | Authored engineering descriptions and numerical test summaries; no screenshot/image files or embedded ROM report. Keep. |
| rom/, reference/, tools/testroms/, downloaded toolchains, build/ | ROMs, third-party material, movies, reports, compiled files and verification outputs are ignored and never uploaded. |
| .foreman/, tools/ntfy.sh | Removed. Useful queue items folded into ROADMAP.md; private notifications replaced by the public fault relay. |
| .claude/, .auto-subagent/ in the history bundle | Inspected all 111 distinct bundle heads (all objects present): six historical .claude entries are worktree gitlinks, not source needed for this build. No tracked .auto-subagent files found; local notes only describe already integrated fixes/remaining branch coverage. Nothing must be recovered into the public tree. |
| Local handoff, bundle and .jarvis/ | Private operational records, explicitly ignored. Old history retained only on a local branch and in the bundle. |
| src/platform/text.c, packaging/steamdeck/make_artwork.py, packaging/linux/make_icon.py (2026-10-16) | Original 5x7 pixel font and generated original artwork (hills, coins, title in that font). No game graphics, logo or box art; generated PNGs stay in ignored build/ and release archives. Keep. |
| src/third_party/rcheevos, verify/tests/ra_fake (2026-10-16) | rcheevos v12.5.0 client source (MIT, RetroAchievements.org; LICENSE kept, notice in packaging/README.txt). The fake-server answers hold an authored five-achievement test set whose conditions name RAM addresses already documented in src/game/ram.h; no ROM bytes, no official set data, no credentials. Keep. |
| src/platform/custom_level.c, verify/tests/levels (2026-10-16) | Level editor and Custom mode. The level files are authored tile/enemy/item numbers (the format of docs/LEVELS.md), not ROM bytes. The code holds addresses of the game's level tables and the address ranges of each world's stock level data (where a custom level is written into the in-memory ROM copy); world palettes, graphics and enemy pictures are read from the player's ROM at run time. Keep. |
| Release binaries (GitHub releases, 2026-10-16) | Compiled from this tree plus the locally generated access table, which is linked in and never published as source. They contain no ROM bytes or artwork; the game loads everything from the player's ROM. |

The boundary is program behavior versus content/analysis payloads: this is a C
translation, so addresses and semantic constants necessarily describe the original
program. It does not ship the ROM's graphics, levels, text, music or instruction
stream. The conservative local-only treatment of execution classifications means
full core builds need a locally generated table. No-ROM CI builds the independent
emulator/tools and frontend unit tests until runtime classification replaces that
build dependency; do not distribute the generated source table.

This is a technical content audit, not a legal clearance or a takedown guarantee.
The visibility gate remains external to this work. Release binaries: see the
"Release binaries" row in the table (pre-releases since 2026-10-16).
