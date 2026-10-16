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
Started by the Warpzone launcher, the ROM it found on your disk
(`WARPZONE_DATA_ROM`) is used without asking when it is v1.1, and a copy is
kept as `sml.gb` in the data directory; any other file there is ignored.

## Windows, PortMaster and Steam Deck

Releases (github.com/jarvis-jhk/super-mario-land-port/releases) contain a
README.txt for every package. Windows (10 or later): unzip, put `sml.gb` next
to `sml.exe` or in `%APPDATA%\super-mario-land-port\`, start `sml.exe`.
PortMaster: unzip into the handheld's ports folder and put the ROM at
`ports/supermarioland/sml.gb`; Select+Start quits, Select+L1 opens the menu. Steam Deck: in Desktop Mode
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
| speedrun timer on/off | F8 | |
| settings menu | Esc, F1 | Guide, Select+Start, Select+L1 |

On PortMaster handhelds Select+Start quits the port (gptokeyb); use
Select+L1 or Guide for the menu there.

## Settings menu

Esc (or a controller's Guide button, Select+Start or Select+L1) opens the
menu over the game, which stands still meanwhile. Up/Down choose, Left/Right
change a value, A/Enter selects, B/Esc goes back. It has widescreen,
scaling, palette, fullscreen, volume, sound, the speedrun timer and its
category, automatic updates, RetroAchievements, the controls, the changelog
and Quit. Under
*Controls* pick an action and press the new key or controller button for
it (Esc or six seconds cancel); *Reset all controls* restores the defaults.
Changes are saved in the settings file when the menu closes; options given
on the command line stay one-off. F12 in the menu saves a picture of it as
`menu.png` in the data directory.

## Widescreen

With widescreen on, the picture keeps the Game Boy's 144 lines and grows
sideways: up to 32:9, so the whole width of a 16:9, 16:10, ultrawide or
handheld screen shows more of the level. `on` fills the window; a fixed
aspect (`4:3`, `16:10`, `16:9`, `64:27`, `32:9` in the menu, any `W:H` in
the settings file) keeps that shape in any window. The middle 160 pixels
are exactly the game's picture. To the right, the level columns the game
has not drawn yet are decoded from the level data in your ROM; to the left
the columns it drew last are kept. Enemies appear only when the game spawns
them at its own screen edge, and the status bar, the title screen, bonus
games, underground rooms and the ending keep blank sides. Widescreen is
presentation only: the game runs exactly as on a Game Boy.

Every binding can be changed in the menu or the settings file. Palette,
scaling and fullscreen changes made with the keys are saved there as well.
Screenshots go to `~/.local/share/super-mario-land-port/screenshots/`
(the picture as shown: 160x144, or wider with widescreen, current palette).

## Settings

`~/.config/super-mario-land-port/settings.ini` (or `$XDG_CONFIG_HOME/...`)
is created with commented defaults on first start; `--config FILE` uses
another file. Command-line options override it for one run (`sml --help`).

* `[video]` `scale` (initial window size), `scaling`:
  `integer` (whole multiples, black borders), `sharp` (fills the window
  with square pixels and edges at most one screen pixel soft), `nearest`
  (fills the window, uneven pixel sizes); `fullscreen`; `vsync`;
  `palette` (`green`, `gray`, `pocket` or four `#RRGGBB` colours, lightest
  first); `widescreen` (`off`, `on` = fill the window, or an aspect ratio
  such as `16:9`); `fast_forward_speed` (0 = as fast as possible, N = N
  times).
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
  unbinds. `menu` also takes button pairs such as `back+start` (both held).
  `quit` is unbound by default (Quit is in the menu); a key bound to both
  opens the menu. `stick` and `deadzone` control the left stick.

None of these settings change the game: pacing, scaling, resampling and
fast forward only affect presentation, so the game computes exactly what
the cartridge computes and input movies stay in sync.

## Speedrun timer

F8 (or `timer = yes` under `[speedrun]`, or `--timer`) shows a timer in the
bottom right corner. It follows the speedrun.com rules for Super Mario Land
(real time): it starts on the frame before 1-1 first appears after Start on
the title screen and stops on the first frame in which Tatanga's sprite has
disappeared. It counts emulated Game Boy cycles, not wall-clock time, so the
result equals the same run on a console and does not change with the
display rate, window stalls or fast forward. A run that used fast forward is
marked `FF` (a video of it would be shorter than its time).

Splits are automatic, one per level, on the frame before the next level
appears; the last one is Tatanga. Each split shows the time and the
difference to your personal best (green ahead, red behind, gold for a new
best segment). Returning to the title (game over, reset) ends the attempt.
`category = normal+hard` times Normal + Hard Mode: 24 splits, the run goes on
through the ending until Tatanga falls in the second quest.

Personal best, best segments, attempt count and the last 32 finished times
are kept in `speedrun-any.txt` (or `speedrun-normal-hard.txt`) in the data
directory, with an up-to-date LiveSplit copy next to it
(`speedrun-any.lss`). `sml --import-lss FILE` takes over the personal best,
best segments and attempt count of an existing LiveSplit file with one
segment per level.

## RetroAchievements

The official RetroAchievements set for Super Mario Land works with your v1.1
ROM: the port keeps the Game Boy's memory exactly as the original, so the
set's conditions are checked after every frame just like in an emulator.
Open the menu, choose *RetroAchievements*, then *Sign in...* and enter the
user name and password of your retroachievements.org account (type them, or
pick letters with the controller: A types, B deletes, Start finishes). Only
the login token is kept, in `retroachievements.login` in the data directory;
*Sign out* deletes it. Later starts sign in by themselves.

Unlocks pop up at the top of the window and are sent to your account (when
offline they are sent later); measured achievements show their progress in
the bottom left corner. *Achievement list...* shows the whole set with your
unlocks (`*`), points, progress and the chosen one's description. *Pop-ups*
turns the messages off, *Achievements* turns it all off (`[achievements]`
`enabled` / `popups` in settings.ini).

Only softcore is supported (no hardcore mode); unlocks count as softcore on
your profile. Achievements only read the game's memory, so the game, its
timing and every TAS are unchanged with them on.

## Options for testing and TAS playback

    sml --model agb --movie build/resync11.movie   # console-verified TAS
    sml --record my.movie                           # save input on exit
    sml --headless 600 --screenshot out.png         # no window, no audio
    sml --model agb --movie build/resync11.movie --headless 44100 --speedrun-report

`--model dmg|cgb|agb` selects the console whose timing is modelled
(DMG by default; the resync TAS was made on a Game Boy Advance). `--stats`
prints the measured frame rate, pacing mode and audio queue on exit.
`--achievements-report` prints each unlock with its frame; with
`SML_RA_FAKE=verify/tests/ra_fake` the test set is served from files instead
of the network (headless runs never go online).
