# Running state

## Bonus-game flower crash (0.5.2) — 2026-10-19

- Requests t_mvfsal31i6jbh (Warpzone launcher crash report, exit 134 after
  89 s) and t_mvfsb9zjkxt9t (Jan: crash collecting the bonus-game flower):
  timing-shadow abort at 02:5DE5. Small Mario winning the flower runs the
  grow/shrink loop 02:5DCE-5DF6, which no access-table run had reached
  (the TAS is always big there), so its OAM-buffer reads were unclassified.
  Issue #4.
- New verify/movies/bonus-flower.movie: resync11 input with three idle
  polls at poll 6000 (the 1-3 boss fireball hits Mario, he finishes small),
  then A 26 polls into the ladder selection (flower, grow animation). The
  0.5.1 port reproduces the exact abort on it. Added to the access-table
  generator and to check-states. Fresh table = old table + 7 entries
  (02:5DD3-5DEF). Audit: no other indirect access in the bonus code
  (02:5832-5F14, 00:3D97-3EE5) is missing from the table.
- Gates (pipefail) passed: check/check-states/check-models (shadow 0
  mismatches; bonus-flower 8682 identical polls, all other movies identical),
  check-frontend with the 0.5.2 static binary, check-fault. Release binary
  plays the bonus-flower movie through. Pre-release v0.5.2; #4 and #5 (ntfy
  copy of the same crash) closed.
- Search scripts and logs: build/flower/. Gates ran in build/wt-flower
  (HEAD + this change; the main tree's uncommitted pack WIP left alone).

## macOS roadmap item — 2026-10-19

- Request t_mvewigcq10i7c8 (from adhoc, combined item mario-macos-support):
  added ROADMAP.md priority 10 "macOS support" after Jan's items (universal
  or two-arch build, macOS runner smoke test in CI, release asset, PLAYING
  notes, unsigned with Gatekeeper workaround) and its known risks (SDL2
  bundling, Metal/OpenGL and Retina scaling, controllers, Application
  Support paths, updater's /proc/self/exe/AppImage logic, curl, libwarpzone
  macOS build). Roadmap only, no implementation.
- The main tree still holds an earlier worker's uncommitted presentation-pack
  WIP (src/platform/pack.c, sdl_main.c, apu, Makefile, stb); left untouched
  and uncommitted. Gates for this commit ran on HEAD + ROADMAP.md in a
  separate worktree: check/check-states/check-models (shadow 0
  mismatches, all movies identical), check-frontend with the static binary,
  check-fault passed. Evidence: build/gate-macos-roadmap/.

## 0.5.1 crash fixes — 2026-10-18

- Request t_mvd8mk6e1eds8s (work session). Issue #3 (0.5.0 Linux fault,
  bonus-game ladder, twice) and #1 (time-up after the pause/reset movie):
  timing-shadow aborts on paths no access-table run had reached. The
  generator now also replays seeded random input branching off the TAS
  every 2500 polls (3 seeds, 3000 polls each) plus the new bonus-ladder,
  pipe-down, pipe-enter, time-up and continue-select movies, which
  check-states also compares (reference vs port vs hybrid).
- Fresh-seed sweep with the new table: 60 TAS-branch runs (4000 random
  polls) and 40 power-on random-play runs (20000 polls), seeds not used by
  the generator: 0 shadow aborts. Issue #1 repro runs 70000 frames.
- Gates (pipefail) passed: check/check-states/check-models (shadow 0
  mismatches; resync11 44976, ending 65477, game over 5082, pause/reset 706,
  bonus-ladder 8864, pipe-down 26120, pipe-enter 30396, time-up 17082,
  continue-select 29014 identical polls), check-frontend, check-fault.
  Evidence: build/r051-gates-*.log.
- Published pre-release https://github.com/jarvis-jhk/super-mario-land-port/releases/tag/v0.5.1
  (tag on e8d0b4c): 12 assets HTTP 200, expected sizes, byte-identical;
  release binary times resync11 at 12:17.02 and runs the bonus-ladder,
  pipe-enter, time-up and continue-select movies; a downloaded 0.5.0
  updated itself to 0.5.1 under Xvfb (hash equal to update.txt).
  Issues #3 and #1 closed.

