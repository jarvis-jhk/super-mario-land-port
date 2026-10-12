# Hardware model accuracy

The hardware model (`src/hw/machine.c`, with the CPU in `verify/emu/sm83.c`)
is checked in three ways:

1. **Blargg** `make check-emu` (cpu_instrs, instr_timing, mem_timing): passes.
2. **Gambatte's hardware test ROMs** (`build/hwtests/<category>/*.gb[c]`, verified on
   real DMG-08 / CGB-04C consoles), run with `verify/tests/hwtest.c`:
   `build/hwtest dmg|cgb|agb [-v] [--oracle FILE] ROM...`. Each ROM shows a few hex
   digits; the expected digits are in its file name.
3. **Gambatte itself as oracle**, on the real game: `build/gbref` vs `build/emurun`
   with `--poll-log`, compared by `tools/compare_polls.py` (time and WRAM/HRAM/VRAM/OAM
   hashes at each of the ~45000 joypad polls of the TAS).

## Scope of the test suite

Super Mario Land is a DMG game. On a DMG every ROM is run (the CGB flag is ignored).
On CGB/AGB only ROMs without the CGB flag are meaningful (DMG-compatibility mode), but
only a handful of the test ROMs are plain `.gb`, and those carry DMG-only expectations.
So for CGB/AGB the suite is used as a *differential* test against Gambatte:

    python3 tools/regen_hwtest_glyphs.py  # local-only third-party test font
    g++ -O2 -std=c++11 -w -DHAVE_STDINT_H -Ireference/gambatte/libgambatte/include \
        -o build/hworacle verify/tests/hworacle.cpp build/libgambatte.a      # after "make oracle"
    build/hworacle agb build/hwtests/*/*.gb* > oracle_agb.txt
    build/hwtest agb --oracle oracle_agb.txt build/hwtests/*/*.gb*

`hworacle` runs each ROM on Gambatte with the CGB flag (header byte 0143) cleared and
prints the digits shown; `hwtest --oracle` compares our model against them. ROMs that
show nothing readable that way (they need CGB mode) are dropped by the oracle.

Skipped everywhere: double speed (`_ds_`), `speedchange`, `cgbpal_m3`, `dma` (CGB HDMA),
`serial`, `undef_ops`, `sound` (APU out of scope here), tests of the Color's palette
registers (`_cgbp`). Also not run: tests that need cartridge RAM or MBC variants
(they time out into garbage on our MBC1-without-RAM model; see "Known gaps").

## Results (passed/run)

Before = model at commit ed12c46 (tick-then-access, STAT-line LCD, instant OAM DMA);
after = this work.

### By file name (DMG: all ROMs; CGB/AGB: plain .gb ROMs only)

| category | DMG before | DMG after | CGB before | CGB after | AGB before | AGB after |
|---|---|---|---|---|---|---|
| display_startstate | 3/4 | 4/4 | - | - | - | - |
| div | 1/2 | 2/2 | - | - | - | - |
| enable_display | 38/68 | 68/68 | - | - | - | - |
| halt | 33/75 | 75/75 | - | - | - | - |
| irq_precedence | 16/20 | 20/20 | - | - | - | - |
| lcdirq_precedence | 28/31 | 31/31 | - | - | - | - |
| ly0 | 20/33 | 33/33 | - | - | - | - |
| lyc0int_m0irq | 2/2 | 2/2 | - | - | - | - |
| lyc153int_m2irq | 3/6 | 6/6 | - | - | - | - |
| lycEnable | 55/78 | 78/78 | - | - | - | - |
| lycint_ly | 2/2 | 2/2 | - | - | - | - |
| lycint_lycflag | 2/4 | 4/4 | - | - | - | - |
| lycint_lycirq | 1/2 | 2/2 | - | - | - | - |
| lycint_m0stat | 1/2 | 2/2 | - | - | - | - |
| lycm2int | 3/9 | 9/9 | - | - | - | - |
| lywrite | 4/4 | 4/4 | - | - | - | - |
| m0enable | 60/69 | 69/69 | - | - | - | - |
| m0int_m0irq | 1/2 | 2/2 | - | - | - | - |
| m0int_m0stat | 2/4 | 4/4 | - | - | - | - |
| m0int_m3stat | 1/2 | 2/2 | - | - | - | - |
| m1 | 40/61 | 61/61 | - | - | - | - |
| m2enable | 28/42 | 42/42 | - | - | - | - |
| m2int_m0irq | 18/32 | 32/32 | - | - | - | - |
| m2int_m0stat | 1/2 | 2/2 | - | - | - | - |
| m2int_m2irq | 4/6 | 6/6 | - | - | - | - |
| m2int_m2stat | 1/2 | 2/2 | - | - | - | - |
| m2int_m3stat | 1/2 | 2/2 | - | - | - | - |
| miscmstatirq | 104/139 | 139/139 | - | - | - | - |
| oam_access | 14/24 | 24/24 | - | - | - | - |
| oamdma | 60/393 | 293/393 | - | - | - | - |
| scx_during_m3 | 0/2 | 2/2 | - | - | - | - |
| sprites | 65/96 | 96/96 | - | - | - | - |
| tima | 66/113 | 113/113 | - | - | - | - |
| vram_m3 | 10/15 | 15/15 | - | - | - | - |
| vramw_m3end | 10/12 | 12/12 | - | - | - | - |
| window | 68/133 | 133/133 | - | - | - | - |
| **total** | 766/1493 | 1393/1493 | 0/0 | 0/0 | 0/0 | 0/0 |

