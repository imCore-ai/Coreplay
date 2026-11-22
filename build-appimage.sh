#!/bin/bash
# AppImage of Coreplay, run inside tools/linux/appimage.Dockerfile:
#   docker build -t coreplay-appimage-build -f tools/linux/appimage.Dockerfile tools/linux
#   docker run --rm -v "$PWD:/src:ro" -v "<out>:/out" coreplay-appimage-build bash /src/build-appimage.sh [--tests]
# Writes /out/Coreplay-<version>-x86_64.AppImage. It carries Qt, libmpv + ffmpeg,
# OpenImageIO and OpenColorIO; the system gives glibc, libstdc++, OpenGL and the GPU driver.
set -euo pipefail
SRC=/src
OUT=${OUT:-/out}
tests=OFF
for a in "$@"; do
  case "$a" in
    --tests) tests=ON ;;
  esac
done
count=$(git -c safe.directory='*' -C "$SRC" rev-list --count HEAD 2>/dev/null || echo 0)
base=$(sed -n 's/^project(Coreplay VERSION \([0-9]*\.[0-9]*\).*/\1/p' "$SRC/CMakeLists.txt")
version="$base.$count"
B="$OUT/build-appimage"
cmake -S "$SRC" -B "$B" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCOREPLAY_TESTS=$tests \
  -DCMAKE_PREFIX_PATH="/opt/oiio;/opt/ocio" -DCOREPLAY_VERSION_FULL="$version"
ninja -C "$B"
echo "Built $B/coreplay ($version)"

app=$(mktemp -d)/AppDir
trap 'rm -rf "$(dirname "$app")"' EXIT
install -Dm755 "$B/coreplay" "$app/usr/bin/coreplay"
for s in 16 32 48 64 128 256 512; do
  mkdir -p "$app/usr/share/icons/hicolor/${s}x${s}/apps"
  ffmpeg -loglevel error -y -i "$SRC/res/coreplay.png" -vf "scale=$s:$s:flags=lanczos" \
    "$app/usr/share/icons/hicolor/${s}x${s}/apps/coreplay.png"
done
desktop="$(dirname "$app")/coreplay.desktop"
cat > "$desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Coreplay
GenericName=Media Player
Comment=Play and review movies and image sequences (EXR, DPX, TIFF, PNG)
Exec=coreplay %F
Icon=coreplay
Terminal=false
Categories=AudioVideo;Video;Player;Graphics;
MimeType=video/mp4;video/quicktime;video/x-matroska;video/webm;video/x-msvideo;video/mpeg;video/x-m4v;image/x-exr;image/x-dpx;image/tiff;image/png;image/jpeg;
StartupWMClass=coreplay
X-AppImage-Version=$version
EOF

export LD_LIBRARY_PATH=/opt/oiio/lib64:/opt/ocio/lib64${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
export QMAKE=/usr/bin/qmake6
export EXTRA_PLATFORM_PLUGINS="libqwayland.so"
export NO_STRIP=0
linuxdeploy --appdir "$app" -d "$desktop" \
  -i "$app/usr/share/icons/hicolor/256x256/apps/coreplay.png" --plugin qt
# The C++ runtime must come from the system: the GPU driver (Mesa's LLVM, NVIDIA)
# loads into the same process and needs the system's newer libstdc++.
rm -f "$app"/usr/lib/libstdc++.so* "$app"/usr/lib/libgcc_s.so*
# linuxdeploy leaves these to the system, but many desktops do not have them, and
# Coreplay does not start without them. Each only talks to a system service (the GL
# driver, a JACK or PipeWire server); mpv falls back to PulseAudio / ALSA.
for f in /usr/lib64/libOpenGL.so.0 /usr/lib64/pipewire-0.3/jack/libjack.so.0 /usr/lib64/libpipewire-0.3.so.0; do
  l=$(basename "$f")
  cp -L "$f" "$app/usr/lib/$l"
  patchelf --set-rpath '$ORIGIN' "$app/usr/lib/$l"
done
mkdir -p "$OUT"
cd "$OUT"
rm -f Coreplay-*-x86_64.AppImage
ARCH=x86_64 VERSION="$version" linuxdeploy --appdir "$app" --output appimage
mv Coreplay-*x86_64.AppImage "Coreplay-$version-x86_64.AppImage" 2>/dev/null || true
ls -la "$OUT"/Coreplay-*.AppImage
