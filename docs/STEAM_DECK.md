# Steam Deck release integration

Requested 2026-10-11: a desktop entry and Steam integration with the proper
name and artwork, similar to s25coop. This is a first-release acceptance gate;
the current Linux AppImage alone does not satisfy it.

## Installation and launch

- Ship the Linux x86_64 build with a Desktop Mode installer, no root access or
  SteamOS changes required. Install to a stable user-owned location and provide
  a desktop entry named **Super Mario Land**, with the original application icon.
- Add a native non-Steam game named **Super Mario Land**. Use a stable launcher
  path, quoted executable and start directory, fullscreen launch options and
  controller defaults. Do not force Proton for this native Linux build.
- Keep the ROM user-supplied; explain its supported version and placement before
  first play. Preserve ROM, settings, saves, mods and the Steam shortcut on update.
- Reuse the updater/changelog shared with Linux. Updates must preserve the
  shortcut app ID so artwork and controller assignments remain associated.

## Reuse from s25coop

Reviewed `jarvis-jhk/s25coop/tools/coop/add-to-steam.py` on 2026-10-12. Reuse
its binary VDF parsing and editing strategy with license attribution when
implementing; keep this project's titles, executable paths and artwork separate.
It matches shortcuts by the executable field, preserves existing app IDs and
unrelated fields, detects duplicate matches, validates profiles before writing,
backs up the original VDF and uses fsync plus atomic replacement. It refuses
shortcut changes while Steam is running and checks again before replacement.
Keep these safeguards. Malformed VDFs must leave the user's file untouched.

Detect standard and Flatpak Steam installations, resolve symlinked roots once,
and let the user select among multiple accounts rather than modify every
profile without a choice. Repeated installation repairs the existing shortcut
instead of adding duplicates. Name migration retains its app ID. When Steam
is running, tell the user to exit it; do not kill Steam or overwrite its live
configuration. Artwork-only repair may run independently.

## Artwork

Bundle original, clearly titled artwork made for this port: portrait capsule
(600×900), landscape capsule (920×430), hero (1920×620), transparent logo and
square icon. Use the existing original four-shade hill/coin motif as a starting
point; no Nintendo sprites, box art or ROM-derived graphics. Supply artwork
locally so installation works offline. Install to the selected user's Steam
grid directory using the shortcut ID and the standard portrait/landscape/
hero/logo/icon names. Preserve user replacements unless repair is requested.

## Release checks

Test temporary Steam profiles before touching real hardware: repeated install,
name migration, stable app ID across updates, spaces in paths, multiple profiles,
Flatpak layout, malformed/truncated VDF, running Steam, original backup and
preservation of unrelated shortcuts and customized artwork. Installer/updater
faults use asynchronous sanitized reports, with no private endpoint in public
sources or artifacts; normal “Steam is running” and “ROM missing” messages do
not count as faults.

Before calling the Deck release ready, verify installation in Desktop Mode,
then name and all artwork in Gaming Mode; start/quit, fullscreen, audio,
D-pad/stick and A/B/Start/Select bindings, suspend/resume, offline launch and
update plus first-launch changelog. Headless checks cannot validate these real
hardware interactions. The Deck is currently outside the gateway device scope;
record this release gate and batch a hardware test when the installer is ready.