## Warpzone fault reports — 2026-10-17

- Request t_mvc43532c85kj (automatic, warpzone lane): report crashes and
  faults through Warpzone's feedback API. Deferred, not a fault: the request
  itself says to start once libwarpzone 0.1.3 is released; latest release is
  v0.1.2 and https://warpzone.virtualtabletop.io/api/v1/feedback answers 404.
  Recorded under ROADMAP item 6. Crashes are already reported through the
  ntfy relay (docs/FAULT_REPORTS.md); the Warpzone detail must keep its
  fixed-payload rule (no logs/paths/memory/ROM data). Uncommitted 0.5.1 work
  in the tree left untouched.

## Status bar top-line fix — 2026-10-16

- Request t_mvbjus1m16maua (Jan): the top pixel row glitches sometimes;
  correct it on the display. Cause: the vblank handler writes SCX = SCY = 0
  for the status bar at its very end (after DrawColumn, DMA, score, timer);
  when it runs long (column draws, sound timer first) the first 1-4 lines
  are drawn with the previous frame's playfield scroll, so HUD text jumps
  sideways. Seen in 3923 of 44100 TAS frames (lines 0-3, rarely 4; only
  SCX/SCY differ; states 00/0D/1C/1D/22/23).
- Fix (presentation only, frontend sets Machine.fix_top_lines): when a line
  1-7 is drawn with SCX = SCY = 0 and every earlier line differs from it
  only in scroll, those lines are drawn again with its registers (scratch
  LcdState, skipped if the window is visible there); line_regs updated so
  widescreen sides agree. Emulation state untouched; the core verifiers use
  the stable hw snapshot anyway. `--display-report` counts redraws and
  frames still stale; check-frontend requires 0 stale on resync11 (3922
  redrawn). Before/after picture checked: frame 100 line 1 shifted 56 px
  before, correct after.
- Gates (pipefail) passed: check/check-states/check-models (shadow 0
  mismatches; 44976/65477/5082/706 identical polls plus the uncommitted
  bonus-ladder/pipe/time-up movies), check-frontend, check-fault.
  Evidence: build/topline-gates-*.log.
- Left uncommitted for the 0.5.1 work (not this request): the bonus-ladder,
  pipe and time-up movies, regen_access_table.py random branches, Makefile
  check-states list, BUILDING/PUBLIC_AUDIT notes and the CHANGELOG 0.5.1
  section (now also listing this fix). Issue #3 (0.5.0 bonus-game fault)
  is still open; Jan reported another bonus-ladder crash at 22:47 UTC.

## Level editor — 2026-10-16

- Roadmap item mario-level-editor (Opus quota session). Done when: a
  pre-release whose menu opens a level editor to place a world's tiles, item
  blocks and enemies on a scrolling grid, save the level as a ROM-free file
  and play it at once in a separate Custom mode, while every stock TAS stays
  identical. No new issues/PRs at start (only #1, known).
- src/platform/custom_level.c (no SDL): text format SML-LEVEL 1 (tiles per
  row, `enemy col row id`, `item col id`; docs/LEVELS.md), identity =
  SHA-256 of the canonical text. cl_build_rom writes the level in the game's
  column format into a 64 KiB in-memory copy in place of world W's first
  level (screen list, enemy list in the level bank; item and empty pipe
  lists in bank 3), using the measured stock level-data space per bank
  (bank 2 0x69A6-0x7873 = 1-2/1-3 data not shared with the title screen;
  bank 1 0x56CD-0x7DAE; bank 3 0x52CC-0x6417); identical screens once.
- Custom mode (sdl_main.c): starts from the title as a continue into world
  W (pokes C0A6/C0A8/C004 on state $0F, presses Start), clear = state 7 ->
  tally done, game over = $39/$3A, back to the editor; timer and
  achievements off; `--record` movies get `# custom level <sha256>` and
  start at the level's first poll. Enemy columns calibrated on runs:
  progress = $0C + (col - 24) / 2, odd columns +8 px (cols 40/50/61 spawn
  exactly there); Mario starts at cols 4-5; the level end stops with the
  last column at the right edge (no stale columns).
- Editor: grid with the world's graphics (VRAM from a silent capture run of
  the stock ROM), enemies as their first script sprite (tile lists at
  0x30B4), item letters, start box, enemy-start line; keyboard, mouse and
  controller; tile/enemy palettes as pictures; menu with play, save, name,
  world, length, open, new, back, quit. Autosave on leaving/playing.