### Against Gambatte as oracle (all ROMs, CGB flag cleared, DMG-compatibility mode)

| category | DMG before | DMG after | CGB before | CGB after | AGB before | AGB after |
|---|---|---|---|---|---|---|
| display_startstate | 11/12 | 12/12 | 11/12 | 12/12 | 12/12 | 12/12 |
| div | 7/8 | 8/8 | 7/8 | 8/8 | 8/8 | 8/8 |
| enable_display | 38/68 | 68/68 | 37/68 | 68/68 | 38/68 | 68/68 |
| halt | 33/75 | 75/75 | 43/75 | 75/75 | 43/75 | 75/75 |
| irq_precedence | 36/40 | 40/40 | 36/40 | 40/40 | 36/40 | 40/40 |
| lcdirq_precedence | 28/31 | 31/31 | 28/31 | 31/31 | 28/31 | 31/31 |
| ly0 | 20/33 | 33/33 | 20/33 | 33/33 | 18/33 | 33/33 |
| lyc0int_m0irq | 2/2 | 2/2 | 2/2 | 2/2 | 2/2 | 2/2 |
| lyc153int_m2irq | 3/6 | 6/6 | 3/6 | 6/6 | 3/6 | 6/6 |
| lycEnable | 56/80 | 80/80 | 65/80 | 80/80 | 65/80 | 80/80 |
| lycint_ly | 2/2 | 2/2 | 2/2 | 2/2 | 2/2 | 2/2 |
| lycint_lycflag | 2/4 | 4/4 | 2/4 | 4/4 | 2/4 | 4/4 |
| lycint_lycirq | 1/2 | 2/2 | 1/2 | 2/2 | 1/2 | 2/2 |
| lycint_m0stat | 1/2 | 2/2 | 1/2 | 2/2 | 1/2 | 2/2 |
| lycm2int | 3/9 | 9/9 | 3/9 | 9/9 | 3/9 | 9/9 |
| lywrite | 4/4 | 4/4 | 4/4 | 4/4 | 4/4 | 4/4 |
| m0enable | 60/69 | 69/69 | 64/69 | 69/69 | 64/69 | 69/69 |
| m0int_m0irq | 1/2 | 2/2 | 1/2 | 2/2 | 1/2 | 2/2 |
| m0int_m0stat | 2/4 | 4/4 | 2/4 | 4/4 | 2/4 | 4/4 |
| m0int_m3stat | 1/2 | 2/2 | 1/2 | 2/2 | 1/2 | 2/2 |
| m1 | 41/62 | 62/62 | 47/62 | 62/62 | 47/62 | 62/62 |
| m2enable | 28/42 | 42/42 | 28/42 | 42/42 | 28/42 | 42/42 |
| m2int_m0irq | 18/32 | 32/32 | 18/32 | 32/32 | 18/32 | 32/32 |
| m2int_m0stat | 1/2 | 2/2 | 1/2 | 2/2 | 1/2 | 2/2 |
| m2int_m2irq | 4/6 | 6/6 | 4/6 | 6/6 | 4/6 | 6/6 |
| m2int_m2stat | 1/2 | 2/2 | 1/2 | 2/2 | 1/2 | 2/2 |
| m2int_m3stat | 1/2 | 2/2 | 1/2 | 2/2 | 1/2 | 2/2 |
| miscmstatirq | 104/139 | 139/139 | 126/139 | 139/139 | 126/139 | 139/139 |
| oam_access | 15/25 | 25/25 | 17/25 | 25/25 | 17/25 | 25/25 |
| oamdma | 67/400 | 300/400 | 73/403 | 285/403 | 73/400 | 285/400 |
| scx_during_m3 | 0/2 | 2/2 | 0/2 | 2/2 | 0/2 | 2/2 |
| sprites | 65/96 | 96/96 | 68/96 | 96/96 | 68/96 | 96/96 |
| tima | 68/115 | 115/115 | 67/115 | 115/115 | 68/115 | 112/115 |
| vram_m3 | 10/15 | 15/15 | 9/15 | 15/15 | 9/15 | 15/15 |
| vramw_m3end | 10/12 | 12/12 | 10/12 | 12/12 | 10/12 | 12/12 |
| window | 68/133 | 133/133 | 68/133 | 133/133 | 68/133 | 133/133 |
| **total** | 812/1540 | 1440/1540 | 871/1543 | 1425/1543 | 873/1540 | 1422/1540 |

