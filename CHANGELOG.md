# Changelog

The game shows the new sections of this file on the first start of a new
version. Newest first; one "## <version> - <date>" heading per release.

## 0.3.1 - 2026-10-16

- Started from the Warpzone launcher, the game uses the Super Mario Land ROM
  the launcher found on your disk without asking, and keeps a copy.

## 0.3.0 - 2026-10-16

- Settings menu: Esc, or Guide, Select+Start or Select+L1 on a controller
  (Select+L1 on PortMaster handhelds). Widescreen, scaling, palette,
  fullscreen, volume, sound, speedrun timer, automatic updates and the
  controls can be changed with keyboard or controller; changes are saved.
- Controls can be rebound in the menu: pick an action, press the new key or
  button.
- Widescreen: the picture grows sideways to fill 16:9, 16:10, ultrawide or
  any other screen (up to 32:9) and shows more of the level, decoded from the
  level data of your own ROM. The game itself is unchanged.
- Esc now opens the menu; Quit is in the menu.

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