- Tests: frontend_test (format, errors, build/read back on a blank image in
  all worlds, no repeat over item blocks, too-big refused); check-frontend
  clears verify/tests/levels/walk-w1..4 (polls 1009) and loses to a Chibibo
  (enemy-w1, game over polls 1401); verify/tests/editor_xvfb.sh (xdotool:
  tile, enemy, item, Ctrl+S, play, back to editor, quit; file checked,
  pictures checked by eye). Item kinds 28/2A/2C seen as mushroom/heart/star.
- Gates (pipefail) passed: check/check-states/check-models (shadow 0
  mismatches; resync11 44976, ending 65477, game over 5082, pause/reset 706
  identical polls), check-frontend incl. custom levels, check-fault.
  Evidence: build/editor-gates-*.log.
- Published pre-release https://github.com/jarvis-jhk/super-mario-land-port/releases/tag/v0.5.0
  (tag on acbce93): 12 assets HTTP 200, expected sizes, byte-identical;
  release binary times resync11 at 12:17.02 and clears walk-w3; a
  downloaded 0.4.0 updated itself to 0.5.0 (hash equal to update.txt).
  Item closed.
- Open (not blocks): warp pipes/underground rooms, autoscroll/shooter
  levels, boss gates, leftward-scroll option, Warpzone sharing; Jan's test
  with a real controller/handheld (d-pad repeat, Start menu, Select kinds).

## RetroAchievements — 2026-10-16

- Roadmap item mario-retroachievements (Opus quota session). Done when: a
  pre-release where the player logs in to RetroAchievements in the settings
  menu, the official set loads for the v1.1 ROM, unlocks pop up in game and
  a menu page lists all achievements with progress, while every TAS stays
  identical.
- rcheevos v12.5.0 vendored in src/third_party/rcheevos (MIT; no
  raintegration, libretro, disc/zip hashing or validator). The v1.1 ROM's
  MD5 b259feb4... resolves to RA game 504 (checked against the server;
  the set itself needs a login, so the official conditions were not run
  here). src/platform/achievements.c: memory via machine_peek after every
  frame, curl in a worker thread (request body in a 0600 temp file, never on
  the command line), callbacks on the main thread, softcore only, token in
  <data>/retroachievements.login. Headless runs never go online.
- Menu: RetroAchievements page (on/off, pop-ups, sign in/out, list); sign-in
  text entry by keyboard or controller letter grid. Pop-ups top centre
  (4 s, 1.5 s while more wait), measured progress bottom left.
- Tests: check-frontend now plays resync11 against verify/tests/ra_fake
  (SML_RA_FAKE): unlock frames 2495/8310/30190/44041 must match
  expected.txt and the speedrun time stays 12:17.02 (cycles 3091287748).
  verify/tests/achievements_xvfb.sh: sign out/in via xdotool, list picture,
  fast-forwarded TAS shows the "Unlocked: Coin Collector" pop-up (checked by
  eye). Real server: a wrong login over curl gets "Invalid user/password
  combination" shown in the menu. Overhead: none measurable headless.
- Issue #2 (two M0-dev Linux fault reports at 11:13/11:14 UTC): developer
  builds from a worker's test runs, not a release; the gates ran build/sml
  without SML_NO_REPORT. The Makefile now exports SML_NO_REPORT=1 (the fault
  test clears it itself). Closed with that note.
- Open for Jan (not a block): sign in with his real account, unlock one
  official achievement, check it on his RA profile; whether RA accepts the
  "SuperMarioLandPort" user agent for softcore unlocks is only visible there.
- Gates (pipefail) passed: check/check-states/check-models (shadow 0
  mismatches; resync11 44976, ending 65477, game over 5082, pause/reset 706
  identical polls), check-frontend incl. achievements and widescreen 97.5 %,
  check-fault. Evidence: build/ach-gates-*.log.
