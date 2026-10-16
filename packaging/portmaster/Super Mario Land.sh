#!/bin/bash
# PortMaster launcher for the Super Mario Land C port.
# Put your Super Mario Land (World) v1.1 ROM at ports/supermarioland/sml.gb.
XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}
if [ -d "/opt/system/Tools/PortMaster/" ]; then
  controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then
  controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then
  controlfolder="$XDG_DATA_HOME/PortMaster"
else
  controlfolder="/roms/ports/PortMaster"
fi
source "$controlfolder/control.txt"
[ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"
get_controls

GAMEDIR="/$directory/ports/supermarioland"
cd "$GAMEDIR" || exit 1
> "$GAMEDIR/log.txt" && exec > >(tee "$GAMEDIR/log.txt") 2>&1

# Settings, screenshots and the version marker stay inside the port folder.
export XDG_CONFIG_HOME="$GAMEDIR/conf" XDG_DATA_HOME="$GAMEDIR/conf"
export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"
$ESUDO chmod +x "$GAMEDIR/sml.aarch64"

# Select+Start quits through gptokeyb; the game reads the controller itself.
$GPTOKEYB "sml.aarch64" &
[ "$(type -t pm_platform_helper)" = function ] && pm_platform_helper "$GAMEDIR/sml.aarch64"
./sml.aarch64 --fullscreen
[ "$(type -t pm_finish)" = function ] && pm_finish
$ESUDO kill -9 $(pidof gptokeyb) 2> /dev/null
printf "\033c" > /dev/tty0 2> /dev/null
