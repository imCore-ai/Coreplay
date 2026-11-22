#!/bin/bash
# Packs a Linux build into coreplay_<version>_amd64.deb (Ubuntu 26.04 and newer):
#   make_deb.sh <build/coreplay> <version> <out dir>
# The libraries it needs (Qt 6, libmpv, OpenImageIO, OpenColorIO) come from the
# distribution: dpkg-shlibdeps writes them into Depends.
set -euo pipefail
exe=$1
version=$2
out=$3
src=$(cd "$(dirname "$0")/../.." && pwd)
root=$(mktemp -d)
trap 'rm -rf "$root"' EXIT

install -Dm755 "$exe" "$root/usr/bin/coreplay"
strip "$root/usr/bin/coreplay"
for s in 16 24 32 48 64 128 256 512; do
  mkdir -p "$root/usr/share/icons/hicolor/${s}x${s}/apps"
  ffmpeg -loglevel error -y -i "$src/res/coreplay.png" -vf "scale=$s:$s:flags=lanczos" \
    "$root/usr/share/icons/hicolor/${s}x${s}/apps/coreplay.png"
done
mkdir -p "$root/usr/share/applications"
cat > "$root/usr/share/applications/coreplay.desktop" <<EOF
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
EOF

# Depends from the libraries the program links (dpkg-shlibdeps wants a debian/control).
work=$(mktemp -d)
mkdir -p "$work/debian"
printf 'Source: coreplay\n\nPackage: coreplay\nArchitecture: amd64\n' > "$work/debian/control"
depends=$(cd "$work" && dpkg-shlibdeps -O -e "$root/usr/bin/coreplay" 2>/dev/null | sed -n 's/^shlibs:Depends=//p')
rm -rf "$work"
size=$(du -sk "$root/usr" | cut -f1)
mkdir -p "$root/DEBIAN"
cat > "$root/DEBIAN/control" <<EOF
Package: coreplay
Version: $version
Section: video
Priority: optional
Architecture: amd64
Installed-Size: $size
Depends: $depends
Recommends: qt6-wayland
Maintainer: Coreplay <noreply@imcore.ai>
Homepage: https://imcore.ai
Description: Coreplay media player
 Plays and reviews movies (through libmpv) and image sequences (EXR, DPX, TIFF,
 PNG through OpenImageIO, colour through OpenColorIO): A/B compare, LUTs, colour
 grading, frame-accurate stepping and gapless playlists.
EOF
mkdir -p "$out"
deb="$out/coreplay_${version}_amd64.deb"
dpkg-deb --root-owner-group --build "$root" "$deb" >/dev/null
echo "Packed $deb"
