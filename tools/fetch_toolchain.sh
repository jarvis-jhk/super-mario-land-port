#!/bin/bash
# Downloads the cross-compiler (Zig) and SDL2 development files into tools/.
# Idempotent: finished steps are skipped (delete the directory to redo one).
# Everything is checked against a pinned SHA256 and extracted into a fresh,
# empty directory (archives are untrusted data). Nothing is installed.
set -euo pipefail
. "$(dirname "$0")/build_env.sh"

ZIG_VER=0.14.1
ZIG_URL=https://ziglang.org/download/$ZIG_VER/zig-x86_64-linux-$ZIG_VER.tar.xz
ZIG_SHA=24aeeec8af16c381934a6cd7d95c807a8cb2cf7df9fa40d359aa884195c4716c

SDL_VER=2.30.0
SDL_SRC_URL=https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VER/SDL2-$SDL_VER.tar.gz
SDL_SRC_SHA=36e2e41557e0fa4a1519315c0f5958a87ccb27e25c51776beb6f1239526447b0
SDL_MINGW_URL=https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VER/SDL2-devel-$SDL_VER-mingw.tar.gz
SDL_MINGW_SHA=d44bd8cd869219f43634a898d7735d0c92d4b2cca129f709dc8b98402b36d5f3
DEB=libsdl2-2.0-0_2.30.0+dfsg-1ubuntu3.1
DEB_AMD64_URL=https://archive.ubuntu.com/ubuntu/pool/main/libs/libsdl2/${DEB}_amd64.deb
DEB_AMD64_SHA=a2e733f1339272f22cf7777d454b5d4f71e27335d89d4c99cf41e1ce1bb5c462
DEB_ARM64_URL=https://ports.ubuntu.com/ubuntu-ports/pool/main/libs/libsdl2/${DEB}_arm64.deb
DEB_ARM64_SHA=3406a7a3cf98a633c9a1289eb20a31e03a04477afb95c8558f2de7f70abdd0bb

# fetch <url> <sha256> <dest-file>
fetch() {
    local url=$1 sha=$2 dest=$3
    mkdir -p "$(dirname "$dest")"
    if [ ! -f "$dest" ]; then
        echo "downloading $url"
        curl -fsSL -o "$dest.part" "$url"
        mv "$dest.part" "$dest"
    fi
    echo "$sha  $dest" | sha256sum -c --quiet - \
        || { echo "checksum mismatch: $dest" >&2; rm -f "$dest"; exit 1; }
}

# fresh_dir <var> : creates a new empty temp dir inside build/tmp
fresh_dir() { mktemp -d "$TMPDIR/extract.XXXXXX"; }

# --- Zig ---------------------------------------------------------------
if [ ! -x "$ZIG" ]; then
    fetch "$ZIG_URL" "$ZIG_SHA" "$SML_ROOT/tools/zig/dl/$(basename "$ZIG_URL")"
    d=$(fresh_dir)
    tar -xJf "$SML_ROOT/tools/zig/dl/$(basename "$ZIG_URL")" -C "$d" --strip-components=1
    # tools/zig/ may hold dl/; move the extracted files next to it
    cp -a "$d"/. "$SML_ROOT/tools/zig/"
    rm -rf "$d"
fi
"$ZIG" version

# --- SDL2 headers (from the source release) ----------------------------
if [ ! -f "$SDL2_INCLUDE/SDL.h" ]; then
    fetch "$SDL_SRC_URL" "$SDL_SRC_SHA" "$SDL2_DIR/dl/$(basename "$SDL_SRC_URL")"
    d=$(fresh_dir)
    tar -xzf "$SDL2_DIR/dl/$(basename "$SDL_SRC_URL")" -C "$d" --strip-components=2 "SDL2-$SDL_VER/include"
    mkdir -p "$SDL2_INCLUDE"
    cp -a "$d"/. "$SDL2_INCLUDE/"
    rm -rf "$d"
fi

# --- SDL2 for Windows (MinGW dev package: headers, import lib, SDL2.dll) -
if [ ! -f "$SDL2_MINGW/bin/SDL2.dll" ]; then
    fetch "$SDL_MINGW_URL" "$SDL_MINGW_SHA" "$SDL2_DIR/dl/$(basename "$SDL_MINGW_URL")"
    d=$(fresh_dir)
    tar -xzf "$SDL2_DIR/dl/$(basename "$SDL_MINGW_URL")" -C "$d" --strip-components=1 \
        "SDL2-$SDL_VER/x86_64-w64-mingw32"
    mkdir -p "$SDL2_DIR/mingw"
    cp -a "$d"/. "$SDL2_DIR/mingw/"
    rm -rf "$d"
fi

