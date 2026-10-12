# Translating the game to C

This is the guide for turning the original routines into readable C. Read
`docs/PLAN.md` first for the overall design.

## The contract

A translated function must behave exactly like the original routine:
same memory effects, same hardware accesses in the same order, and the
**same sequence of conditional branch outcomes** as the original
instructions. The last point is what makes the port cycle-exact: the timing
model replays the original instruction stream (from the ROM) and only needs
the C code to tell it which way each conditional branch went. Everything
else (straight-line code, calls, returns, unconditional jumps) it follows by
itself, so C functions may be structured freely: merge routines, split them,
use loops and helper functions, return values instead of registers.

## The runtime interface (`src/game/rt.h`)

| Original | C |
|----------|---|
| any conditional `jr`/`jp`/`call`/`ret` | `taken(cond)` where cond is true iff the original **takes** the branch. Write it inside the if/while: `if (taken(lives == 0)) return;` For a body executed on fall-through: `if (!taken(x == 0)) { ... }`. |
| a counted loop with only arithmetic inside (e.g. multiply by repeated add) | compute the result directly and call `loop_taken(n)` (n iterations) |
| `rst $28` jump table | `jump_table(index)` then a `switch` |
| `ldh [rXX], a` etc. (FF00-FF7F, FFFF) | `io_write(rXX, v)`, `io_read(rXX)` |
| video RAM access | `vram_read(addr)`, `vram_write(addr, v)` |
| OAM access (FE00-FE9F) | `oam_write(addr, v)` |
| `ld [$2000], a` (MBC bank) | `set_rom_bank(b)` |
| `halt` | `halt()` |
| accesses through a pointer (`[hl]`, `[de]`, `[bc]`, `[c]`) | look the instruction up in `src/core/access_table.c` (bank, address, classes: 1 RAM/ROM, 2 hardware, 4 shared). Only RAM/ROM (1): `mem_read`/`ram_write`/named variables. Only hardware (2): `io_*`/`vram_*`/`oam_*` or `bus_*`. RAM *and* hardware/shared (3, 5, ...): `bus_read`/`bus_write` for **every** access of that instruction. Only shared (4): `shared_*` in main-program code, plain in interrupt handlers. |
| `jp hl` (other than the `rst $28` dispatcher) | `computed_jump(target)` |
| variables shared with interrupt handlers (`src/game/shared.h`), from main-program code | `shared_read(addr)` / `shared_write(addr, v)` |
| plain RAM | named variables from `src/game/ram.h`, or `ram_read`/`ram_write` for computed addresses |
| ROM data | `rom_byte(bank, addr)` / `rom_ptr(bank, addr)` (bank ignored below $4000) |
| `ld b, r` / `ld b, [hl]` (r not B), `rl b` / `rr b`, and `pop bc` at 00:0FDC | `reg_b(value)` with the new B, at that point in the event order; it returns value, so `uint8_t n = reg_b(count);` |
| `inc bc` / `dec bc` | `bc = inc_bc(bc)` / `bc = dec_bc(bc)` on the 16-bit value the original keeps in BC |

Rules of thumb:

* Every conditional branch the original executes must produce exactly one
  `taken()` call, in the same order. Conditional `ret`/`call` count too.
  `jr` to the next instruction is still a branch.
* Hardware accesses must be made in the same order as the original. Two
  reads where the original reads twice (debouncing, retries) stay two reads.
* Keep 8/16-bit wrap-around semantics explicit (`uint8_t`, `uint16_t`,
  casts). Never index C arrays with values from game RAM without the same
  wrapping the original has; prefer `rom_byte(bank, addr)` with 16-bit
  address arithmetic. No undefined behaviour, ever: the tester feeds random
  states.
* BCD arithmetic: `bcd_add`/`bcd_sub` in `bcd.h` are bit-exact with ADD/SUB
  + DAA.
* Flag tricks: if the original computes something via the carry flag (`rr`,
  `rla`...), derive the exact result; check which flag value enters.
* Register B is part of the observable state: the vblank handler's
  bouncing-block path (BlockEvents) uses the B of whatever code it
  interrupted, which can be $C0 (e.g. DrawEnemy keeps its C0xx OAM pointer
  in BC). The shadow keeps B itself for constants (`ld b, n`, `ld bc, nn`),
  `inc b`/`dec b`, shifts, `res`/`set` and `push bc`/`pop bc`; everything
  else in the table row above needs `reg_b()`. Code executed from data
  counts too (the bonus room's prize table at 00:3E7B runs as
  `ld bc, $E502 / inc bc`). The shadow fails at once on a missing or extra
  report, and fntest compares each reported value with the original's B.
* Code inside interrupt handlers (vblank, STAT, timer/sound engine) accesses
  shared variables directly; main-program code must use `shared_*`.
  Routines that run in both (HUD printing, DisplayLives...) use `shared_*`:
  inside a handler it is a plain access. Likewise `shared_or_ram_read` for
  a pointer read whose instruction is class 5 (shared and plain RAM).

## Readability

* Name functions and variables by what they mean in the game (snake_case).
  Each function gets a comment saying what it does, in game terms, and the
  original location: `(00:1C33)`. Declare it in `src/game/game.h` with the
  original label: `void update_lives(void); /* 00:1C33 UpdateLives */`.
