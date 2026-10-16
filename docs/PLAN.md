# Plan and status

This file is the single source of truth for "where are we". Keep it current:
every work session starts by reading it and ends by updating it.

## M0 public-tree setup (2026-10-12)

See [ROADMAP.md](../ROADMAP.md), [NOTES.md](../NOTES.md) and the
[per-file content audit](PUBLIC_AUDIT.md). The repository must remain private
until the external visibility gate approves the fresh tree. No release yet.
The TAS roadmap push retry uses authenticated HTTPS to the default main branch;
see NOTES.md for the corrected worker-authentication status and fresh gate evidence.
Generated ROM access classifications and third-party test font are local-only;
see BUILDING.md for regeneration. Runtime asset tables removed during the audit
now read operands/data from the player's ROM, preserving timing events.
The public anonymous fault relay uses the Actions secret only on the server.

## Four-platform release (2026-10-16)

packaging/make_release.sh builds pre-release assets for Linux x86_64
(AppImage, tarball; aarch64 AppImage too), Windows x86_64, PortMaster aarch64
(system SDL2, `make port-portmaster-aarch64`) and the Steam Deck installer
(packaging/steamdeck/). Release builds update themselves from GitHub releases
(src/platform/update.c, size + SHA-256 from update.txt, atomic replace, opt-out)
and show CHANGELOG.md sections on the first start of a new version, before the
core starts, so game state and TAS playback are untouched. docs/UPDATES.md has
the details. Hardware checks on Windows, a PortMaster handheld and the Deck are
the player's; see NOTES.md for what was verified here.

## No-ROM CI (2026-10-16)

GitHub Actions (.github/workflows/ci.yml) builds the hardware model, tools
and the port for Linux x86_64/aarch64 and Windows without a ROM, linking the
placeholder src/core/access_table_stub.c (selected with ACCESS_TABLE=), and
runs the frontend and fault-reporter tests. Stub binaries refuse to run the
game; the timing shadow needs the ROM-run classifications at run time, so
playable/release binaries are still built locally with the generated table.
A CI-built release would need the table derived on the player's machine.

## Coverage checkpoint (2026-10-14)

The setup audit, fresh tree, roadmap and fault relay are implemented; GitHub
now reports public visibility. This worker did not change it. The existing
session restriction on releases remains in force; external visibility-gate
evidence remains a release prerequisite; no-ROM CI exists since 2026-10-16.

`check-states` now enforces coverage, rather than only comparing whichever
paths happen to run. Authored cases cover both square-2 pause notes and the
timer interrupt's pause/repeated-beep/unpause paths, all 21 menu branches,
normal/underground/title first-screen widths, and targeted coverage across 15
enemy/shooter entries. Existing pipe, bonus-ladder, time-up, pause, unused
dispatcher and reunion cases now have coverage contracts as well. No game
code or validity predicate changed.

Further coverage review remains necessary: the new enemy tests protect
specific branches, not every script/collision branch. Hardware wait loops
depend on sub-frame phase; the second STAT wait is one-sided in these seeds.
The enemy-list scan reached through ResetLevelEnemies remains one-sided;
that caller resets progress to the first screen. InitEnemySlots is tested
independently with later progress. Do not invent impossible game states or
change validity predicates to force an outcome.

## Current state (2026-10-11, read this first)

**The Linux port is complete (phase 8 for Linux).** The SDL2 frontend
(`src/platform/sdl_main.c`) has a settings file with key and controller
bindings (`settings.c`), audio resampled to the device rate with dynamic
rate control (`audio_sync.c`), clock or vsync pacing with automatic
fallback, integer/sharp/nearest scaling, palettes, fast forward,
screenshots and start-up error dialogs. `make appimage-x86_64` and
`make appimage-aarch64` build self-contained AppImages: SDL2 is built
from source and linked statically for glibc 2.31 and loads X11, GL and
audio libraries at run time. User guide: docs/PLAYING.md; build details:
docs/BUILDING.md. `make check-frontend` tests the settings parser,
the resampler and a headless run.

**Phase 7 is done: the port runs without an interpreter.** `src/core/core.c`
runs the C game (`run_game()`) on the timing shadow, with the three C
interrupt handlers registered directly with the shadow. The production
build (`make sml`, `make port-all`) contains no SM83 interpreter, no hybrid
bridge and no function registry. The hybrid core is now a verification tool:
`verify/tools/core_hybrid.c` and `verify/tools/hybrid.c` are linked into
`build/portrun_hybrid` only. `build/portrun` is the port itself.

