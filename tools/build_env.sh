# Sourced by other scripts (do not execute). Keeps every cache and temp file
# inside the project so nothing is written to ~/.cache, /tmp, etc.
# Usage:  . tools/build_env.sh
# SML_ROOT: set by the caller (the Makefile does), else derived from this
# file's location (bash), else the current directory (run from the repo root).
if [ -z "${SML_ROOT:-}" ]; then
    if [ -n "${BASH_VERSION:-}" ]; then
        SML_ROOT="$(cd "$(dirname "$(eval 'echo "${BASH_SOURCE[0]}"')")/.." && pwd)"
    else
        SML_ROOT="$(pwd)"
    fi
fi
export SML_ROOT

export TMPDIR="$SML_ROOT/build/tmp"
export XDG_CACHE_HOME="$SML_ROOT/build/cache"
export ZIG_GLOBAL_CACHE_DIR="$SML_ROOT/build/cache/zig-global"
export ZIG_LOCAL_CACHE_DIR="$SML_ROOT/build/cache/zig-local"
export PIP_NO_CACHE_DIR=1
export PYTHONDONTWRITEBYTECODE=1
mkdir -p "$TMPDIR" "$XDG_CACHE_HOME" "$ZIG_GLOBAL_CACHE_DIR" "$ZIG_LOCAL_CACHE_DIR"

export ZIG="$SML_ROOT/tools/zig/zig"
export SDL2_DIR="$SML_ROOT/tools/sdl2"
export SDL2_INCLUDE="$SDL2_DIR/include"
export SDL2_LINUX_X86_64="$SDL2_DIR/linux-x86_64"
export SDL2_LINUX_ARM64="$SDL2_DIR/linux-arm64"
export SDL2_MINGW="$SDL2_DIR/mingw/x86_64-w64-mingw32"
