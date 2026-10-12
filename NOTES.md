# Running state

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
