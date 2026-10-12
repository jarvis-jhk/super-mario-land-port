# Building

## Prerequisites

Host: `gcc`/`g++`, `make`, `python3`, `curl`, plus `tar`, `xz`, `dpkg-deb`
(Debian/Ubuntu; only used to unpack archives, nothing is installed),
`sha256sum` and, for AppImages, `mksquashfs` (squashfs-tools). No SDL2 dev package, no cross-compilers, no Node. Everything else
is downloaded into the project by one script.

## Toolchain

    tools/fetch_toolchain.sh        # or: make toolchain

Idempotent and checksum-pinned. Downloads into `tools/` (git-ignored), about
450 MB:

| What | Where |
|------|-------|
| Zig 0.14.1 (C cross-compiler) | `tools/zig/` |
| SDL2 2.30.0 headers (source release) | `tools/sdl2/include/` |
| SDL2 2.30.0 MinGW dev package (import lib, `SDL2.dll`) | `tools/sdl2/mingw/` |
| libSDL2 2.30.0 from the Ubuntu 24.04 amd64 / arm64 `.deb`s (unpacked only, link stubs) | `tools/sdl2/linux-x86_64/`, `tools/sdl2/linux-arm64/` |
| X11/GL/PulseAudio/ALSA/udev headers from Ubuntu 20.04 `-dev` `.deb`s (for building SDL2) | `tools/linux-headers/` |
| AppImage type2 runtimes, release 20251108 (x86_64, aarch64) | `tools/appimage/` |

`tools/build_env.sh` (sourced by scripts and the Makefile) keeps all caches and
temp files inside the project: `TMPDIR=build/tmp`, `XDG_CACHE_HOME=build/cache`,
`ZIG_GLOBAL_CACHE_DIR`/`ZIG_LOCAL_CACHE_DIR` under `build/cache`.

`make sml` and the `sdl_hello` test link dynamically against
`libSDL2-2.0.so.0` (the system must provide it, on Ubuntu/Debian
`libsdl2-2.0-0`). The Linux release binaries link SDL2 statically (see
"The port" below). Windows builds ship `SDL2.dll` next to the exe.

## Platform toolchain test (`src/platform/sdl_hello.c`)

    make hello-linux-x86_64     # build/linux-x86_64/sdl_hello
    make hello-linux-aarch64    # build/linux-aarch64/sdl_hello
    make hello-windows-x86_64   # build/windows-x86_64/sdl_hello.exe + SDL2.dll
    make hello-all

Linux targets are built for glibc 2.31 or newer (`-target *-linux-gnu.2.31`).
Run: `build/linux-x86_64/sdl_hello` (640x576 window for 1 s) or
`build/linux-x86_64/sdl_hello --headless-test` (dummy video driver, exit 0).

Verified on the dev machine (Linux Mint, X11): the x86_64 binary runs, both
modes, using the system libSDL2 2.30.0. The aarch64 binary and the Windows exe
were only checked with `file`/`readelf` (correct ELF aarch64 / PE32+ x86-64,
`libSDL2-2.0.so.0` as the dynamic dependency); they have not been run (no
qemu/wine installed here).

## The port

    make sml                    # build/sml: host compiler, system libSDL2 (development)
    make check-frontend         # settings/audio unit tests + headless run of build/sml
    make port-linux-x86_64      # build/linux-x86_64/sml
    make port-linux-aarch64     # build/linux-aarch64/sml
    make port-windows-x86_64    # build/windows-x86_64/sml.exe + SDL2.dll
    make appimage-x86_64        # build/dist/Super_Mario_Land_Port-x86_64.AppImage
    make appimage-aarch64       # build/dist/Super_Mario_Land_Port-aarch64.AppImage
    make appimage-all

The Linux release binaries link SDL2 2.30.0 statically. `tools/build_sdl2.sh
<arch>` builds it from the source release with Zig for glibc 2.31
(`build/sdl2-<arch>/`, done automatically by the targets above). SDL loads
X11 (and Xext, Xcursor, Xi, Xfixes, Xrandr, Xss), OpenGL/GLX or GLES/EGL,
PulseAudio, ALSA and udev with `dlopen`, so it was configured with only
their headers, which `tools/fetch_toolchain.sh` takes from pinned Ubuntu 20.04
`-dev` packages (`tools/linux-headers/`). Wayland sessions use XWayland,
SDL2's default. The binaries need nothing but glibc 2.29 or newer
(`libc`, `libm`, `libpthread`, `libdl`).

`packaging/linux/make_appimage.sh` packs a binary with its desktop entry
and icon (`packaging/linux/make_icon.py`, original pixel art) into an
AppImage: the pinned type2 runtime (`tools/appimage/`) followed by a zstd
squashfs (`mksquashfs`, from squashfs-tools). No appimagetool is needed.

Verified on the dev machine (Linux Mint 22.3, X11): the x86_64 AppImage
starts through FUSE, finds the ROM next to it, plays with sound and
quits cleanly. The aarch64 binary and AppImage were built and checked
with `file`/`readelf` only (no arm64 machine or qemu here).

## ROM

The game data is read from the user's own ROM, which is not part of the
repository: put it at `rom/sml.gb` (Super Mario Land (World) Rev A / v1.1,
SHA1 `418203621b887caa090215d97e3f509b79affd3e`). `rom/` is git-ignored.

## Other targets

The Makefile's native targets (`make`, `make oracle`, `make check-emu`) use the
host `cc`; see the top of the Makefile.

## Local verification metadata after the public-tree cleanup

The core's access classifications are generated from your ROM and are not tracked.
Full builds need the local Gambatte reference and reference/tas/SML1.1Resync.bk2
as well as rom/sml.gb. To prepare them:

    make stable-hw
    make -j6 build/gbref build/emurun_s
    make build/resync11.movie
    python3 tools/regen_access_table.py --jobs 6
    make check check-states check-models SHELL="/bin/bash -o pipefail"

The Makefile also generates missing classifications before a full core build.
The generator never requires an old private table. Keep generated data local;
see docs/PUBLIC_AUDIT.md. Hardware-oracle font data is also local-only:

    python3 tools/regen_hwtest_glyphs.py

Without a ROM/reference checkout, make all build/frontend_test builds the emulator,
CPU-test runner and frontend unit tests. Full-core builds currently need the
locally generated access table; removing that build dependency is M0 CI work.
Anonymous reporting requires curl on Linux / curl.exe on Windows and can be
disabled with SML_NO_REPORT=1 during tests. Release builds use make VERSION=<tag> to define SML_VERSION.

If the worker has no system SDL2 (or the pinned dynamic stub needs a newer host
libc), build the static Linux binary with the pinned toolchain and verify it:

    make port-linux-x86_64
    SML_NO_REPORT=1 make check-frontend FRONTEND_BINARY=build/linux-x86_64/sml SHELL="/bin/bash -o pipefail"

In this minimal worker, g++ is absent. The independent oracle can be rebuilt
with the same pinned toolchain instead of using a copied host executable:

    . tools/build_env.sh
    make -B -j2 build/gbref CXX="$ZIG c++ -target x86_64-linux-gnu.2.31"