* Add RAM variables to `ram.h` (sorted by address, with the community
  disassembly label in the comment). Grep for the address first; never add
  the same address twice.
* Use enums for magic numbers (tile numbers, map positions, game states,
  sound numbers in `sound.h`).
* Group functions by subsystem: `hud.c`, `video.c`, `player.c`, ... one
  subsystem per file.
* Don't keep register-level structure ("a", "hl") unless it helps; use
  parameters and return values.
* Comments explain *why* and the game meaning, including oddities and bugs
  of the original (which must be preserved).

## Verifying (`verify/fntest`)

1. Add an adapter and a table entry in `verify/fntest/registry*.c`.
   **Declare as outputs every register any caller uses after the call**
   (check each call site in the listing: e.g. callers of LookupTile read
   `[hl]`, so HL is an output). The full-game hybrid run (below) catches
   missing outputs, the unit test does not.
   Details:
   the adapter takes the original's input registers, calls the C function,
   stores its results in the output registers that callers use, declared
   with `OUT_*` bits. Set `in_interrupt` for routines that only run inside
   an interrupt handler.
2. `make build/fntest && build/fntest rom/sml.gb --model agb --movie build/resync11.movie NAME...`
   It snapshots the machine every time the original routine is entered
   during the TAS run, runs original and C from the same state (plus 8
   fuzzed variants with random values in the RAM bytes the original read)
   and compares memory, events and outputs. `-v` dumps both event logs on
   the first failure.
3. Fuzzing can produce states the game never reaches (a sound number past
   the end of its table, a data pointer into VRAM). If the original then
   does something nonsensical (jumps into data), give the table entry a
   `valid_state` predicate that rejects such states, with a comment saying
   why they are impossible. Don't use it to hide real differences.
4. Look at "one-sided branches": branch directions never exercised. Either
   reason that the C code is right by construction, or find a movie that
   exercises them.
4. All functions in the registry must pass before committing.

Callees must be translated before callers (bottom up): a C function can
only call C functions.

Functions that never return (main loop, waits with `halt`) or that depend on
interrupts can't be unit-tested; they are verified by the full-game lockstep
test.

## Full-game check (port vs original)

    make build/portrun build/portrun_hybrid build/emurun_s
    build/emurun_s rom/sml.gb --model agb --movie build/resync11.movie --poll-log build/e_agb.log
    build/portrun  rom/sml.gb --model agb --movie build/resync11.movie --poll-log build/p_agb.log
    python3 tools/compare_polls.py build/e_agb.log build/p_agb.log

`portrun` runs the port's own core (src/core/core.c): the C game on the
timing shadow, no interpreter. Both logs must be identical (time and memory
at every joypad poll). To find which routine breaks it, use the development
core: `portrun_hybrid` runs the original on the interpreter with every
registered translation replacing its routine (verify/tools/hybrid.c), and
`SML_HYBRID_ONLY=Name1,Name2 build/portrun_hybrid ...` limits the
replacements.

## Finding your way in the original

* `build/listing/bankN.lst` (from `tools/listing.py`): the whole ROM with
  labels, comments, and `x` marks for executed instructions.
* `build/functions.txt` (from `tools/functions.py`): every routine with size,
  call count, callers and callees.
* `reference/disasm/*.asm`: the community disassembly with comments.

## Working in parallel (several agents/people)

Subsystems are translated in parallel. Each owns its files:

| Subsystem | C files | header | registry |
|-----------|---------|--------|----------|
| core (shared helpers) | `hud.c video.c score.c joypad.c` | `game.h` | `registry.c` |
| sound engine | `sound_*.c` | `sound_engine.h`, `sound.h` | `registry_sound.c` |
| enemies/objects | `enemies*.c`, `objects*.c` | `enemies.h` | `registry_enemies.c` |
| player/collision | `player*.c`, `collision*.c` | `player.h` | `registry_player.c` |
| level/states/main | `level*.c`, `states*.c`, `bonus*.c`, `main*.c` | `level.h` | `registry_level.c` |

* `game.h` includes all subsystem headers; declare your functions in your
  subsystem header only.
* `ram.h` is shared. Before adding a variable, grep `src/game/ram*.h` for its
  address; if it exists, use that name. Add new ones with small, anchored
  edits (never rewrite the file). Same for `sound.h` constants.
* A registry file defines `const FnTest fn_tests_<sub>[] = { ..., { 0 } };`
  (see `verify/fntest/fntest.h`).
* Build a tester with only the sources you need, so another subsystem's
  half-written file can't break your build:

      gcc -O2 -g -Wall -Wextra -Wno-unused-parameter \
          -Ibuild/stable/src/hw -include build/stable/src/hw/machine.h \
          -include build/stable/verify/emu/gb.h -o build/fntest_<sub> \
          verify/fntest/fntest.c verify/fntest/registry.c verify/fntest/registry_<sub>.c \
          src/game/hud.c src/game/video.c src/game/score.c src/game/joypad.c \
          src/game/<your files and the files of subsystems you call> \
          build/stable/src/hw/machine.c build/stable/src/hw/lcd.c build/stable/src/hw/apu.c \
          build/stable/verify/emu/sm83.c build/stable/verify/emu/gb.c src/util/movie.c src/core/access_table.c

  (`make stable-hw` creates `build/stable/`.) `make build/fntest` builds
  everything together.
* Don't commit; the maintainer reviews and commits.
