#!/bin/bash
# Builds every release asset for one version into build/release/v<VERSION>/:
#   packaging/make_release.sh 0.1.0
# Needs the locally generated access table (docs/BUILDING.md), the toolchain
# and a "## <VERSION>" section in CHANGELOG.md. No ROM data is packaged.
# Assets: AppImages (x86_64, aarch64), Linux x86_64 tarball, Windows zip,
# PortMaster zip, Steam Deck installer archive, the raw update files and
# update.txt (size + SHA-256 per update file, read by src/platform/update.c).
set -euo pipefail
cd "$(dirname "$0")/.."
version=${1:?usage: make_release.sh VERSION}
[[ $version =~ ^[0-9]+(\.[0-9]+){1,3}$ ]] || { echo "bad version $version" >&2; exit 2; }
grep -q "^## $version " CHANGELOG.md || { echo "CHANGELOG.md has no '## $version' section" >&2; exit 2; }
grep -q 'shadow_access_table_size = -1' src/core/access_table.c 2> /dev/null && { echo "placeholder access table" >&2; exit 2; }

make -j10 port-all VERSION="$version"
packaging/linux/make_appimage.sh x86_64
packaging/linux/make_appimage.sh aarch64
for bin in build/linux-x86_64/sml; do
    [ "$("$bin" --version)" = "$version" ] || { echo "$bin is not $version" >&2; exit 1; }
done

out=build/release/v$version
stage=build/release/stage-$version
rm -rf "$out" "$stage"
mkdir -p "$out" "$stage"
name=super-mario-land-port-$version

# update files (fixed names; update.c picks the one for its package)
cp build/dist/Super_Mario_Land_Port-x86_64.AppImage build/dist/Super_Mario_Land_Port-aarch64.AppImage "$out/"
cp build/linux-x86_64/sml "$out/sml-linux-x86_64"
cp build/linux-aarch64/sml "$out/sml-linux-aarch64"
cp build/windows-x86_64/sml.exe "$out/sml-windows-x86_64.exe"
cp build/portmaster-aarch64/sml.aarch64 "$out/sml-portmaster-aarch64"

# Linux x86_64 tarball
d=$stage/$name-linux-x86_64
mkdir -p "$d"
cp build/linux-x86_64/sml packaging/README.txt CHANGELOG.md "$d/"
sed 's/^Name=.*/Name=Super Mario Land/' packaging/linux/super-mario-land-port.desktop > "$d/super-mario-land-port.desktop"
python3 packaging/linux/make_icon.py "$d/super-mario-land-port.png" 256
tar -C "$stage" --owner=0 --group=0 -czf "$out/$name-linux-x86_64.tar.gz" "$name-linux-x86_64"

# Windows x86_64 zip
d=$stage/windows/$name-windows-x86_64
mkdir -p "$d"
cp build/windows-x86_64/sml.exe build/windows-x86_64/SDL2.dll CHANGELOG.md "$d/"
sed 's/$/\r/' packaging/README.txt > "$d/README.txt"
python3 packaging/zipdir.py "$out/$name-windows-x86_64.zip" "$stage/windows"

# PortMaster zip: "Super Mario Land.sh" and supermarioland/
d=$stage/portmaster
mkdir -p "$d/supermarioland"
cp "packaging/portmaster/Super Mario Land.sh" "$d/"
cp build/portmaster-aarch64/sml.aarch64 packaging/portmaster/port.json packaging/README.txt CHANGELOG.md "$d/supermarioland/"
chmod 755 "$d/Super Mario Land.sh" "$d/supermarioland/sml.aarch64"
python3 packaging/zipdir.py "$out/$name-portmaster.zip" "$d"

# Steam Deck installer
d=$stage/$name-steamdeck
mkdir -p "$d/artwork"
cp packaging/steamdeck/install.sh packaging/steamdeck/steam_shortcut.py packaging/README.txt CHANGELOG.md \
   build/dist/Super_Mario_Land_Port-x86_64.AppImage "$d/"
python3 packaging/steamdeck/make_artwork.py "$d/artwork"
tar -C "$stage" --owner=0 --group=0 -czf "$out/$name-steamdeck.tar.gz" "$name-steamdeck"

{
    echo "version $version"
    for f in Super_Mario_Land_Port-x86_64.AppImage Super_Mario_Land_Port-aarch64.AppImage \
             sml-linux-x86_64 sml-linux-aarch64 sml-windows-x86_64.exe sml-portmaster-aarch64; do
        echo "file $f $(stat -c %s "$out/$f") $(sha256sum "$out/$f" | cut -d' ' -f1)"
    done
} > "$out/update.txt"
(cd "$out" && sha256sum -- * > SHA256SUMS)
rm -rf "$stage"
ls -l "$out"
