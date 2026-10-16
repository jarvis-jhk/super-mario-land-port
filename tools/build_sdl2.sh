#!/bin/bash
# Builds a static SDL2 for a Linux release target with Zig:
#   tools/build_sdl2.sh x86_64|aarch64     -> build/sdl2-<arch>/lib/libSDL2.a
# Targets glibc 2.31. X11, Xext/Xcursor/Xi/Xfixes/Xrandr/Xss, PulseAudio,
# ALSA and udev are loaded at run time with dlopen (SDL's "-shared" options),
# so the executable has no link-time dependency on them and runs on systems
# that lack some of them (e.g. no PulseAudio: SDL falls back to ALSA).
# Wayland sessions use X11 through XWayland (SDL2's default).
# Needs tools/fetch_toolchain.sh (Zig, SDL2 source, focal headers).
set -euo pipefail
. "$(dirname "$0")/build_env.sh"

arch=${1:?usage: build_sdl2.sh x86_64|aarch64}
case $arch in x86_64|aarch64) ;; *) echo "unknown arch $arch" >&2; exit 2;; esac
SDL_VER=2.30.0
out=$SML_ROOT/build/sdl2-$arch
[ -f "$out/lib/libSDL2.a" ] && { echo "$out/lib/libSDL2.a is up to date"; exit 0; }

src=$(mktemp -d "$TMPDIR/sdl2-src.XXXXXX")
trap 'rm -rf "$src"' EXIT
tar -xzf "$SDL2_DIR/dl/SDL2-$SDL_VER.tar.gz" -C "$src" --strip-components=1

# configure finds the sonames to dlopen by listing the link path; when
# cross-compiling the real libraries are absent, so give it empty files
# with the sonames every glibc distribution ships. Nothing links them.
stubs=$src/soname-stubs
mkdir -p "$stubs"
for so in libX11.so.6 libXext.so.6 libXcursor.so.1 libXi.so.6 libXfixes.so.3 \
          libXrandr.so.2 libXss.so.1 libpulse.so.0 libpulse-simple.so.0 libasound.so.2 libudev.so.1; do
    : > "$stubs/$so"
done

target="$arch-linux-gnu.2.31"
inc=$SML_ROOT/tools/linux-headers/include
# ALSA is detected with a link test: a stub libasound with that one symbol.
echo 'int snd_ctl_open(void) { return 0; }' > "$src/asound_stub.c"
"$ZIG" cc -target "$target" -shared -o "$stubs/libasound.so" "$src/asound_stub.c"
# PulseAudio and ALSA are found through pkg-config.
mkdir -p "$src/pkgconfig"
printf 'Name: libpulse\nDescription: stub\nVersion: 13.99.1\nCflags: -I%s -D_REENTRANT\nLibs: -L%s -lpulse\n' \
    "$inc" "$stubs" > "$src/pkgconfig/libpulse.pc"
printf 'Name: alsa\nDescription: stub\nVersion: 1.2.2\nCflags: -I%s\nLibs: -L%s -lasound\n' \
    "$inc" "$stubs" > "$src/pkgconfig/alsa.pc"
export PKG_CONFIG_LIBDIR=$src/pkgconfig PKG_CONFIG_PATH=
cd "$src"
CC="$ZIG cc -target $target" AR="$ZIG ar" RANLIB="$ZIG ranlib" \
CFLAGS="-O2 -g0" CPPFLAGS="-I$inc" LDFLAGS="-L$stubs" \
./configure --host="$arch-linux-gnu" --prefix="$out" \
    --disable-shared --enable-static \
    --enable-video-x11 --enable-x11-shared \
    --x-includes="$inc" --x-libraries="$stubs" \
    --disable-video-wayland --disable-video-kmsdrm --disable-video-vivante \
    --disable-video-directfb --disable-video-rpi --disable-video-offscreen \
    --enable-pulseaudio --enable-pulseaudio-shared --enable-alsa --enable-alsa-shared \
    --disable-pipewire --disable-jack --disable-esd --disable-sndio --disable-nas \
    --disable-arts --disable-fusionsound --disable-libsamplerate \
    --disable-dbus --disable-ime --disable-ibus --disable-fcitx \
    --enable-libudev --disable-rpath > "$src/configure.log" 2>&1 \
    || { tail -40 "$src/configure.log"; exit 1; }
grep -E -- "-- dynamic" "$src/configure.log" || true
for need in SDL_AUDIO_DRIVER_PULSEAUDIO_DYNAMIC SDL_AUDIO_DRIVER_ALSA_DYNAMIC SDL_VIDEO_DRIVER_X11_DYNAMIC \
            SDL_VIDEO_OPENGL_GLX SDL_VIDEO_RENDER_OGL SDL_UDEV_DYNAMIC; do
    grep -q "#define $need " include/SDL_config.h || { echo "SDL configure did not enable $need" >&2; exit 1; }
done
jobs=$(nproc); [ "$jobs" -gt 10 ] && jobs=10   # at most 10 CPUs
make -s -j"$jobs" > "$src/make.log" 2>&1 || { tail -40 "$src/make.log"; exit 1; }
make -s install > /dev/null
echo "built $out/lib/libSDL2.a"
