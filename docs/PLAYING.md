# Playing

The port needs your own copy of the cartridge: **Super Mario Land (World)
(Rev A / v1.1)**, SHA1 `418203621b887caa090215d97e3f509b79affd3e`. It is
never bundled.

## Linux

Download or build (`make appimage-x86_64`, `make appimage-aarch64`, see
docs/BUILDING.md) `Super_Mario_Land_Port-<arch>.AppImage`, make it
executable and start it. It runs on any glibc 2.29+ distribution with X11
or XWayland and PulseAudio, PipeWire (pulse) or ALSA; nothing else needs
to be installed. Without FUSE, run it with `--appimage-extract-and-run`.

The plain binary `build/linux-<arch>/sml` (`make port-linux-<arch>`)
works the same way without the AppImage wrapper.

The ROM is looked up, in order, as `sml.gb` and `rom/sml.gb` in the
current directory, `sml.gb` next to the AppImage, next to the executable,
in `../rom/` from the executable, and in the data directory
`~/.local/share/super-mario-land-port/sml.gb`. `--rom FILE` overrides
the search. Started from a desktop launcher, errors appear in a message box.

## Windows, PortMaster and Steam Deck

Releases (github.com/jarvis-jhk/super-mario-land-port/releases) contain a
README.txt for every package. Windows (10 or later): unzip, put `sml.gb` next
to `sml.exe` or in `%APPDATA%\super-mario-land-port\`, start `sml.exe`.
PortMaster: unzip into the handheld's ports folder and put the ROM at
`ports/supermarioland/sml.gb`; Select+Start quits. Steam Deck: in Desktop Mode
run `install.sh [path/to/sml.gb]` from the steamdeck archive; it installs to
`~/.local/share/super-mario-land-port/`, adds a "Super Mario Land" desktop
entry and (after Steam is closed) a non-Steam game with artwork. Running it
again repairs the installation and keeps the ROM, settings and Steam entry.

## Updates and changelog

Release builds look for a newer GitHub release in the background at start-up,
download the file for their package next to the program, verify its size and
SHA-256 against the release's `update.txt` and only then replace the program;
the new version runs from the next start and a message says so. Offline
starts, read-only installs and development builds are unaffected. Turn it off
with `check = no` under `[update]` in settings.ini, `--no-update` or
`SML_NO_UPDATE=1`. The first start of a new version shows its changelog
(`--changelog` shows it again; A, Start or Enter continues). Details:
docs/UPDATES.md.

## Controls

| Game Boy | Keyboard | Controller |
|----------|----------|------------|
| D-pad | arrow keys | D-pad, left stick |
| A | X | south (A) |
| B | Z | west / east (X / B) |
| Start | Enter | Start |
| Select | Right Shift, Backspace | Back |

| Frontend | Keyboard | Controller |
|----------|----------|------------|
| fast forward (hold) | Tab | right shoulder |
| next palette | F9 | |
| next scaling mode | F10 | |
| fullscreen | F11, Alt+Enter | |
| screenshot | F12 | |
| quit | Esc | |

Every binding can be changed in the settings file. Palette, scaling and
fullscreen changes made with the keys are saved there on exit.
Screenshots go to `~/.local/share/super-mario-land-port/screenshots/`
(native 160x144, current palette).

## Settings

`~/.config/super-mario-land-port/settings.ini` (or `$XDG_CONFIG_HOME/...`)
is created with commented defaults on first start; `--config FILE` uses
another file. Command-line options override it for one run (`sml --help`).

* `[video]` `scale` (initial window size), `scaling`:
  `integer` (whole multiples, black borders), `sharp` (fills the window
  with square pixels and edges at most one screen pixel soft), `nearest`
  (fills the window, uneven pixel sizes); `fullscreen`; `vsync`;
  `palette` (`green`, `gray`, `pocket` or four `#RRGGBB` colours, lightest
  first); `fast_forward_speed` (0 = as fast as possible, N = N times).
* `sync`: `clock` paces at the Game Boy's exact 59.73 frames per second.
  `vsync` shows one game frame per display refresh, for displays at about
  60 Hz: the game runs up to 0.5 % fast, but scrolling is perfectly smooth.
  `auto` (default) uses vsync when the display reports 59-61 Hz and falls
  back to the clock if the display does not actually pace at about 60 Hz.
* `[audio]` `mute`, `volume` (0-100), `latency` (target output queue in
  ms). The APU output is resampled to the device rate and the ratio is
  nudged by at most 0.5 % to keep the queue at the target, so audio never
  drifts, crackles from underruns or builds up latency.
* `[keyboard]`, `[controller]`: comma-separated SDL key names (`X`,
  `Right Shift`, `Keypad Enter`...) and controller buttons (`a`, `b`, `x`,
  `y`, `back`, `start`, `leftshoulder`, `dpup`...) per action; `none`
  unbinds. `stick` and `deadzone` control the left stick.

None of these settings change the game: pacing, scaling, resampling and
fast forward only affect presentation, so the game computes exactly what
the cartridge computes and input movies stay in sync.

## Options for testing and TAS playback

    sml --model agb --movie build/resync11.movie   # console-verified TAS
    sml --record my.movie                           # save input on exit
    sml --headless 600 --screenshot out.png         # no window, no audio

`--model dmg|cgb|agb` selects the console whose timing is modelled
(DMG by default; the resync TAS was made on a Game Boy Advance). `--stats`
prints the measured frame rate, pacing mode and audio queue on exit.
