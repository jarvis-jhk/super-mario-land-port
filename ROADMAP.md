# Roadmap

**First rule: every TAS plays back perfectly at every commit. Enhancements
change presentation only; custom gameplay runs in its separate mode.**

JARVIS owns implementation and releases; player requests become items here.
Current implementation and verification commands: [docs/PLAN.md](docs/PLAN.md).

## Current priorities

1. Preserve the cycle-exact stock game: close differential-test coverage gaps
   without weakening validity checks. The deterministic coverage checkpoint is
   complete: square 2 pause notes, all menu branches, first-screen widths,
   targeted branches in 15 enemy/shooter entries, and pipe/bonus transition
   contracts. Continue whole-movie coverage review for remaining script,
   collision and hardware-wait paths. All existing TAS replays must pass
   before every commit and release.
2. **Four-platform release** (first pre-release v0.1.0, 2026-10-16): Linux
   x86_64 AppImage and tarball, Windows x86_64 zip with SDL2, PortMaster
   aarch64 package and Steam Deck installer, with checksum-verified automatic
   updates, offline fallback, opt-out and the in-game changelog on the first
   start of a new version ([docs/UPDATES.md](docs/UPDATES.md)). Open: the
   player's hardware tests (Windows, PortMaster handheld, Deck); fix what they
   report in follow-up releases.
3. **Steam Deck desktop and Steam integration** (requested 2026-10-11): install
   a desktop launcher and a non-Steam game named **Super Mario Land**, with
   bundled original library artwork and controller defaults. Done
   2026-10-16: installer and shortcut tool ship in v0.1.0; open: the player's
   real-Deck checks in the [acceptance criteria](docs/STEAM_DECK.md).
4. In-game settings and controller navigation, widescreen at any aspect ratio,
   and RetroAchievements integration with achievement overviews.
5. Speedrun timer and automatic level splits, practice/statistics tools and
   LiveSplit import/export. Verify current category rules before implementation;
   measure emulated cycles, including fast-forwarded tally/fake-Daisy scenes.
   Timer done 2026-10-16 (v0.2.0): speedrun.com Any% and Normal + Hard Mode
   timing in emulated cycles, automatic per-level splits, PB/best segments,
   LiveSplit .lss export/import ([docs/PLAYING.md](docs/PLAYING.md)). Open:
   practice/statistics tools, more comparisons/layouts, the fast-forward
   option for the tally and fake-Daisy scenes.
   **TAS recorder and TAS player** (requested 2026-10-12): reach both through
   the in-game menus, with controller navigation. Record inputs, save recordings,
   select a recording and replay it deterministically; preserve cycle-exact
   stock playback and separate replay identities for custom gameplay.
6. **Warpzone integration** (requested 2026-10-16): JARVIS's shared online
   layer (https://warpzone.virtualtabletop.io, the one allowed Jan domain in
   this repo) replaces an own lobby service. Done: the self-updater stays
   silent in launcher installs (`.warpzone-managed`, `WARPZONE_LAUNCHED=1`)
   and a launcher `--rom` hand-over is kept as sml.gb. Next: descriptor fixes
   (PR to jarvis-jhk/warpzone), the C API (tiers 1-3 per its
   docs/integration.md), leaderboards `any` (seasonal) and `1-1` fed by the
   speedrun timer with ruleset stock vs. lockfile hash and replay hashes.
   No chat or free-text player content.
7. **Shared screens for up to 16 players** (requested 2026-10-16: "playable
   cooperatively with synchronized splitscreen views (up to 16 players) each
   playing their own game - just for some shared fun"): every player runs an
   own stock game; a Warpzone room (preset `together`, star around the host,
   the host relays views/inputs) shows all games side by side, synchronized.
   Presentation only; each game stays cycle-exact.
8. Level/world editor and the custom-content runtime, with in-game sharing,
   browsing, voting and management through Warpzone.
9. **Mod ecosystem** (requested 2026-10-11): typed packages with transitive
   versioned dependencies, delivered by the Warpzone archive (server-side
   resolution into lockfiles, offline validation, `wz_sync_mods`), and
   sandboxed Lua for custom tile/player/enemy behavior.
   [Design and staged delivery](docs/MODS.md).

## Rules for every release

- Load the stock graphics, levels, music and timing tables from the player's
  own ROM. Distribute no ROM, ROM dumps or Nintendo artwork.
- Stock gameplay and presentation-only packs preserve the original state,
  timing, achievement memory map and TAS replay. Custom gameplay has an
  explicitly separate mode, saves, replay identity and leaderboard eligibility.
- Updates preserve ROMs, saves, settings and installed mods. Check release
  downloads and checksums before announcing a release.
- Report real runtime/installer/updater faults asynchronously with app,
  version and platform identification. Never include ROM data, paths containing
  personal information, credentials or gameplay content in reports. Keep the
  private reporting endpoint outside public source and public release assets;
  use a public relay if distributed clients need reporting.

## Further requested features

- ROM v1.0 support, including its TAS recordings and savestate-start case.
- Integer/sharp pixel scaling (Linux implemented; keep parity across targets).
- Presentation packs: PNG replacements and streamed OGG/Opus/FLAC music/effects
  while the original APU keeps running; ROM tile export for local authoring,
  VGM export, band-limited APU output, speaker filter and stereo widening.
- Optional leftward scrolling: Yes / No / Level default; stock default is No,
  custom levels specify their default.
- PortMaster compatibility beyond one handheld: older glibc/kernel targets,
  controller support and KMSDRM verification; never bundle librt, keep bundled
  libdrm out of the game's library search path.

## Status

The pure-C stock core and Linux frontend/AppImages are implemented. Release
packaging for all four platforms, safe updates, the changelog screen and the
Steam Deck installer exist (pre-release v0.1.0); Windows, PortMaster and Deck
runtime behaviour awaits the player's hardware tests. The speedrun timer
ships in v0.2.0. Warpzone online features, shared screens and the mod
runtime are planned work.

The setup audit, fresh history, roadmap and anonymous fault relay are complete.
GitHub reports public visibility (changed outside the workers). No-ROM CI
(2026-10-16) builds all three port targets and runs the no-ROM tests with a
placeholder access table; those binaries cannot play, so release binaries are
built locally with the generated table.

## Verification maintenance (former local queue)

- Keep shared-address synchronization and the local access classifications in
  sync. After touching shared.h, rebuild emurun_s from the current reference
  snapshot and run --shared-report, then regenerate the ignored access table.
- Sample new routines across the whole input movie (--samples 100000 --polls N),
  since the smoke test only samples the first 100 polls. Document unreachable
  branches; preserve the validity predicates.
- The translation queue is complete (230 entries, interpreter-free production
  core); historical work logs are in docs/PLAN.md. The remaining queue is
  coverage, Windows/arm64 runtime verification and phase 9 presentation work.