# --- SDL2 for Linux: link stubs from the Ubuntu 24.04 (noble) packages -
# Only unpacked, never installed. The executables link dynamically against
# libSDL2-2.0.so.0, which the target system provides at run time.
for arch in amd64:x86_64 arm64:arm64; do
    deb_arch=${arch%%:*}; dir=$SDL2_DIR/linux-${arch##*:}
    if [ ! -e "$dir/libSDL2.so" ]; then
        eval "url=\$DEB_${deb_arch^^}_URL sha=\$DEB_${deb_arch^^}_SHA"
        fetch "$url" "$sha" "$SDL2_DIR/dl/${DEB}_$deb_arch.deb"
        d=$(fresh_dir)
        dpkg-deb -x "$SDL2_DIR/dl/${DEB}_$deb_arch.deb" "$d"
        mkdir -p "$dir"
        cp -a "$d"/usr/lib/*/libSDL2-2.0.so.0* "$dir/"
        ln -sf libSDL2-2.0.so.0 "$dir/libSDL2.so"
        rm -rf "$d"
    fi
done

# --- Headers for building SDL2 from source (Linux release builds) -------
# The release binaries link SDL2 statically, built by tools/build_sdl2.sh
# against glibc 2.31. SDL loads X11, PulseAudio, ALSA and udev at run time
# (dlopen), as is OpenGL, so only their headers are needed: taken from the Ubuntu 20.04
# (focal) -dev packages, unpacked only. The headers are the same for every
# architecture; the amd64 packages serve the arm64 build as well.
FOCAL=https://archive.ubuntu.com/ubuntu
FOCAL_DEBS="
pool/main/x/xorgproto/x11proto-dev_2019.2-1ubuntu1_all.deb 4144072931cbfbb422b465ae4775ce906d01ea816d432ed820b301e08cfef975
pool/main/libx/libx11/libx11-dev_1.6.9-2ubuntu1_amd64.deb f839bd9f00ab8e95f9a86f2d7e98c6ecc5a457fc08dcac2a98f46cfec6ba0b91
pool/main/libx/libxext/libxext-dev_1.3.4-0ubuntu1_amd64.deb b39e2d033916a4e2c1e970465517e285c3e532d3e2f451b720e67ba09cbb2733
pool/main/libx/libxrandr/libxrandr-dev_1.5.2-0ubuntu1_amd64.deb 8e3d54f0605a4aa192d8413ed0f3e2c82dff606b81499710208190cd965a31f2
pool/main/libx/libxrender/libxrender-dev_0.9.10-1_amd64.deb aeb7abe8409afbb484c06882c158e7e695743d678387064ef95bdb3d6edbce15
pool/main/libx/libxcursor/libxcursor-dev_1.2.0-2_amd64.deb 23bf15dd1bebb352effdeaab6678b995d61817c2b33d0c914c120e2450edc111
pool/main/libx/libxi/libxi-dev_1.7.10-0ubuntu1_amd64.deb 04a74024e897f19fc0738135a977a9f7d6f9671886cb6f439e2a7354ccd7542c
pool/main/libx/libxfixes/libxfixes-dev_5.0.3-2_amd64.deb 677dc9b5dde080f9ae99ab31f5c8777b01d27d6efe4380687224916151567183
pool/main/libx/libxss/libxss-dev_1.2.3-1_amd64.deb 95f280803c189ac5339a28de0fe46ae33827d72352656963df5a4c5b20229983
pool/main/libx/libxcb/libxcb1-dev_1.14-2_amd64.deb 824b00562519ccfdd9dd5faa58bbc5cb60b7a6b4eea09eca58c989bd921f88d5
pool/main/libx/libxau/libxau-dev_1.0.9-0ubuntu1_amd64.deb 61d3505d0db08c398e91ca5b51f928aa5dbb4d384ccfa2e5c6f0419a57ecf524
pool/main/a/alsa-lib/libasound2-dev_1.2.2-2.1_amd64.deb df7a3b0d1a1b1b77ab954552f0c17da502c5104f1ca1204634751d0e4535108a
pool/main/p/pulseaudio/libpulse-dev_13.99.1-1ubuntu3_amd64.deb f8fae9fd03f885dcf316682fb30dd6f03a89d7a63def72afabb7621a6e987fa7
pool/main/s/systemd/libudev-dev_245.4-4ubuntu3_amd64.deb ee7f1d80a984f62669f7b4ae77522afbb4b5ba38e59b0db11806a867bc2a69cb
pool/main/libg/libglvnd/libgl-dev_1.3.1-1_amd64.deb 4a7fff3bbe3f5af24f8e36b5565c36b14a2acd3e66e58ecf15b15a36aa15f929
pool/main/libg/libglvnd/libglx-dev_1.3.1-1_amd64.deb 4df0694e208a7b7a32311b3d7dd424d3b8775624f3786a7fd7efe54c66f760ed
"
SYSROOT_INC=$SML_ROOT/tools/linux-headers/include
if [ ! -f "$SYSROOT_INC/GL/glx.h" ]; then
    d=$(fresh_dir)
    while read -r path sha; do
        [ -n "$path" ] || continue
        fetch "$FOCAL/$path" "$sha" "$SML_ROOT/tools/linux-headers/dl/$(basename "$path")"
        dpkg-deb -x "$SML_ROOT/tools/linux-headers/dl/$(basename "$path")" "$d"
    done <<< "$FOCAL_DEBS"
    mkdir -p "$SYSROOT_INC"
    cp -a "$d"/usr/include/. "$SYSROOT_INC/"
    rm -rf "$d"
fi

# --- AppImage runtimes (type2-runtime release 20251108) ------------------
# An AppImage is this runtime followed by a squashfs image of the app.
RUNTIME_URL=https://github.com/AppImage/type2-runtime/releases/download/20251108
RUNTIME_X86_64_SHA=2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d
RUNTIME_AARCH64_SHA=00cbdfcf917cc6c0ff6d3347d59e0ca1f7f45a6df1a428a0d6d8a78664d87444
fetch "$RUNTIME_URL/runtime-x86_64" "$RUNTIME_X86_64_SHA" "$SML_ROOT/tools/appimage/runtime-x86_64"
fetch "$RUNTIME_URL/runtime-aarch64" "$RUNTIME_AARCH64_SHA" "$SML_ROOT/tools/appimage/runtime-aarch64"

echo "toolchain ready: zig $("$ZIG" version), SDL2 $SDL_VER"