The interrupted-register context is solved by tracking register B in the
shadow (`shadow_b()`). The vblank handler's BlockEvents uses the B of the code
it interrupted, and that value really can be $C0 (DrawEnemy keeps its C0xx
OAM pointer in BC). The shadow follows constants, inc/dec b, shifts,
res/set and push/pop bc. The C code reports every other B load with
`reg_b()` / `inc_bc()` / `dec_bc()`: 124 instruction sites, including
`pop bc` after `push af` at 00:0FDC and the prize table at 00:3E7B that runs
as code. See docs/TRANSLATION.md. A static value-set analysis found that B
may hold $C0 at most interruptible instructions, so no shortcut exists.
fntest compares each reported B with the original. shadowtest checks the
shadow's B against the original at every interrupt dispatch.

Verification of the pure-C build: identical poll logs on the console-verified
TAS on AGB, CGB and DMG (44976 polls each), on the ending (65477, AGB and
CGB), on game over (5082), on pause/reset (706) and on idle/demo (8061).
The 46000-frame shadow replay has zero mismatches, including B at every
dispatch. All 230 function-test entries pass on the TAS (100 samples,
fuzz 8). `make check`, `make check-states` and `make check-models` run the
pure-C build. The hybrid core is still checked as well.

### Next

Player roadmap additions (2026-10-11), designed 2026-10-12: see the top-level
[ROADMAP.md](../ROADMAP.md), [mod design](MODS.md) and
[Steam Deck release criteria](STEAM_DECK.md). Scripted gameplay belongs to
the separate Custom mode; the stock core and its TAS invariant remain intact.
These are planned milestones, not implemented features.

The TAS roadmap delivery follow-up uses authenticated HTTPS to the remote's
default main branch; verification and delivery state are recorded in NOTES.md.

1. Close remaining coverage gaps (SetupChannel square 2, one-sided enemy
   branches, additional valid pipe/bonus/menu paths); use the seeded
   differential cases below without weakening validity predicates.
2. Windows: run the windows-x86_64 build on Windows (it builds, untested),
   then a release zip. Run the aarch64 AppImage on real arm64 hardware.
3. Phase 9 and the user roadmap (widescreen, RetroAchievements, speedrun
   timer, menu-accessible TAS recorder/player, editor...). An in-game menu for
   settings/bindings belongs there.

Translation sub-agents: none running. To restore verification tools:
`make stable-hw`, then `make check`, `make check-states` and
`make check-models` with shell pipefail enabled. Input movies and commands
are listed below.

**Lag frames (solved 2026-10-10).** The vblank handler (0060: DrawColumn,
coin/block events, lives, OAM DMA, score, timer, AnimateBackground) also
runs when a frame lags, and then it interrupts main code. `emurun
--shared-report` counts such a vblank (return address not 0297) as
concurrent and its `call hDMARoutine` as a read of C000-C09F; the resulting
variables are in `src/game/shared.h`. Translations reach them with
`shared_*`; routines that run in both contexts (DrawColumn, HUD printing,
AddCoin) use them everywhere; `shared_or_ram_read` covers class-5 pointer
reads. FFFD (the active ROM bank restored by the timer interrupt) is
also synchronized: a C caller must not update it while the shadow is
still finishing instructions in the previous bank.
`tools/regen_access_table.py` regenerates `src/core/access_table.c`
from fresh runs (merging with `emurun --access-table` keeps stale RAM bits).

## Goal

A human-readable C port of **Super Mario Land** (Game Boy, 1989) for Linux and
Windows whose output is **cycle-exact**: for the same joypad input it produces
the same frames, the same audio and the same random numbers as the original
cartridge, so tool-assisted speedruns (TAS) recorded on emulators play back
perfectly on the port. Exactness is checked automatically against the
original ROM running in a reference emulator.

Target ROM: **Super Mario Land (World) (Rev A / v1.1)**,
SHA1 `418203621b887caa090215d97e3f509b79affd3e`. The ROM is not part of the
repository; the port loads graphics, levels, music and timing data from the
user's own copy at start-up.

## Hard rule

**At every commit, every TAS in `verify/movies/` (and `reference/tas/`) must
still play back perfectly** — on the reference emulator and on the port,
including in enhanced modes (widescreen, upscaling, achievements). Enhancements
may only change presentation, never game state.

## Roadmap (from the user, 2026-10-08)

