#!/bin/bash
# Drives the level editor in a virtual X server: opens it (--editor), puts a
# tile, an enemy and an item block into a new level, saves it (Ctrl+S), plays
# it (menu: Play), goes back to the editor from the game's menu and quits.
# Checks the saved level file and that pictures of the editor and of the
# level in play were taken.
#
#   verify/tests/editor_xvfb.sh BINARY ROM OUTDIR
set -euo pipefail
if [ "${1:-}" = --inside ]; then
    shift
    bin=$1 rom=$2 out=$3
    "$bin" --rom "$rom" --windowed --scale 3 --editor &
    pid=$!
    for _ in $(seq 1 50); do
        win=$(xdotool search --name "Super Mario Land" 2>/dev/null | head -1) && [ -n "$win" ] && break
        sleep 0.2
    done
    [ -n "${win:-}" ] || { echo "editor_xvfb: no window"; kill $pid; exit 1; }
    xdotool mousemove 1279 719          # the pointer away from the grid
    sleep 1.5
    key() { xdotool key --window "$win" "$@"; sleep 0.15; }
    key Home                            # column 0; row 15:
    for _ in $(seq 1 16); do key Down; done
    for _ in 1 2 3 4 5 6; do key Right; done
    for _ in 1 2 3 4 5; do key Up; done # column 6, row 10
    key e; key e; key e                 # third tile of the world's list
    key space
    key Tab                             # enemies
    key t; key F12; key Escape          # their list as pictures: editor-list.png
    for _ in $(seq 1 24); do key Right; done
    key Down; key Down                  # column 30, row 12
    key space
    key Tab                             # items: an item block with a mushroom
    for _ in 1 2 3 4; do key Left; done
    key Up; key Up; key Up              # column 26, row 9
    key space
    key ctrl+s
    key F12                             # editor.png
    sleep 0.5
    key Escape; key Return              # menu: Play this level
    sleep 3
    key F12                             # a screenshot of the level in play
    key Escape                          # game menu
    for _ in 1 2; do key Up; done       # "Back to the editor"
    key Return
    sleep 1.5
    key Escape; key Up; key Return      # editor menu: Quit game
    for _ in $(seq 1 50); do kill -0 $pid 2>/dev/null || break; sleep 0.2; done
    kill -0 $pid 2>/dev/null && { echo "editor_xvfb: did not quit"; kill $pid; exit 1; }
    wait $pid
    exit 0
fi

bin=$(realpath "$1") rom=$(realpath "$2") out=$3
rm -rf "$out" && mkdir -p "$out/config" "$out/data"
out=$(realpath "$out")
export XDG_CONFIG_HOME="$out/config" XDG_DATA_HOME="$out/data" SML_NO_UPDATE=1 SML_NO_REPORT=1
export SDL_AUDIODRIVER=dummy
xvfb-run -a -s "-screen 0 1280x720x24" bash "$0" --inside "$bin" "$rom" "$out"

data="$out/data/super-mario-land-port"
level="$data/levels/New_level.level.txt"
[ -s "$level" ] || { echo "editor_xvfb: level not saved"; exit 1; }
head -1 "$level" | grep -qx 'SML-LEVEL 1' || { echo "editor_xvfb: bad level file"; exit 1; }
grep -qE '^enemy 30 12 [0-9A-F]{2}$' "$level" || { echo "editor_xvfb: enemy missing"; exit 1; }
grep -qx 'item 26 28' "$level" || { echo "editor_xvfb: item missing"; exit 1; }
row10=$(grep '^row 10 ' "$level" | cut -d' ' -f3)
[ "${row10:12:2}" != 2C ] || { echo "editor_xvfb: tile missing"; exit 1; }
[ -s "$data/editor.png" ] || { echo "editor_xvfb: no editor picture"; exit 1; }
[ -s "$data/editor-list.png" ] || { echo "editor_xvfb: no picture of the enemy list"; exit 1; }
ls "$data"/screenshots/sml-*.png >/dev/null 2>&1 || { echo "editor_xvfb: no picture of the level"; exit 1; }
echo "editor_xvfb: passed"
