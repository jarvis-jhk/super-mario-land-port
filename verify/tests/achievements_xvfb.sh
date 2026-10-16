#!/bin/bash
# Drives RetroAchievements in a virtual X server against the test server
# (SML_RA_FAKE=verify/tests/ra_fake, no network): opens the menu, signs out
# and in again with the on-screen text entry, saves a picture of the
# achievement list (F12), then fast-forwards the console-verified TAS until
# just after the first unlock and keeps the window with its pop-up.
#
#   verify/tests/achievements_xvfb.sh BINARY ROM MOVIE OUTDIR
set -euo pipefail
if [ "${1:-}" = --inside ]; then
    shift
    bin=$1 rom=$2 movie=$3 out=$4
    "$bin" --rom "$rom" --model agb --movie "$movie" --windowed --scale 3 --frames 2520 \
        --screenshot "$out/window.png" &
    pid=$!
    for _ in $(seq 1 50); do
        win=$(xdotool search --name "Super Mario Land" 2>/dev/null | head -1) && [ -n "$win" ] && break
        sleep 0.2
    done
    [ -n "${win:-}" ] || { echo "achievements_xvfb: no window"; kill $pid; exit 1; }
    sleep 1
    key() { xdotool key --window "$win" "$@"; sleep 0.3; }
    key Escape                                  # open the menu
    for _ in $(seq 1 10); do key Down; done     # RetroAchievements...
    key Return
    key Down; key Down; key Return              # Sign out
    key Return                                  # Sign in...
    xdotool type --window "$win" --delay 50 test; key Return
    xdotool type --window "$win" --delay 50 secret; key Return
    sleep 0.5
    key Down; key Return                        # Achievement list...
    key F12                                     # its picture
    key Escape; key Escape; key Escape          # back to the game
    xdotool keydown --window "$win" Tab         # fast forward to the first unlock
    wait $pid
    exit 0
fi

bin=$(realpath "$1") rom=$(realpath "$2") movie=$(realpath "$3") out=$4
rm -rf "$out" && mkdir -p "$out/config" "$out/data"
out=$(realpath "$out")
export XDG_CONFIG_HOME="$out/config" XDG_DATA_HOME="$out/data" SML_NO_UPDATE=1 SML_NO_REPORT=1
export SDL_AUDIODRIVER=dummy SML_RA_FAKE=$(realpath "$(dirname "$0")/ra_fake")
xvfb-run -a -s "-screen 0 1280x720x24" bash "$0" --inside "$bin" "$rom" "$movie" "$out" 2>&1 | tee "$out/log.txt"

[ -s "$out/data/super-mario-land-port/achievements.png" ] || { echo "achievements_xvfb: no list picture"; exit 1; }
[ -s "$out/window.png" ] || { echo "achievements_xvfb: no window picture"; exit 1; }
[ ! -e "$out/data/super-mario-land-port/retroachievements.login" ] || { echo "achievements_xvfb: test login kept"; exit 1; }
echo "achievements_xvfb: passed"
