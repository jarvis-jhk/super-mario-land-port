# Running state

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
