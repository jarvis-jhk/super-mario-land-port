#!/bin/bash
# Super Mario Land (C port) - Steam Deck / desktop Linux installer.
# Run in Desktop Mode (double-click in Dolphin or `./install.sh` in Konsole):
#   ./install.sh [path/to/your/sml.gb]
# Installs into ~/.local/share/super-mario-land-port/ without root, adds a
# "Super Mario Land" desktop entry and a non-Steam game with its artwork.
# Running it again repairs the installation; ROM, settings and saves stay.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
data=${XDG_DATA_HOME:-$HOME/.local/share}
dest=$data/super-mario-land-port
app=$dest/app
image=$app/Super_Mario_Land_Port-x86_64.AppImage

mkdir -p "$app/artwork" "$data/applications" "$data/icons/hicolor/256x256/apps"
cp "$here/Super_Mario_Land_Port-x86_64.AppImage" "$image.part"
chmod 755 "$image.part"
mv -f "$image.part" "$image"
cp "$here"/artwork/*.png "$app/artwork/"
cp "$here/artwork/icon.png" "$data/icons/hicolor/256x256/apps/super-mario-land-port.png"
cat > "$data/applications/super-mario-land-port.desktop.part" <<DESKTOP
[Desktop Entry]
Type=Application
Name=Super Mario Land
Comment=Cycle-exact C port of Super Mario Land; plays your own ROM
Exec="$image"
Path=$app
Icon=super-mario-land-port
Categories=Game;ArcadeGame;
Terminal=false
DESKTOP
mv -f "$data/applications/super-mario-land-port.desktop.part" "$data/applications/super-mario-land-port.desktop"
echo "Installed $image"

rom=${1:-}
[ -z "$rom" ] && [ -f "$here/sml.gb" ] && rom=$here/sml.gb
if [ -n "$rom" ]; then
    cp "$rom" "$dest/sml.gb"
    echo "Copied the ROM to $dest/sml.gb"
elif [ ! -f "$dest/sml.gb" ]; then
    echo "Put your Super Mario Land (World) v1.1 ROM at $dest/sml.gb"
    echo "(or run: ./install.sh path/to/sml.gb)"
fi

status=0
python3 "$here/steam_shortcut.py" --exe "$image" --start-dir "$app" --artwork "$here/artwork" \
    --icon "$app/artwork/icon.png" --launch-options=--fullscreen || status=$?
case $status in
    0|2) ;;
    3) echo "The game is installed; run install.sh again after exiting Steam for the Steam entry." ;;
    *) echo "The Steam entry was not changed (code $status); the game itself is installed." ;;
esac
