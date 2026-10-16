Super Mario Land - C port
=========================

A cycle-exact C port of Super Mario Land (Game Boy, 1989). It needs your own
copy of the cartridge: Super Mario Land (World) (Rev A / v1.1), SHA1
418203621b887caa090215d97e3f509b79affd3e, saved as "sml.gb". No game data is
included. Source, issues and releases:
https://github.com/jarvis-jhk/super-mario-land-port

Where the ROM goes
------------------
Linux tarball / AppImage:  next to the program, or
                           ~/.local/share/super-mario-land-port/sml.gb
Windows:                   next to sml.exe, or
                           %APPDATA%\super-mario-land-port\sml.gb
PortMaster:                ports/supermarioland/sml.gb
Steam Deck:                ./install.sh path/to/sml.gb (Desktop Mode), or
                           ~/.local/share/super-mario-land-port/sml.gb

Install
-------
Linux:      unpack the tarball and start ./sml, or make the AppImage
            executable and start it.
Windows:    unzip and start sml.exe (Windows 10 or later).
PortMaster: unzip into the ports folder of your handheld (it adds
            "Super Mario Land.sh" and supermarioland/); Select+Start quits,
            Select+L1 opens the settings menu.
Steam Deck: in Desktop Mode unpack the steamdeck archive and run install.sh.
            It adds a "Super Mario Land" desktop entry and, after you exit
            Steam, a non-Steam game with its own artwork. Run it again to
            repair; ROM, settings and saves are kept.

Updates
-------
At start-up the game looks for a newer release in the background. A new
version is downloaded next to the program, its size and SHA-256 are checked
against the release's update.txt, and only then does it replace the program
for the next start. Offline starts and read-only installs are not affected.
To turn it off, set "check = no" under [update] in settings.ini, start with
--no-update, or set SML_NO_UPDATE=1. The first start of a new version shows
what changed (also: sml --changelog).

Controls
--------
D-pad: arrow keys / D-pad / left stick   A: X / south button
B: Z / west or east button               Start: Enter / Start
Select: Right Shift, Backspace / Back     Fast forward: Tab / right shoulder
Settings menu (widescreen, scaling, palette, volume, timer, controls, quit):
Esc / Guide, Select+Start or Select+L1. F8 speedrun timer, F9 palette,
F10 scaling, F11 fullscreen, F12 screenshot.
Bindings can be changed in the menu and live in settings.ini (~/.config/super-mario-land-port/ on Linux,
the data directory on Windows).

Crashes are reported anonymously (app, version and platform only; no files,
paths or game data). SML_NO_REPORT=1 turns this off.

The Windows build includes SDL2 (zlib licence, https://libsdl.org).
