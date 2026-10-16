#!/bin/bash
# Drives the settings menu in a virtual X server: opens it with Escape,
# switches widescreen on and the palette to gray, saves a picture of the menu
# (F12), closes it and checks that the settings file kept both changes and
# that the window shows the wider picture.
#
#   verify/tests/menu_xvfb.sh BINARY ROM OUTDIR
set -euo pipefail
if [ "${1:-}" = --inside ]; then
    shift
    bin=$1 rom=$2 out=$3
    "$bin" --rom "$rom" --windowed --scale 3 --frames 1500 --screenshot "$out/window.png" &
    pid=$!
    for _ in $(seq 1 50); do
        win=$(xdotool search --name "Super Mario Land" 2>/dev/null | head -1) && [ -n "$win" ] && break
        sleep 0.2
    done
    [ -n "${win:-}" ] || { echo "menu_xvfb: no window"; kill $pid; exit 1; }
    sleep 1
    xdotool windowsize --sync "$win" 960 432 || true
    sleep 0.5
    key() { xdotool key --window "$win" "$@"; sleep 0.3; }
    key Escape                  # open the menu
    key Down; key Right         # Widescreen: off -> on
    key Down; key Down; key Right   # Palette: green -> gray
    key F12                     # picture of the menu
    key Escape                  # close it, the game goes on
    wait $pid
    exit 0
fi

bin=$(realpath "$1") rom=$(realpath "$2") out=$3
rm -rf "$out" && mkdir -p "$out/config" "$out/data"
out=$(realpath "$out")
export XDG_CONFIG_HOME="$out/config" XDG_DATA_HOME="$out/data" SML_NO_UPDATE=1 SML_NO_REPORT=1
export SDL_AUDIODRIVER=dummy
xvfb-run -a -s "-screen 0 1280x720x24" bash "$0" --inside "$bin" "$rom" "$out"

cfg="$out/config/super-mario-land-port/settings.ini"
grep -qx 'widescreen = on' "$cfg" || { echo "menu_xvfb: widescreen not saved"; exit 1; }
grep -qx 'palette = gray' "$cfg" || { echo "menu_xvfb: palette not saved"; exit 1; }
[ -s "$out/data/super-mario-land-port/menu.png" ] || { echo "menu_xvfb: no menu picture"; exit 1; }
[ -s "$out/window.png" ] || { echo "menu_xvfb: no window picture"; exit 1; }
echo "menu_xvfb: passed"