- Published pre-release https://github.com/jarvis-jhk/super-mario-land-port/releases/tag/v0.4.0
  (tag on 3540356): 12 assets HTTP 200, expected sizes, byte-identical;
  release binary times resync11 at 12:17.02 with the test set unlocking on
  the expected frames; a downloaded 0.3.1 updated itself to 0.4.0 (hash
  equal to update.txt). Item closed.

## Warpzone found-ROM hand-over — 2026-10-16

- Request t_mvb0nern42hnrt (automatic, Warpzone lane): launcher >= 0.1.2
  passes WARPZONE_DATA_ROM=<path> on every start. Done: without --rom, a
  v1.1 file there is used and copied to the data dir as sml.gb (once); any
  other file is ignored with a stderr note and the normal search goes on.
  Tested headless: found ROM used + identical copy kept; bad file -> falls
  back to the copy; bad file and no copy -> normal "ROM not found"; --rom
  wins and is not copied without WARPZONE_LAUNCHED=1.
- Descriptor games/super-mario-land-port.toml checked: files, import_args
  (--rom {path}) and the v1.1/v1.0 fingerprints match the importer; the
  missing present path, exec and exact asset names are already in open
  warpzone PR #4 (commented there).
- Gates (pipefail) passed again (core 0 mismatches, frontend incl.
  widescreen, fault): build/wzdata-gates-*.log. Published pre-release
  https://github.com/jarvis-jhk/super-mario-land-port/releases/tag/v0.3.1
  (tag on 7af4d0c): 12 assets HTTP 200, expected sizes, byte-identical;
  release binary times resync11 at 12:17.02; a downloaded 0.3.0 updated
  itself to 0.3.1 (hash equal to update.txt).

## Settings & widescreen — 2026-10-16

- Roadmap item mario-settings-widescreen. Done when: a pre-release where Esc
  or the controller opens an in-game settings menu (widescreen, scaling,
  palette, volume, timer, controls) navigable by pad, and widescreen fills
  any window aspect with level scenery from level data while all TASes stay
  identical. No new issues/PRs at start (only #1, known).
- Menu (sdl_main.c run_menu): the core is simply not stepped while it is
  open; Esc/F1, Guide, Select+Start, Select+L1 (PortMaster: gptokeyb quits on
  Select+Start). Binding pairs "a+b" for controller buttons; the menu action
  wins over others on the same key (old files keep quit = Escape). Controls
  page rebinds by key/button press. Changes are saved on close (only fields
  that differ from the start-up values; CLI overrides stay one-off).
  After closing, game input is ignored until all buttons are released.
- Widescreen (widescreen.c, no SDL): Machine.line_regs records each line's
  LCD registers (written only, never read by the core). Sides: map columns
  drawn since the level part was entered (WideState tracks the first one),
  columns ahead decoded from the ROM level data (bank = hActiveRomBank),
  edge-crossing sprites; HUD, window lines (pause/game over), title, bonus,
  underground, ending stay blank. Data before the first drawn column can be
  the underground room (same screen list), so it stays blank.
- Verified: decoded right side vs the game's own picture 40 frames later on
  resync11: 97.6 % pixel match (rest: enemies, animated water in 3-2); now
  in check-frontend (verify/tests/widescreen_check.py, floor 95 %).
  verify/tests/menu_xvfb.sh drives the menu under Xvfb with xdotool (Esc,
  widescreen on, palette gray, F12, Esc): both saved, menu.png and a 20:9
  window picture checked by eye. Unit tests for widescreen values/widths.
- Open for Jan (not a block): menu with real controllers (Guide/Select+L1 on
  the Knulli handheld), widescreen on the Deck and an ultrawide screen.
- Gates (pipefail, after make stable-hw) passed: check/check-states/
  check-models (shadow 0 mismatches; resync11 44976, ending 65477, game over
  5082, pause/reset 706 identical polls), check-frontend incl. widescreen
  check 97.5 %, check-fault. Evidence: build/settings-gates-*.log.
- Published pre-release https://github.com/jarvis-jhk/super-mario-land-port/releases/tag/v0.3.0
  (tag on 5298021): 12 assets, each HTTP 200 with the expected size and
  byte-identical; release binary times resync11 at 12:17.02; a downloaded
  v0.2.0 updated itself to 0.3.0 (hash equal to update.txt). Item closed.

## Warpzone handoff — 2026-10-16

- Request t_mvavxten267c5a (automatic, from the Warpzone lane). Added
  ROADMAP items 6 (Warpzone integration) and 7 (16-player shared screens,
  Jan's verbatim request); items 8/9 (editor, mods) now go through Warpzone;
  docs/MODS.md: server-side resolution into lockfiles, offline validation.
- Done: self-updater silent with WARPZONE_LAUNCHED=1 or .warpzone-managed in
  the program's folder or its parent (Xvfb file:// test: plain install
  updates, the three managed cases do not); with WARPZONE_LAUNCHED=1 a --rom
  hand-over is copied to the data dir as sml.gb once (tested).
- Descriptor games/super-mario-land-port.toml: [launch] has no exec (needs
  linux/deck "*/sml", windows "*/sml.exe"); asset patterns and SHA256SUMS
  match v0.1.0 names; import_args --rom now persists; present could list
  ~/.local/share/super-mario-land-port/sml.gb. Sent as
  https://github.com/jarvis-jhk/warpzone/pull/4 (exec, exact asset names,
  present). Open: C API tiers 1-3, leaderboards, shared screens.

