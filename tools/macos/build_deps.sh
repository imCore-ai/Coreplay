#!/bin/bash
# Builds every library Coreplay for Mac needs, for macOS 12 and later, into one folder:
#   tools/macos/build_deps.sh <arm64|x86_64> <prefix> <vcpkg root>
# <prefix>/vcpkg/<triplet>  OpenImageIO, OpenColorIO, FFmpeg (with the export encoders), libass
# <prefix>/mpv              libplacebo and libmpv, built on top of them
# Homebrew is used for build TOOLS only (meson, ninja, pkg-config, nasm): its libraries need
# the macOS they were built on (15 for Coreplay 0.3.x), so none of them goes into the app.
set -euo pipefail
arch="$1"; prefix="$2"; vcpkg="$3"
here="$(cd "$(dirname "$0")" && pwd)"
case "$arch" in
  arm64) triplet=arm64-osx-12 ;;
  x86_64) triplet=x64-osx-12 ;;
  *) echo "arch must be arm64 or x86_64"; exit 2 ;;
esac
export MACOSX_DEPLOYMENT_TARGET=12.0
mkdir -p "$prefix"
prefix="$(cd "$prefix" && pwd)"

# 1. vcpkg (classic mode: run outside the repository, whose vcpkg.json is for Windows).
#    PKG_CONFIG_LIBDIR = an empty folder: pkg-config's default folders (Homebrew's) are not searched
#    (passed into the builds by the triplets' VCPKG_ENV_PASSTHROUGH).
mkdir -p "$prefix/no-system-pkgconfig"
(cd "$prefix" && PKG_CONFIG_LIBDIR="$prefix/no-system-pkgconfig" "$vcpkg/vcpkg" install --triplet "$triplet" --overlay-triplets "$here/triplets" \
  --x-install-root "$prefix/vcpkg" --clean-after-build \
  openimageio opencolorio libass \
  "ffmpeg[core,avcodec,avdevice,avfilter,avformat,swresample,swscale,gpl,x264,x265,vpx,opus,dav1d,svt-av1,mp3lame,zlib,bzip2,lzma]")
V="$prefix/vcpkg/$triplet"
# LIBDIR, not PATH: Homebrew's pkg-config also searches Homebrew's own folder, and libmpv then
# linked Homebrew's fontconfig / freetype / libpng (built for macOS 14-15: check_minos caught it).
export PKG_CONFIG_LIBDIR="$prefix/mpv/lib/pkgconfig:$V/lib/pkgconfig"
unset PKG_CONFIG_PATH
flags="-mmacosx-version-min=12.0 -arch $arch"
src="$prefix/src"; mkdir -p "$src"

# 2. libplacebo: mpv's renderer. OpenGL only (Coreplay draws mpv's frames in its own GL view).
if [ ! -f "$prefix/mpv/lib/pkgconfig/libplacebo.pc" ]; then
  rm -rf "$src/libplacebo"
  git clone -q --depth 1 --branch v7.387.0 --recurse-submodules --shallow-submodules \
    https://github.com/haasn/libplacebo.git "$src/libplacebo"
  meson setup "$src/libplacebo/build" "$src/libplacebo" --prefix "$prefix/mpv" --libdir lib \
    --buildtype release -Ddefault_library=shared -Dc_args="$flags" -Dcpp_args="$flags" \
    -Dc_link_args="$flags" -Dcpp_link_args="$flags" \
    -Dvulkan=disabled -Dopengl=enabled -Dd3d11=disabled -Dglslang=disabled -Dshaderc=disabled \
    -Dlcms=disabled -Dlibdovi=disabled -Dxxhash=disabled -Dunwind=disabled \
    -Ddemos=false -Dtests=false
  meson install -C "$src/libplacebo/build"
fi

# 3. libmpv (no player program, no scripting): Coreplay drives it through its client API.
#    Swift stays on: mpv 0.42's Cocoa layer (also the GPU path for VideoToolbox frames) is
#    partly Swift; -Dswift-build=disabled failed in clipboard-mac.m ('osdep/mac/swift.h').
if [ ! -f "$prefix/mpv/lib/pkgconfig/mpv.pc" ]; then
  rm -rf "$src/mpv"
  git clone -q --depth 1 --branch v0.42.0 https://github.com/mpv-player/mpv.git "$src/mpv"
  # mpv 0.42 compiles view.swift whenever Cocoa + Swift are on, but its MetalLayer class only
  # with Vulkan (off here: Coreplay draws with OpenGL). The one use is a check of the layer's
  # class; MetalLayer is a CAMetalLayer, so checking for CAMetalLayer means the same.
  sed -i '' 's/layer as? MetalLayer {/layer as? CAMetalLayer {/' "$src/mpv/video/out/mac/view.swift"
  if grep -n "MetalLayer" "$src/mpv/video/out/mac/view.swift" | grep -v "CAMetalLayer" | grep -q .; then
    echo "mpv view.swift: the MetalLayer patch no longer applies (mpv changed)"; exit 1
  fi
  meson setup "$src/mpv/build" "$src/mpv" --prefix "$prefix/mpv" --libdir lib \
    --buildtype release -Ddefault_library=shared -Dc_args="$flags" -Dobjc_args="$flags" \
    -Dc_link_args="$flags" -Dobjc_link_args="$flags" \
    -Dlibmpv=true -Dcplayer=false -Dtests=false -Dfuzzers=false \
    -Dlua=disabled -Djavascript=disabled -Dmacos-media-player=disabled -Dmacos-touchbar=disabled \
    -Dswift-flags="-target $arch-apple-macos12.0" \
    -Dmanpage-build=disabled -Dhtml-build=disabled -Dpdf-build=disabled \
    -Dlibarchive=disabled -Dlibbluray=disabled -Ddvdnav=disabled -Dcdda=disabled \
    -Duchardet=disabled -Drubberband=disabled -Dzimg=disabled -Dvapoursynth=disabled \
    -Dlcms2=disabled -Djpeg=disabled -Dsdl2-audio=disabled -Dsdl2-video=disabled \
    -Dvulkan=disabled -Dshaderc=disabled -Dspirv-cross=disabled
  meson install -C "$src/mpv/build"
fi
echo "libraries for macOS 12 ($arch): $prefix"
