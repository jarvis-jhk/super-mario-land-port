#!/bin/bash
# Packs a Linux release binary into an AppImage:
#   packaging/linux/make_appimage.sh x86_64|aarch64
#     in:  build/linux-<arch>/sml (make port-linux-<arch>)
#     out: build/dist/Super_Mario_Land_Port-<arch>.AppImage
# An AppImage is the type2 runtime (tools/fetch_toolchain.sh) followed by a
# squashfs image of the AppDir. The binary links SDL2 statically and loads
# X11/PulseAudio/ALSA/OpenGL from the system, so nothing else is bundled.
# The ROM is never included; see docs/BUILDING.md for where it is looked up.
set -euo pipefail
. "$(dirname "$0")/../../tools/build_env.sh"

arch=${1:?usage: make_appimage.sh x86_64|aarch64}
bin=$SML_ROOT/build/linux-$arch/sml
runtime=$SML_ROOT/tools/appimage/runtime-$arch
out=$SML_ROOT/build/dist/Super_Mario_Land_Port-$arch.AppImage
[ -x "$bin" ] || { echo "missing $bin (make port-linux-$arch)" >&2; exit 1; }
[ -f "$runtime" ] || { echo "missing $runtime (tools/fetch_toolchain.sh)" >&2; exit 1; }
command -v mksquashfs > /dev/null || { echo "mksquashfs not found (squashfs-tools)" >&2; exit 1; }

work=$(mktemp -d "$TMPDIR/appimage.XXXXXX")
trap 'rm -rf "$work"' EXIT
app=$work/AppDir
mkdir -p "$app/usr/bin" "$app/usr/share/applications" "$app/usr/share/icons/hicolor/256x256/apps"
cp "$bin" "$app/usr/bin/sml"
"$ZIG" objcopy --strip-debug "$app/usr/bin/sml" 2> /dev/null || strip --strip-debug "$app/usr/bin/sml" 2> /dev/null || true
cat > "$app/AppRun" <<'EOF'
#!/bin/sh
here=$(dirname "$(readlink -f "$0")")
exec "$here/usr/bin/sml" "$@"
EOF
chmod +x "$app/AppRun"
desktop=$SML_ROOT/packaging/linux/super-mario-land-port.desktop
cp "$desktop" "$app/"
cp "$desktop" "$app/usr/share/applications/"
python3 "$SML_ROOT/packaging/linux/make_icon.py" "$app/super-mario-land-port.png" 256
cp "$app/super-mario-land-port.png" "$app/usr/share/icons/hicolor/256x256/apps/"
ln -s super-mario-land-port.png "$app/.DirIcon"

mksquashfs "$app" "$work/app.squashfs" -root-owned -noappend -comp zstd -quiet -no-progress > /dev/null
mkdir -p "$(dirname "$out")"
cat "$runtime" "$work/app.squashfs" > "$out.part"
chmod +x "$out.part"
mv "$out.part" "$out"
echo "built $out ($(du -h "$out" | cut -f1))"