## Speedrun timer — 2026-10-16

- Roadmap item mario-speedrun-timer. Done when: a pre-release shows an
  optional in-game speedrun timer that times Any% by the speedrun.com rule in
  emulated frames, splits automatically per level, keeps PB/best segments and
  exports LiveSplit .lss, verified on the TAS. No open issues/PRs at start.
- Rules (speedrun.com API, game sml1, 2026-10-16): real time, emulators
  allowed; Any% starts on the frame before the level and UI load in, ends on
  the frame Tatanga's sprite disappears; Normal + Hard Mode runs both quests.
  The "399" community proposal is not the rule.
- Added src/platform/speedrun.c (no SDL): reads FFB3/FFB4/FF9F/D007 and OAM
  once per video frame (read-only), times in emulated cycles; start = the
  blank frame before 1-1 (frame 38 on resync11, checked against the frames),
  split per level on the same rule, end = first frame without Tatanga's tiles
  ($AC-$AF/$BC-$BF/$CB-$CF) after D007 = $60 in 4-3 (frame 44043, checked).
  Demo/attract play is ignored; returning to the title ends an attempt.
  PB, best segments, attempts, last 32 runs in speedrun-<cat>.txt plus an
  .lss copy in the data dir; --import-lss; F8 / [speedrun] timer / --timer;
  overlay bottom right with delta colours and an FF mark. --frames N and a
  window --screenshot were added for tests.
- Verified: frontend_test (synthetic runs, deltas, golds, resets, Normal +
  Hard through the title demo, file and .lss round trips, LiveSplit shapes);
  resync11 headless report 12:17.02 = 3091287748 cycles (now in
  check-frontend); ending movie with normal+hard keeps running after the
  ending; game-over movie resets; Xvfb window screenshot shows the overlay
  and the 1-1 split (0:38.19). Windows and PortMaster targets compile.
- Found, not caused by this item: the pause-reset movie followed by idle
  input aborts in the timing shadow at the time-up (00:1D04); filed issue #1.
- Open for Jan (not a block): playing with the timer on real setups;
  PortMaster has no default controller bind for the timer (settings.ini).
- Gates (pipefail) passed: check/check-states/check-models (shadow 0
  mismatches, resync11 44976 identical polls AGB/CGB/DMG), check-frontend
  incl. the timer check, check-fault. Evidence: build/speedrun-gates-*.log.
- Published pre-release https://github.com/jarvis-jhk/super-mario-land-port/releases/tag/v0.2.0
  (tag on 82cbf71): 12 assets, each HTTP 200 with the expected size; the
  release binary times resync11 at 12:17.02; a downloaded v0.1.0 updated
  itself from GitHub to 0.2.0 (hash equal to update.txt). Item closed.

## Deck integration — 2026-10-16

- Roadmap item mario-deck-integration. Done when: a GitHub prerelease ships a
  Steam Deck package whose installer adds a desktop entry and a non-Steam game
  "Super Mario Land" with original artwork, verified on a test Steam profile.
  No open issues/PRs.
