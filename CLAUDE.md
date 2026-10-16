# Mario lane instructions

Read docs/PLAN.md, ROADMAP.md and NOTES.md at the start; update running state
at the end. JARVIS owns this repository, roadmap, issues, CI and releases. The
player adds roadmap requests. This lane edits only this project; request changes
in other projects through their lanes.

First rule: every stock TAS plays back perfectly at every commit, including
presentation enhancements. Enhancements change presentation only. Custom
scripted gameplay is a separate mode with separate saves and replay identity.

## ROM and public scope

Use the player's own v1.1 ROM at rom/sml.gb. Never publish ROMs, extracted bytes,
ROM reports, disassemblies, snapshots, generated ROM analysis or game artwork.
Read docs/PUBLIC_AUDIT.md before changing fixtures/tables. Runtime game assets
come from the ROM; instruction addresses and translated behavior are code,
not substitutes for ROM assets. No private endpoints, identities, device paths
or worker state may enter the public tree or release assets. Secrets come from
environment variables or GitHub Actions secrets only. Escalate takedown notices
to the owner; do not argue them yourself.

Workers never change repository visibility. Releases are GitHub pre-releases
built with packaging/make_release.sh after the gates below; JARVIS dispatches
release work through the roadmap. Preserve old
history locally; push only the fresh main history with author AND committer
jarvis-jhk <326430845+jarvis-jhk@users.noreply.github.com>.

## Work and verification

Use at most 10 CPUs and keep build/ bounded. This worker has no device targets;
use headless SDL, Xvfb and the ROM-based reference runners locally. Device steps
in other documentation must use jarvis-gw; no direct SSH or adb here.

Before every commit/push and release, run with Bash pipefail:

    make check check-states check-models SHELL="/bin/bash -o pipefail"
    make check-frontend SHELL="/bin/bash -o pipefail"
    make check-fault

check includes fuzzed fntest, the 46000-frame shadowtest and resync11 lockstep.
Use make stable-hw to refresh the independent emulator snapshot. Generate
build/resync11.movie with the Gambatte oracle as documented in docs/PLAN.md;
generate the ignored access table from fresh ROM runs. Never weaken validity
predicates to make a test pass. Gate evidence stays in build/; summaries go in
NOTES.md. No-ROM CI can build the emulator/tools and frontend unit tests; a
full port build currently needs the locally generated access table.

## Releases and reports

M0 needs Linux, Windows, PortMaster and Steam Deck packages, safe verified
updates and an in-game changelog. Follow ROADMAP.md and docs/STEAM_DECK.md.
Verify assets download with HTTP 200 and expected sizes before announcing.
Only announce ready releases, with a short German message and release URL.
Do not send progress notifications from automatic requests.

Real faults go through the public anonymous ntfy relay described in
docs/FAULT_REPORTS.md, then a GitHub issue and the mario lane. Never upload
logs, paths, memory, ROM data, credentials or personal data. Ordinary missing
ROMs, invalid options and offline network failures are not crash reports.