1. Cycle-exact port, verified (core work, see phases below).
2. **Any aspect ratio**: ultrawide screens show much more of the world. Must
   not change game logic (TAS sync); the extra area is drawn from level data.
   **Implemented 2026-10-16 (v0.3.0, src/platform/widescreen.c):** the
   hardware model records each line's LCD registers (output only); the
   frontend keeps the game's 160 pixels and adds sides from the background
   map (columns drawn since the level part was entered) and, to the right,
   columns decoded from the level data like load_next_column. Edge-crossing
   sprites are drawn; window lines, the HUD and non-level scenes stay blank.
   `make check-frontend` compares the decoded sides with the game's own
   pictures 40 frames later over resync11 (floor 95 %).
3. **RetroAchievements** integrated (rcheevos), with nice overviews in the
   menu. The port keeps the original memory map, so the official
   achievement set's memory conditions work unchanged.
4. Packages: **AppImage for amd64 and arm64**, **Windows amd64** binary.
5. **Upscaled but still pixelated** graphics (integer / sharp scaling).
6. **Level/world editor** in game, comparable to Super Mario Maker but limited
   to the mechanics Super Mario Land has (its tiles, enemies, power-ups, pipes,
   autoscroll/shooter stages, bosses...).
7. **Online custom levels**: in-game upload, listing, voting and playing,
   backed by a custom server (to be written; hosting TBD with the user).
   Custom levels are a separate mode; the original game and TAS sync are
   untouched.
8. **Speedrunning statistics and tools** (timer/splits, per-level stats,
   practice helpers; details to be designed with the user).
   **Implemented 2026-10-16 (v0.2.0, src/platform/speedrun.c):** the
   speedrun.com rules (API, checked 2026-10-16) are real time, Any% and
   Normal + Hard Mode: start on the frame before the level and UI load in
   (the blank frame before 1-1), end on the frame Tatanga's sprite
   disappears; the community "399" proposal is not the rule. The timer
   counts emulated cycles between those frames (resync11: 12:17.02,
   3091287748 cycles), splits per level on the same rule, keeps PB, best
   segments, attempts and history, and exports/imports LiveSplit `.lss`.
   Open from the list below: comparisons beyond PB (average, sum of best
   display), layout options, practice tools, per-level statistics and the
   fast-forwarded tally/fake-Daisy option.
   Requirements for the built-in timer (added 2026-10-11):
   * **Compatible with current speedrun categories.** Timing must match the
     rules of the existing leaderboards (speedrun.com categories, their
     start/end points, RTA vs. in-game/frame-count timing, version and
     hardware rules); look the rules up when designing, don't guess. Time
     is derived from emulated cycles (4194304 Hz, ~59.73 fps), never from
     the host display rate or wall clock, so a run timed by the port equals
     the same run on real hardware and on accepted emulators. Offer the
     official timing method per category plus a raw cycle/frame count.
     A community proposal (speedrun.com/posts/7gmf4) starts timing on the
     first frame the level timer shows 399 and ends when Tatanga explodes;
     check whether that is the current rule.
   * **Replaces common timing tools** (LiveSplit and similar): split files
     per category, start/reset, start offset, comparisons (personal best,
     best segments, average, sum of best, best possible time), live delta
     and possible time save, gold splits, attempt counter and run history,
     configurable layout, hotkeys and controller binds. Import/export
     LiveSplit `.lss` so runners keep their existing splits and history.
   * **Splits are automatic, one per level** (assumed to be the community
     convention; confirm against popular split files). The game knows when
     a level ends, so there is no manual split, undo or skip split. Worlds
     can be shown as groups of their three levels.
   * **Option: fast-forward the end-of-level score tally** (the remaining
     level time counting down into points) **and the fake Daisy scenes**
     after the bosses of worlds 1-3 (one option for both). Implemented
     as fast-forward, not as a jump: the emulated game runs these scenes
     unchanged but without waiting for the display, so game state, RNG and TAS sync are
     unaffected (hard rule). Because the timer counts emulated cycles, the
     run time still includes the full scenes and matches the speedrun rules
     with no extra correction; only wall-clock time is saved. The timer
     shows that these scenes were fast-forwarded, and since a recorded video
     would be shorter than the official time, the on-screen timer is what
     verifies such a run.
9. **Scroll back to the left** setting: Yes / No / Level default. Stock
   levels default to No (original behaviour); custom levels choose it in the
   level editor.
10. Support the v1.0 ROM too: two of the TASes found are for v1.0 (the
   "SML1.0Resync" and the "hard mode" TAS 6596M, which also starts from a
   Gambatte savestate on a GBC in DMG mode).
