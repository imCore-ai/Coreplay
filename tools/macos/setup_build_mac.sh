#!/bin/bash
# Sets up a Mac to build Coreplay for macOS (Apple silicon and Intel), with no Homebrew:
#   tools/macos/setup_build_mac.sh <build root>      (default: ~/CoreplayBuild)
# Everything goes into <root>/tools: Python 3.11 + meson, ninja, cmake, pkgconf and aqtinstall
# (through uv), nasm and the GNU autotools from their source releases, Qt 6.8.3 (universal:
# arm64 + x86_64), and vcpkg at the commit the GitHub build uses. Needs Apple's command-line
# tools (xcode-select --install) and uv (https://docs.astral.sh/uv/) in <root>/tools/uv or PATH.
# Safe to run again: each step is skipped when its result is already there.
set -euo pipefail
root="${1:-$HOME/CoreplayBuild}"
T="$root/tools"; mkdir -p "$T/src" "$T/prefix/bin"
VCPKG_COMMIT=2fb57e1c4738d4b4484d2234c886caf006612222
QT_VERSION=6.8.3
NASM=2.16.03; M4=1.4.19; AUTOCONF=2.72; AUTOMAKE=1.17; LIBTOOL=2.5.4
export PATH="$T/prefix/bin:$T/venv/bin:$PATH"

uv="$T/uv"; [ -x "$uv" ] || uv="$(command -v uv || true)"
[ -n "$uv" ] || { echo "uv not found: put it at $T/uv"; exit 1; }

# 1. Python tools (arm64; meson and ninja drive the x86_64 builds too, with -arch flags).
if [ ! -x "$T/venv/bin/meson" ]; then
  export UV_PYTHON_INSTALL_DIR="$T/python" UV_CACHE_DIR="$T/uv-cache"
  "$uv" python install cpython-3.11-macos-aarch64
  "$uv" venv --seed --python cpython-3.11-macos-aarch64 "$T/venv"
  "$T/venv/bin/python" -m pip install -q "meson==1.6.1" "ninja==1.11.1.3" "cmake==3.31.6" "pkgconf==2.3.0.post1" "aqtinstall==3.3.0"
fi
# The program itself, not the wrapper (the wrapper prints a banner and empties PKG_CONFIG_PATH).
ln -sf "$T/venv/lib/python3.11/site-packages/pkgconf/.bin/pkgconf" "$T/prefix/bin/pkg-config"

fetch() {  # fetch <url> <file>: downloads once into tools/src
  [ -f "$T/src/$2" ] || curl -fL --retry 5 -o "$T/src/$2" "$1"
}
# 2. nasm (x86 assembler for FFmpeg, x264, x265, dav1d, libjpeg-turbo on the Intel build).
if [ ! -x "$T/prefix/bin/nasm" ]; then
  fetch "https://www.nasm.us/pub/nasm/releasebuilds/$NASM/nasm-$NASM.tar.gz" "nasm-$NASM.tar.gz"
  rm -rf "$T/src/nasm-$NASM" && tar -xzf "$T/src/nasm-$NASM.tar.gz" -C "$T/src"
  (cd "$T/src/nasm-$NASM" && ./configure -q --prefix="$T/prefix" && make -s -j"$(sysctl -n hw.ncpu)" && make -s install)
fi
# 3. GNU autotools (vcpkg ports that use configure: mp3lame and others).
build_gnu() {  # build_gnu <name> <version>
  [ -x "$T/prefix/bin/$1" ] || [ -x "$T/prefix/bin/g$1" ] && return 0
  fetch "https://ftpmirror.gnu.org/gnu/$1/$1-$2.tar.gz" "$1-$2.tar.gz"
  rm -rf "$T/src/$1-$2" && tar -xzf "$T/src/$1-$2.tar.gz" -C "$T/src"
  (cd "$T/src/$1-$2" && ./configure -q --prefix="$T/prefix" "${@:3}" && make -s -j"$(sysctl -n hw.ncpu)" && make -s install)
}
build_gnu m4 "$M4"                                 # autoconf needs GNU m4 (macOS has BSD m4)
build_gnu autoconf "$AUTOCONF"
build_gnu automake "$AUTOMAKE"
build_gnu libtool "$LIBTOOL" --program-prefix=g   # glibtool / glibtoolize: Apple's libtool keeps its name
ln -sf "$T/prefix/bin/glibtoolize" "$T/prefix/bin/libtoolize"

# 4. Qt (the macOS build is universal: one install for both chips).
if [ ! -x "$T/Qt/$QT_VERSION/macos/bin/macdeployqt" ]; then
  (cd "$T" && "$T/venv/bin/aqt" install-qt mac desktop "$QT_VERSION" clang_64 --outputdir "$T/Qt")
fi

# 5. vcpkg at the GitHub build's commit.
if [ ! -x "$T/vcpkg/vcpkg" ]; then
  [ -d "$T/vcpkg/.git" ] || git clone -q https://github.com/microsoft/vcpkg "$T/vcpkg"
  git -C "$T/vcpkg" fetch -q origin "$VCPKG_COMMIT" || true
  git -C "$T/vcpkg" checkout -q "$VCPKG_COMMIT"
  "$T/vcpkg/bootstrap-vcpkg.sh" -disableMetrics
fi

echo "== tools in $T"
for t in meson ninja cmake pkg-config nasm autoconf automake glibtool; do printf '%-11s %s\n' "$t" "$("$t" --version 2>&1 | head -1)"; done
echo "Qt          $T/Qt/$QT_VERSION/macos"; lipo -archs "$T/Qt/$QT_VERSION/macos/lib/QtCore.framework/QtCore"
echo "vcpkg       $(git -C "$T/vcpkg" rev-parse --short=12 HEAD)"
