# Changelog

The game shows the new sections of this file on the first start of a new
version. Newest first; one "## <version> - <date>" heading per release.

## 0.2.0 - 2026-10-16

- Speedrun timer (F8, or "timer = yes" under [speedrun] in settings.ini):
  times Any% by the speedrun.com rules, from the frame before 1-1 appears to
  the frame Tatanga disappears, in exact Game Boy time. Splits happen on
  their own, one per level, with the difference to your personal best and
  gold best segments. "category = normal+hard" times Normal + Hard Mode.
- Personal best, best segments and attempts are saved and kept as a
  LiveSplit file (speedrun-any.lss in the data directory); --import-lss
  takes over the splits of an existing LiveSplit file.
- Ready for the Warpzone launcher: installs it manages are updated by the
  launcher instead of the game, and the ROM it hands over is kept.

## 0.1.0 - 2026-10-16

- First preview release for Linux (AppImage and tarball), Windows, PortMaster
  handhelds and the Steam Deck.
- Plays Super Mario Land (World) v1.1 from your own ROM, cycle-exact: inputs,
  pictures, sound and random numbers match the original cartridge, so TAS
  recordings play back perfectly.
- Automatic updates: at start-up the game looks for a newer release in the
  background, checks its size and SHA-256 and installs it for the next start.
  Offline starts are not affected; set "check = no" under [update] in
  settings.ini to turn it off.
- This changelog appears once on the first start of every new version.
- Integer, sharp and nearest scaling, three palettes, fast forward (Tab or the
  right shoulder button), screenshots (F12) and game controller support.
- Steam Deck: an installer adds a desktop entry and a "Super Mario Land"
  non-Steam game with its own artwork.