- Already met by v0.1.0: downloaded super-mario-land-port-0.1.0-steamdeck.tar.gz
  (HTTP 200, 1198405 bytes) and ran its install.sh on a fake HOME with one
  Steam account: AppImage, desktop entry, icon, shortcut "Super Mario Land"
  (quoted exe, start dir, --fullscreen) and all five grid images. No code change.
  Item closed; the Gaming Mode checks in docs/STEAM_DECK.md are Jan's open
  hardware test, fixes follow his reports. Gates passed (pipefail):
  build/deck-int-{core,frontend,fault}.log.

## Four-platform release v0.1.0 — 2026-10-16

- Roadmap item mario-four-platform-release. Done when: a GitHub prerelease
  offers Linux AppImage/tarball, Windows zip, PortMaster zip and Steam Deck
  installer (all HTTP 200, expected size) that checksum-verify updates and
  show the changelog on first launch of a new version. No open issues/PRs.
- Added: built-in CHANGELOG.md screen (own 5x7 font, before the core starts),
  background updater (GitHub release list -> update.txt -> download next to
  the program -> size + SHA-256 -> atomic rename; opt-out [update] check = no,
  --no-update, SML_NO_UPDATE=1), `make port-portmaster-aarch64` (system SDL2),
  packaging/make_release.sh, PortMaster launcher, Steam Deck install.sh and an
  own steam_shortcut.py (s25coop's is GPL-2.0; this repo has no licence, so it
  was not copied) with original generated artwork. docs/UPDATES.md.
- Verified here: unit tests (sha256 vectors, versions, manifest, changelog);
  Xvfb runs with a file:// release tree: 0.0.9 -> 0.1.0 self-update of the
  plain binary, bad checksum keeps the old binary, offline is silent,
  read-only dir shows "Version 0.2.0 is available"; changelog screen once per
  version (screenshot checked). Deck installer on a fake HOME/Steam profile:
  AppImage, desktop entry, icon, ROM copy, shortcut + 5 grid images, repeat
  install idempotent; the installed AppImage updated itself in place.
  steam_shortcut_test covers the docs/STEAM_DECK.md VDF cases (Steam-running
  case needs an exec-capable TMPDIR; passed with TMPDIR=build).