11. **Asset packs (presentation-only)** (added 2026-10-11). The user's ROM
   stays the source of all game data (legal distribution, TAS sync,
   achievement memory map); modern formats are optional layers on top:
   * Custom levels (items 6/7) get their own documented, versioned format
     (JSON or compact binary) holding only tile/enemy/object IDs, never ROM
     bytes, so levels can be shared and hosted legally.
   * PNG graphics packs: the renderer maps tile + palette to a replacement
     image if present (like Mesen HD packs); a `--dump-tiles` tool exports
     the user's ROM graphics as a template. Goes with items 2 and 5.
   * Audio packs: optional streamed OGG/Opus/FLAC per music track/effect
     (like MSU-1); the APU keeps running muted so timing and state are
     unchanged. Plus APU output options (band-limited synthesis, clean vs.
     speaker filter, stereo widening). No MIDI as a playback format (it
     cannot represent the APU's timbre); VGM export for remixers instead.

12. **TAS recorder and TAS player** (added 2026-10-12): both accessible through
    the game menus, including controller navigation. Record inputs and save
    recordings; choose saved recordings in the menu and play them back
    deterministically. Preserve cycle-exact stock TAS synchronization and keep
    custom gameplay recordings in their separate replay identity.

## TAS movies found (2026-10-08)

| Movie | ROM | Made on | Notes |
|-------|-----|---------|-------|
| TASVideos 2091M, `muggv3-supermarioland.vbm` | v1.1 | VBA-rr v24, DMG | published run; VBA timing is inaccurate |
| User file `SML1.1Resync.bk2` (TiKevin83) | v1.1 (header says v1.0 hash) | BizHawk/Gambatte, **GBA** (CGB in DMG mode) | console verified resync of 2091M |
| User file `SML1.0Resync.bk2` (TiKevin83) | v1.0 | BizHawk/Gambatte, GBA | resync for v1.0 |
| TASVideos 6596M hard mode `.bk2` | v1.0 | BizHawk 2.9.1/Gambatte, GBC (DMG mode) | starts from a savestate |

Consequence: the hardware model (`src/hw`) needs selectable variants — DMG
(default, the real Game Boy), CGB/GBA running a DMG game, and possibly a
VBA-rr compatible timing mode — with identical game code on top.

## Architecture decision: cycle-exact via a timing shadow

The original game is coupled to sub-frame CPU timing:

* music runs from the **timer interrupt** at 64 Hz (not per frame), and the
  game waits on music state (`$DFE9`, "wait until the song is over");
* six places read the **DIV register** as a random number (bonus-game prize
  and ladder, ending clouds, boss cry, an enemy script command);
* the HUD split uses an LY=15 STAT interrupt; VRAM reads during mode 3 fail.

So the port models the CPU's *timing* (not its instruction semantics):

* `src/hw/` — the Game Boy chipset without the CPU: memory, timer, LCD
  controller (timing + `lcd.c` renderer), APU, joypad, interrupt controller,
  MBC1. Shared by the port and the reference emulator, so identical CPU
  behaviour always gives identical pixels and samples.
* **Timing shadow** — a "virtual program counter" that walks the original
  instruction stream (decoded from the ROM) and charges each instruction's
  exact cycle cost to the chipset, dispatching interrupts at the exact
  instruction boundary. The C code only reports what the shadow cannot know:
  * conditional branch outcomes: `if (taken(lives == 0)) return;` means
    "the original's conditional branch here is taken iff lives == 0";
  * jump-table indices / computed jumps;
  * hardware accesses (`io_write(rSCX, x)`, `vram_write(...)`, `io_read(rDIV)`),
    which are performed at their exact cycle;
  * accesses to variables shared with interrupt handlers (sound requests,
    game state), via small helpers that sync the shadow first.
  Calls, returns, unconditional jumps and straight-line code are followed
  automatically, so C functions may be structured freely.
* The verification harness compares the port's reported branch sequence with
  the reference CPU's, which pinpoints any divergence to one instruction.

## Phases

| # | Phase | Status |
|---|-------|--------|
| 0 | Reference emulator (`verify/emu`), renderer, movie format, `emurun` | done |
| 1 | TAS import + sync on Gambatte (independent oracle) | done (SML1.1Resync, GBA model, 176 boot frames) |
| 2 | Hardware model shared by port and reference (`src/hw/machine.c`) | done; matches Gambatte on the whole TAS for DMG, CGB, AGB (docs/HARDWARE_ACCURACY.md) |
| 3 | Analysis tools: labels (`tools/symbols.py`), listing, coverage, call graph, function inventory | done |
| 4 | Timing shadow (`src/core/shadow.c`) + conformance test (`shadowtest`) | done, 0 mismatches on the whole TAS, all models |
| 5 | Function tester (`verify/fntest`), hybrid port (now `verify/tools/hybrid.c`), lockstep (`make check`) | done |
| 6 | Translate all code to readable C (bottom-up) | done: 205 of 205 inventoried routines in C, 230 registry entries |
| 7 | Main loop + interrupt handlers in C; remove the interpreter; pure-C build passes all checks | done: interpreter-free core, register B tracked by the shadow; all lockstep checks pass |
| 8 | SDL2 frontend polish (audio pacing, input config, scaling) | done for Linux: settings file, bindings, rate-controlled audio, vsync/clock pacing, scaling modes, AppImages (amd64, arm64); Windows builds but is untested |
| 9 | Widescreen, RetroAchievements + menu, speedrun tools, level editor, Windows package | in progress: Windows package, speedrun timer, settings menu and widescreen done (v0.2.0, v0.3.0); RetroAchievements, practice tools, editor open |

### How to verify (all must pass before a commit)

    make check SHELL="/bin/bash -o pipefail"  # fntest + shadowtest + TAS lockstep
    make check-states SHELL="/bin/bash -o pipefail"  # rare states + ending/death/reset, hybrid and C main

The shell option preserves failing statuses through the summary pipelines.

`build/resync11.movie` (per-poll input of the console-verified TAS) is made by
`build/gbref rom/sml.gb --model agb --frame-input reference/tas/resync11.fin 176 --record-polls build/resync11.movie`
after `tools/tas_convert.py reference/tas/SML1.1Resync.bk2 reference/tas/resync11.fin`.

The game-over regression movie is a short start-and-walk-right input that
loses all three lives and returns to the menu. Verify its function coverage
with:

    build/fntest rom/sml.gb --model agb --movie verify/movies/game-over.movie \
        --samples 100000 --polls 5081 --fuzz 8 KillMario ResetToMenu

Use the same movie with both poll-log runners for lockstep (5082 polls
with their default trailing inputs).

The tracked ending movie extends the console-verified input through Daisy's
reunion, the full credits and a button press after THE END. The pause/reset
movie toggles pause twice, presses A+B+Select+Start, then starts again.
`check-states` replays both ownership modes against fresh reference logs.
`build/portrun` is the pure-C port; `SML_C_MAIN=1 build/portrun_hybrid ...` gives the hybrid core C main-loop ownership.

`verify/fntest/state-cases.txt` supplies 27 independent cases across 11
routines: pipe entry/return, both ladder directions and landings, time-up,
pause/unpause, unused dispatcher entries and creation of Daisy's heart.
Each case copies the reference movie snapshot, seeds hexadecimal RAM/VRAM
address=value fields and runs both versions; existing fuzz validity
predicates still apply. To run the cases without movie-sampled function tests:

    build/fntest rom/sml.gb --model agb --movie build/resync11.movie \
        --polls 100 --samples 0 --fuzz 8 --cases verify/fntest/state-cases.txt \
        --require-coverage verify/fntest/state-required.txt

The deterministic coverage checkpoint runs separately with fuzz disabled:

    build/fntest rom/sml.gb --model agb --movie build/resync11.movie \
        --polls 100 --samples 0 --fuzz 0 --cases verify/fntest/coverage-cases.txt \
        --require-coverage verify/fntest/coverage-required.txt

Cases can seed hexadecimal `hl`, `de`, `bc` (16-bit pairs) and `a` (a byte),
in addition to memory and ROM bank. Each case restores the original base
snapshot before applying inputs. A requirement `NAME tested` needs a passing
sample; `NAME BANK:PC` needs both branch outcomes in that routine's original
ROM execution. Missing samples, wrong-bank sites and one-sided branches fail
the command. The input/contract regression test checks these failure modes.

The final summary reports untested registry entries outside that focused
case set; those are exercised by the separate movie/function checks.

### Translation status (subsystems, see docs/TRANSLATION.md)

| Subsystem | Owner | State |
|-----------|-------|-------|
| core helpers (hud, video, score, joypad) | main | done |
| sound engine (bank 3 6600-7FFF) | sub-agent | complete (57 entries), docs/SOUND_ENGINE.md |
| enemies/objects | sub-agent | done (all entries verified) |
| player/collision | sub-agent + main | done; docs/PLAYER.md |
| level (columns, pipes, items, timer, floaties, music) | main | done |
| game states / main loop / bonus game / ending / interrupts | main | done; pure-C runtime verified on all movies and models |

## Log

* 2026-10-14: added deterministic differential coverage contracts and register
  seeds to fntest. The square-2, menu, first-screen and targeted enemy/shooter
  checkpoint is implemented; pipe/bonus/pause transition coverage is enforced.
  Full core/ROM, frontend and anonymous-fault verification results are in
  NOTES.md; local evidence remains in build/session-*. No release or device
  verification is claimed.

* 2026-10-11 (phase 8, Linux): frontend finished and packaged. The
  settings file and resampler (started earlier as standalone modules) are
  wired into sdl_main.c with unit tests (`make check-frontend`). Pacing:
  `auto` uses vsync on ~60 Hz displays after a one-second warm-up (new
  windows on this X11 desktop present early for the first frames) and a
  two-second measurement, capped at 63.5 fps so a display that ignores
  vsync is detected and falls back to the clock. The clock pacer no longer
  catches up more than two frames, which used to overfill the audio queue.
  Measured here (59.98 Hz display): vsync pacing locks at 59.97-60.00 Hz;
  clock pacing runs at 59.745 fps; the audio queue stays at 61-69 ms
  around its 64 ms target without underruns. Hotkeys were tested in a
  nested Xephyr server (the desktop session was locked). Release builds:
  SDL2 2.30.0 from source with Zig (`tools/build_sdl2.sh`), headers from
  pinned focal packages, linked statically; AppImages are runtime + zstd
  squashfs (`packaging/linux/`). The binaries need only glibc 2.29.

* 2026-10-11 (phase 7): interpreter removed from the port. The shadow
  tracks register B (new event `reg_b`, also checked by fntest and
  shadowtest); 124 B-load sites annotated in C. The pure-C `portrun` matches
  every recording on every model listed under Current state. The hybrid core
  moved to verify/tools; `make check-models` added. Release binaries for
  linux-x86_64, linux-aarch64 and windows-x86_64 build without the
  interpreter.

* 2026-10-11 (remaining translations): all 205 inventoried routines now
  have C implementations, with 230 registered entries (60 added). New
  modules cover menu, level/death transitions, pipes, bonus ladders,
  reunion/credits, main dispatch/initialization/pause and interrupts.
  Preserve the original's off-by-one clears, reversed pipe coordinates,
  repeated hardware/shared reads, map wrapping and DMA delay. Long ending
  lockstep caught an incorrect heart tile (2A instead of the ROM's 84);
  a seeded transition now checks it directly. Focused fuzzing also caught
  repeated shared reads omitted from the kiss/credit scrolling paths.
  The sprite validity predicate counts actual layout tiles to prevent
  impossible fuzz states overflowing the ending OAM buffer into counters.
  No additional shared addresses were found in the ending analysis.
  The access table now has 2584 entries and includes ending/game-over/reset
  recordings; pause's LCDC bit operations need their indirect I/O classes.
  Vector hooks now record interrupt nesting before entering C, because the
  interpreter's trace hook has not run yet. The returning hybrid adapter
  leaves the non-returning reset chord to interpreter bootstrap; C-main
  owns reset by looping back to initialize_game. Shadow frame callbacks
  service platform frames while the C loop runs, including HALT waits.
  The fntest DMA replay applies the completed 160-byte transfer after the
  game's fixed wait, since the function harness does not advance hardware.
  Added seeded original-vs-C cases and a port poll-state dump option.
  The SDL build now actually links the translated game and adapters.
  Full function checks pass with fuzzing; 46000 shadow frames replay
  44707982 events and check 5312445 access/dispatch cycles with zero
  mismatches. Fresh movie checks match 44976 TAS, 8061 idle/demo, 65477
  ending, 5082 game-over and 706 pause/reset polls. Separate CGB and DMG
  TAS replays match all 44976 polls in both hybrid and C-main modes. Both hybrid and C-main
  modes pass the three tracked regression movies; C-main passes idle too.
  Smoke measures 230 entries, 108 exercised functions, 425 passing samples
  and 286 covered branches. Playbook floors and state checks are updated.
  Interpreter removal remains phase 7 work, especially the vblank B context.
  Logs: build/states_*, build/main_functions.log, build/interrupt_fix.log,
  build/ending_fix.log.


* 2026-10-11 (ten more functions): translated DrawFirstLevelScreen (0807),
  WalkRightAtExit (0EBB), PrepareMarioAndDaisy (0EE7), PrintVictoryMessage
  (0F8A), WriteDaisySprites (1029), ResetToMenu (1530), plus ending states
  UpdateExitWalk (0EA9), UpdateDaisyMeeting (0ECD), UpdateDaisyDialogue
  (0F6A) and UpdateDaisyMorph (0FFD). The port has 170 registered entries,
  165 of the 205 inventoried routines. The first-screen initialization
  preserves the extra byte in the object copy; dialogue preserves the
  low-byte map wrap and the original A result on countdown-only calls.
  All callers of PrintVictoryMessage were checked: only A is an output.
  The new cursor validity predicate requires a background-map address on
  calls that print; random high bytes otherwise send the original store
  into MBC or unrelated RAM. Exit walking reuses the player sprite/world
  invariants; first-screen drawing checks the column loader's level/screen
  invariants after its own column-index reset.
  The TAS run (`--samples 100000 --polls 44976 --fuzz 8`) passes all nine
  routines it reaches. Idle mode passes nine first-screen snapshots with
  60 fuzz cases. The new game-over fixture passes ResetToMenu and all
  three KillMario snapshots (24 fuzz cases), closing the death coverage gap.
  Remaining new one-sided paths: first-screen's underground-width test
  (0838) needs a pipe-route movie; the second STAT wait in dialogue (0FAD)
  never loops in these recordings, but deliberately repeats the original
  hardware read and conditional loop. The dialogue caller's row-wrap
  branch (0FBB) is one-sided in the TAS; the standalone printer fuzz run
  covers both directions. Other one-sided sites belong to existing callees.
  The complete function suite (`--samples 100 --fuzz 8`) has zero failures;
  its three TAS-only coverage gaps are KillMario, SetupChannel square 2
  and ResetToMenu (the game-over fixture covers the first and third).
  The 46000-frame shadow replay checks 44707868 events and 5312330
  hardware access/dispatch cycles with zero mismatches. Fresh TAS, idle and
  game-over hybrid logs match all 44976, 8061 and 5082 polls.
  Fresh shared-memory analysis adds no addresses to shared.h; regenerating
  the access table produces no diff. Function smoke measures 170 entries,
  97 exercised functions, 390 passing samples and 179 covered branches;
  the corresponding regression floors and hybrid-count floor are raised.
  Logs: `build/ten_*`.

* 2026-10-11 (autoscroll): GameState_0D (00:2376) translated and
  registered as UpdateAutoscrollGameplay. The submarine/plane frame keeps
  the original pause check, boss-goal check, shooter movement and shots,
  bank switches, reward arcs, enemies, timer and animation order. Function
  tests pass 1914 TAS snapshots (`--samples 100000 --fuzz 0`); the bounded
  fuzz run passes 100 snapshots plus 678 fuzz cases and covers both sides
  of all three branch sites in the new frame routine. An additional
  whole-movie fuzz run (`--samples 100000 --fuzz 8`) was stopped after
  ten minutes without completing; no pass is claimed for that run.
  The registry reuses
  the existing callee validity predicates, with the paused path needing
  no callee invariants. The full suite (`--samples 100 --fuzz 8`) has zero
  failures; the two existing movie coverage gaps remain.
  `make check` passes with shell pipefail enabled: 46000 shadow frames
  have zero mismatches, and fresh hybrid logs match all 44976 TAS polls.
  Fresh title/attract-mode logs match all 8061 idle polls. The port now
  has 160 translated entries (155 of 205 inventoried routines); registration
  and hybrid-count regression floors are raised to 160. Next: menu and
  level-start leaves. Logs: `build/autoscroll_*`.

* 2026-10-11 (later): MarioJumpAndFire (03:498B, with its internal
  4966/4975 paths) and GameState_00 (00:0627) translated and registered.
  Jump/run/fire control passes 4585 TAS snapshots plus 36680 fuzz cases
  and 781 attract-mode snapshots plus 6248 fuzz cases, with both sides
  of all 28 branch sites covered across the two movies. Normal gameplay
  passes 2713 TAS snapshots and 781 idle snapshots across the complete
  movies (`--samples 100000 --fuzz 0`); its bounded fuzz testing reuses
  the existing callee validity predicates. The complete function suite
  with `--samples 100 --fuzz 8` has zero failures; the two existing movie
  coverage gaps remain.
  Integrating the whole frame exposed an active-bank timing bug: C could
  update FFFD while the shadow was still finishing UpdateFloaties in
  bank 2; a timer interrupt then restored bank 1 prematurely. FFFD is
  now synchronized in bank helpers, DrawMario and StartLevelMusic.
  Regenerated the access table from all six reference runs (38 accesses
  reclassified, no new mixed-access sites). Added docs/PLAYER.md.
  Fresh hybrid runs match all 44976 TAS polls and all 8061 idle polls
  with 159 entries active. The 46000-frame shadow replay checks 44707868
  events / 5312330 hardware access/dispatch cycles with zero mismatches.
  `make check` passes with shell pipefail enabled. Smoke metrics and
  regression floors are updated from fresh measurements.
  Recounting registry addresses against the function
  inventory gives 154 of 205 routines translated (correcting the stale
  inventory count in the previous status). Logs: `build/jump_gameplay_*`,
  `build/jump_input_*`, `build/normal_gameplay_*`.

* 2026-10-11: three level-start helpers translated and registered:
  LoadLevelGraphics (00:05E7, tiles plus animation seed), PrepareHUD
  (00:060F, two visible map rows), and InitLevelCounters (00:3D1A,
  reward sprites, 400-unit clock and bonus-game defaults). The new
  helpers pass the full TAS function run with all seven branch sites
  covered; InitLevelCounters also passes eight fuzz variants. Graphics
  and HUD template loading read only ROM, so the RAM-read fuzzer has
  no inputs to vary. The all-function run (`--samples 100 --fuzz 8`)
  has zero failures; the two existing movie coverage gaps remain.
  Fresh hybrid lockstep matches all 44976 TAS polls and all 8061 idle
  polls with 157 entries active. Shadow replay over 46000 frames checks
  44557550 events / 5011701 hardware access/dispatch cycles with zero
  mismatches. Logs: `build/level_start_*`.
  Playbook floors raised to the current measured function-smoke and
  lag-frame shadow metrics, preserving the existing enemy-probe check.

* 2026-10-10 (later): 12 more routines in C, all fuzz-tested with full
  branch coverage and identical in the hybrid on the TAS and an idle run:
  UpdateTimerAndFloaties/UpdateGameTimer/UpdateFloaties (02:5844-5892,
  `level_floaties.c`), StartLevelMusic (07A3), the star timer (1F03), and
  the bank-3 per-frame Mario routines: pending jump step (48FC), jump arc
  (490D), attract-demo input (4A94; only the idle movie reaches it),
  block fragments (4AEA), bouncing block sprite (4B3C), falling into a pit
  (4B6F), growing (4B8A), shrinking/invulnerable blinking (4BB5).
  The serial `make check` fntest had a latent PlayMusic fuzz failure (the
  per-function parallel runs draw other random numbers and missed it): a
  fuzzed pattern list pointer activates an unused channel whose next
  pattern switch loads a stream pointer outside ROM. `sound_state_valid`
  now also requires that pointer to be in ROM, as the game's always are.

* 2026-10-10: lag-frame vblank sharing solved (see HANDOFF); full hybrid
  with all 141 routines identical to the original on the whole TAS.

* 2026-10-09 (evening): UpdateLevelColumns re-verified; level fuzz
  predicate; hybrid fix: a handler that returns (reti) straight to a
  translated entry (interrupt right after the `call`) left a stale
  isr_depth, so the shadow treated main code as handler code (the abort at
  00:1AB1, poll 378). Found the lag-frame vblank sharing gap (above).

* 2026-10-09: hardware model console-accurate (sub-agent), shadow ported to
  its timing, everything green with 77 translated routines in the hybrid
  port. Sub-agents are limited to two at a time to spare the usage quota.

* 2026-10-08: Gambatte (BizHawk's core) built as an independent oracle
  (`verify/gambatte/gbref.cpp`, mimics BizHawk's vblank-driven frames).
  `SML1.1Resync.bk2` syncs to the end on the GBA model with 176 boot frames
  (`--frame-input resync11.fin 176`), 44915 joypad polls. The published VBA
  run (2091M) does not sync on accurate emulators (expected).

* 2026-10-08: project created. Reference emulator passes Blargg cpu_instrs,
  instr_timing, mem_timing. Attract demo renders correctly. Measured on the
  demo: no lag frames, no VRAM writes while drawing, the STAT interrupt sets
  SCX during line 15's hblank, the main loop writes hScrollX at LY 27..67.