Poll-log comparison against Gambatte on `rom/sml.gb` with `build/resync11.movie`
(TiKevin83's resync, 44976 joypad polls):

| model | before | after |
|-------|--------|-------|
| AGB   | first difference at poll 17 (WRAM: stack contents) | identical for all 44976 polls (time + WRAM/HRAM/VRAM/OAM hashes) |
| DMG   | poll 1, +4 T-cycles | identical for all 44976 polls |
| CGB   | poll 71 (WRAM) | identical for all 44976 polls |

(`gbref` keeps running for ~400 more polls after the movie ends; `emurun` stops with the
movie, hence "lengths differ" in the script's output.)

## What the model does now

### Time base
The CPU performs each bus access at the START of its machine cycle and then lets
4 T-cycles pass (`rd`/`wr` in sm83.c). Hardware state is "as of the cycle of the
access": an event at cycle N is visible to accesses at cycle N and later. All the
small constants (2, 3, 4 cycle offsets) in machine.c are in this base; the post-boot
state (divider phase, LCD position) is therefore exactly what the boot ROMs leave.

### CPU (sm83.c)
* HALT entry costs fetch + 4 cycles on CGB/AGB, fetch + 8 on DMG; the DMA engine
  freezes after the first of those extra cycles (`Machine.cpu_halted`).
* HALT with `IF & IE != 0` does not halt: the next opcode is fetched without
  advancing PC (the HALT bug); if an interrupt is dispatched in that state, PC is
  decremented first so the handler returns to the HALT. This applies with IME=1 too
  (`EI; HALT`).
* Wake-up from HALT: polled every M-cycle; on wake +1 M-cycle on CGB/AGB, on DMG
  only if the request arrived within the last 2 cycles (`machine.min_int_time` records
  when IF&IE last became non-zero). Then dispatch if IME.
* Dispatch (20 cycles): 3 idle M-cycles, push PC high (at +12), choose the vector
  from IF&IE *after* that push (the push may overwrite IE), push PC low (at +16) and
  clear the IF bit at that write (`machine_ack_interrupt`), jump. The acknowledge also
  swallows a request the LCD (or, +1 on CGB, the timer) raises in the next 2 cycles.
* IF writes swallow requests due in the next cycle (for bits written as 0).
* EI delay unchanged (interrupts enabled after the next instruction); RETI immediate.

### Timer
* TIMA counts on falling edges of the DIV bit selected by TAC. Overflow: TIMA reads 0
  for 3 cycles, then TMA is loaded and the interrupt requested; for the next 4 cycles
  TIMA follows TMA (a TMA write reaches TIMA, a TIMA write is lost); a TIMA write
  during the 3 zero cycles cancels the reload and the interrupt.
* DIV write: TIMA counts if the selected bit is high or about to rise within 3 cycles.
* TAC write while running: TIMA counts if the signal falls (new bit 0 / disabled), with
  the same 3-cycle look-ahead. AGB quirk: enabling/retuning counts a falling edge from
  the old selected bit to the new one even if the timer was disabled.

### LCD
* LY counter with per-cycle line position; LY, STAT mode, LYC=LY flag and the VRAM/OAM
  access windows are computed from it with the offsets measured on hardware
  (e.g. LY reads next value 6 cycles before the line ends (`ly&(ly+1)` glitch at
  exactly 6), line 153 reads 0 from its first cycle (AGB: from the second); mode 2 is
  read from cycle 453 of the previous line; OAM blocked 4 cycles before line start; the
  LY=LYC flag drops for 2 cycles before every LY change; AGB mode 1 lasts one cycle
  longer on line 153; DMG can write OAM at exactly line cycle 76, etc.).
* **Mode 3 length** comes from a step-by-step model of the pixel pipeline
  (`PpuModel`): drawing starts SCX mod 8 cycles late, one pixel per cycle, a sprite
  costs 6 cycles plus up to 5 more while the tile fetch in progress finishes (first
  sprite of a tile), a sprite at X=0 costs extra depending on SCX, the window costs 6
  cycles when it starts at WX (restarts on LCDC.5/WX/WY changes mid-line), on CGB sprites
  cost time even when LCDC.1 is clear. Register writes reach the pipeline with the
  delays measured on hardware (SCX/WX/WY/LCDC: 0-3 cycles, CGB later than DMG). The
  same step function run on a copy of the state predicts when drawing ends; STAT mode
  0, VRAM/OAM access and the mode 0 interrupt use that prediction.
* The OAM scan reads entry i's Y/X at line cycle 2i and its size bit (LCDC.2) at 2i+1
  (CGB: also at 2i); an OAM DMA in progress makes the scan see 0xFF; the first line
  after switching the LCD on has no sprites.
* **STAT interrupt**: the four sources (LY=LYC, mode 0/1/2) are separate timed events
  (`ev_m0/m1/m2/lyc`) with the "blocked if another enabled source keeps the line high"
  rules, working on latched copies of STAT/LYC that STAT/LYC writes reach with a few
  cycles delay. Writing STAT on a DMG can raise the interrupt (all enables are briefly
  set); the Color has its own cycle-dependent rules. LYC writes can trigger on the
  cycle LY changes (CGB: 5 cycles late).
* Switching the LCD on: line 0 starts at once, its drawing starts 2 cycles later than
  on other lines, STAT reads mode 0 for the first 80 cycles, OAM/VRAM are accessible
  (the first line behaves as hblank); the LY=LYC flag is frozen while the LCD is off and
  a STAT interrupt is requested on enabling if LYC=0 and the flag was clear.
* VRAM read at exactly the cycle mode 3 ends returns 0 once per run (a quirk of the
  reference emulator build that the TAS was made on; kept for exactness).

### OAM DMA
Real byte-wise transfer: starts 8 cycles after the write, one byte per M-cycle, OAM
inaccessible meanwhile; bus conflicts (CPU reads in the source's address range see the
byte being copied, CPU writes there land in OAM; the conflicting address ranges differ
between DMG and CGB and by source); a restart while running carries on for 2 more bytes
first; the transfer freezes while the CPU is in HALT. Unmapped IO registers do not keep
written values. Reads of A000-BFFF (no cartridge RAM) return the last value on the
cartridge bus (ROM reads; on a DMG also work RAM reads) for 8 cycles, then 0xFF.

## Known gaps

* `oamdma`: ROMs that rely on cartridge RAM / MBC banking (`srambankchange`, `busyread*`
  from A000..., SRAM sources) and a few push/pop-into-IO cases (FEFF/FF01 area) fail.
  Super Mario Land never does any of this (it waits for the DMA in HRAM).
* AGB `tima`: `tc00_late_tc01_5/6` and `tc00_tc01_late_tc00_of_2` (the AGB TAC quirk
  combined with an overflow in the same cycle window): we count the quirk's extra tick
  as a normal increment; Gambatte keeps an un-normalised TIMA value there.
* Palette registers of the Color (not applicable in DMG mode), double speed, HDMA,
  serial, sound: out of scope.
* Initial WRAM/HRAM/OAM contents are zero (the console leaves pseudo-random
  patterns); the game clears what it uses, so the poll hashes still match.
