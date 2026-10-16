# Changelog

The game shows the new sections of this file on the first start of a new
version. Newest first; one "## <version> - <date>" heading per release.

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