- Not verifiable here (Jan's hardware tests, not blockers): Windows runtime
  (cross-built, UCRT + SDL2.dll), PortMaster on the Knulli handheld (dynamic
  system SDL2, gptokeyb Select+Start), Steam Deck Gaming Mode checks listed in
  docs/STEAM_DECK.md, AppImage via FUSE on SteamOS.
- Release binaries link the locally generated access table (never published
  as source); see the PUBLIC_AUDIT.md "Release binaries" row.
- Published pre-release https://github.com/jarvis-jhk/super-mario-land-port/releases/tag/v0.1.0
  (tag on 1a604c2): 12 assets, each HTTP 200 with the expected size. Live
  check: a 0.0.9 build updated itself from this GitHub release to 0.1.0 (hash
  equal to update.txt). Item done; follow-ups come from Jan's hardware tests.
- Gates passed with Bash pipefail before commit: check/check-states/
  check-models (shadow 46000 frames, 47441151 events, 0 mismatches; resync11
  44976 identical polls AGB/CGB/DMG; ending 65477, game over 5082, pause/reset
  706), check-frontend (incl. check-packaging) with build/linux-x86_64/sml,
  check-fault. Evidence: build/release-gates-{core,frontend,fault}.log.

## No-ROM CI — 2026-10-16

- Roadmap item mario-no-rom-ci. Done when: a green GitHub Actions run on main
  builds the Linux x86_64/aarch64 and Windows port plus tools without any ROM
  or generated table and passes the no-ROM tests. No open issues or PRs.
- The access table is a run-time input of the timing shadow, not only a build
  input, so it cannot be dropped. Added the committed placeholder
  src/core/access_table_stub.c (size -1), selected only explicitly with
  make ACCESS_TABLE=...; local builds still use the generated table. Stub
  binaries print "no-ROM CI build" and exit 1 before loading a ROM or
  installing fault reporting, so they cannot play or send reports.
- .github/workflows/ci.yml: pinned toolchain fetch, make all, frontend unit
  tests, check-fault, port-all with the stub, and the refusal check.
  Simulated from a clean tracked-file export (fresh toolchain fetch); this
  container lacks pkg-config, so the static SDL2 configure was reused from
  build/ there; the workflow installs pkg-config. tools/build_sdl2.sh now
  caps make -j at 10.
- Open: CI-built *release* binaries would need the table derived on the
  player's machine; releases are built locally with the generated table.
- ROM gates passed with Bash pipefail (check/check-states/check-models,
  check-frontend with the static binary, check-fault): shadow 46000 frames,
  0 mismatches; resync11 44976 identical polls AGB/CGB/DMG; ending 65477,
  game over 5082, pause/reset 706. Evidence: build/norom-ci-gates.log.

## Deterministic coverage checkpoint — 2026-10-14

- GitHub triage found no open issues or PRs, including fault reports. The
  anonymous fault-report workflow has successful scheduled runs. The setup
  audit, fresh history, roadmap and relay are implemented. GitHub now reports
  public visibility; this worker did not change it. No release is authorized
  under the current session instructions. Reconcile the external gate evidence
  before releasing.
- Added register arguments and mandatory coverage contracts to fntest. The
  contracts fail for missing passing samples or missing branch outcomes,
  including wrong-bank sites. Invalid register/memory fields and malformed
  requirements are checked with real-ROM regression tests.
- Added 91 deterministic cases across 19 entries and 65 coverage requirements:
  both square-2 pause notes, repeated pause beeps, all 21 menu branches,
  normal/underground/title first-screen widths, and targeted coverage across 15
  enemy/shooter entries. Existing rare-state cases have 26 requirements for
  pipe entry/return, bonus landings, time-up, pause, unused dispatch and reunion.
  check-states runs the new checkpoint with fuzz disabled, preserving existing
  fuzzy cases and all movie checks. No game code or validity predicate changed.
- The focused checkpoint also passes on CGB and DMG. Detailed coverage and
  sampled data remain local in build/session-*. No ROM bytes, artwork or
  generated execution reports were added to the public tree; the updated
  content audit and privacy scan are clean.
- Required make check/check-states/check-models passed with Bash pipefail:
  fuzzed fntest and seeded states have zero failures; shadowtest replays 46000
  frames, 47441151 events and 5312445 hardware access/dispatch cycles with zero
  mismatches. resync11 has 44976 identical polls on AGB/CGB/DMG; ending has
  65477, game over 5082 and pause/reset 706, identical in pure C and hybrid.
  Evidence: build/session-rom-gates.log. The final spawn cases use actual
  level-entry addresses in the player's ROM and pass the deterministic
  contracts on all three models (build/session-focused*.log).
- check-frontend passed using the documented static Linux binary (unit tests
  and 600 headless frames); check-fault passed with a local fake sender. No
  synthetic report was sent. Evidence: build/session-{frontend,fault}.log.
  git diff --check is clean; build/ remains below 700 MiB.
- Remaining work: full no-ROM core/platform CI, release packages and safe
  updates/changelog. The targeted checkpoint does not establish full enemy
  script/collision coverage; hardware wait loops need separate phase-sensitive
  review. See docs/PLAN.md for the explicit remaining coverage limits.
- Combined-roadmap synchronization is unavailable in this worker environment.
  Filed an infrastructure request with the self lane; continued with this
  repository's priorities. No new roadmap feature was introduced.

## TAS roadmap request — 2026-10-12

- Added a TAS recorder and TAS player to the speedrun/practice roadmap and
  docs/PLAN.md. Both must be reachable through the game menus with controller
  navigation; recording, saving, selecting and deterministic replay are planned.
- Stock cycle-exact playback and separate custom replay identities remain
  requirements. This request changes documentation only; implementation is
  pending alongside the other phase 9 features.
- Existing asynchronous fault reporting already forwards real faults to the
  mario lane through the anonymous relay; no runtime changes are needed here.
- Required pre-commit gates passed: check/check-states/check-models with Bash
  pipefail, check-frontend with the documented static Linux binary, check-fault
  and git diff --check. Evidence: build/tas-roadmap-{core,frontend,fault}.log.
  Shadow replay: 46000 frames, zero mismatches; TAS: 44976 identical polls on
  AGB/CGB/DMG. No release or device work was needed.
- Push retry (2026-10-12): worker HTTPS authentication is available; the remote
  default branch is main. Publish the original roadmap commit 95871c4 unchanged,
  followed by this running-state correction, using an environment-only credential
  helper. The previous missing-credentials diagnosis is superseded.
- Fresh pre-push gates passed: check/check-states/check-models with Bash pipefail,
  check-frontend using the documented static Linux binary, and check-fault.
  Evidence: build/tas-push-{core,frontend,fault}.log. No release or device work.

## M0 bootstrap — 2026-10-12

This checkout is ready for the separate public visibility gate. The
GitHub repository remains private; no release is authorized in this session.

- Read the local handoff and docs/PLAN.md. No prior CLAUDE.md/AGENTS.md existed;
  CLAUDE.md now records lane ownership, legal/data limits, release policy and gates.
- Folded the local pipeline queue and player roadmap items 1–11 into ROADMAP.md,
  preserving typed mods/Lua design and Steam Deck desktop/Steam requirements.
- Removed private notification tooling and pipeline state. Added a public random
  ntfy topic, SDL fatal-fault reporting and the scheduled issue/queue relay using
  only the existing GitHub Actions secret. No private report URL in the tree.
- Audit details are in docs/PUBLIC_AUDIT.md. Copied reward/floaty/position tables
  now read ROM data at runtime. ROM-run access classifications and third-party
  test-font artwork stay local-only and regenerate without private history.
- Fresh classification generation yields 2583 entries; the single stale row
  removed was RAM-only and therefore has the same default classification.
- The old history bundle's orchestration directories contain no needed source;
  useful remaining branch-coverage tasks are on the roadmap. Keep a local old
  history branch; push only the fresh single-commit main tree.
- Runtime hardware tests on Windows, PortMaster and Deck remain outstanding.
  Full-core no-ROM CI also needs removal of the local table build dependency;
  currently the independent emulator and frontend unit tests build without ROMs.
- Existing copied oracle binary requires newer host libraries than this worker;
  rebuilt the Gambatte oracle with the pinned Zig C++ compiler in this container.
  Regenerated resync11 from the original local TAS with 176 boot frames.

After the evidence passes and main is pushed, send exactly one adhoc-lane
visibility-gate request with the pushed SHA, audit verdicts, scans and gate
results. Next scope is CI plus M0 packaging/updating/changelog on all targets.

## Verified evidence

- Final make check/check-states/check-models passed with Bash pipefail (exit 0).
  Fuzzed function checks and seeded rare-state checks: zero failures. Shadow:
  46000 frames, 47441151 events, 5312445 access/dispatch cycles, zero mismatches.
- Supported v1.1 resync11: 44976 identical polls on AGB/CGB/DMG; AGB hybrid also
  matches. Ending: 65477, game over: 5082, pause/reset: 706 polls, identical in
  pure C and hybrid. Full logs remain local in build/final-gates.log.
- Linux static build and check-frontend passed: settings/audio tests plus
  600-frame SDL headless playback. Host dynamic SDL stubs require newer libc;
  the documented static-binary override avoids that container dependency.
- Clean exported no-ROM tree builds emulator/CPU runner and frontend unit tests;
  those tests and the anonymous relay tests pass. Windows reporter cross-compiles.
  Fake sender tests verify normal exit, crash, duplicate suppression, opt-out
  and no network wait. No synthetic reports posted to the public topic.
- The public workflow's shell syntax and payload/routing filter passed; the
  existing required GitHub Actions secret is present. It uses no private URL
  in source. Actual scheduled delivery remains best-effort as documented.
- Privacy scan is clean: no personal names/addresses, private domains/topics,
  LAN addresses, home paths, secret endpoints, ROMs, binaries or artwork blobs.
- History inspection: 111 distinct bundle heads, six orchestration gitlinks;
  17 historical path changes, all gitlinks, no tracked auto-subagent source.
  Old history is retained on local/pre-jarvis-history and must never be pushed.

Visibility-report delivery is recorded locally in
build/visibility-report-response.json after the push; check it before filing a
duplicate. The pushed main is a single root commit with the required noreply
identity. No release, visibility change or device work belongs to this bootstrap.
