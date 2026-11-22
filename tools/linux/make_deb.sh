#!/bin/bash
# Packs the Coreplay AppImage into coreplay_<version>_amd64.deb:
#   make_deb.sh <Coreplay-<version>-x86_64.AppImage> <version> <out dir>
# The .deb carries the same files as the AppImage (Qt, libmpv + ffmpeg, OpenImageIO,
# OpenColorIO) in /opt/coreplay, so it installs on Ubuntu 22.04+ and Debian 12+. A .deb
# built against a distribution's own Qt and OpenColorIO installs only on that release
# (Ubuntu 24.04 has Qt 6.4 and OpenColorIO 2.1; Coreplay needs Qt 6.5 and OCIO 2.3).
# Depends lists only what the AppImage takes from the system (glibc, the C++ runtime,
# OpenGL, X11/Wayland, fonts, sound): every desktop has these.
# Runs on any Debian or Ubuntu (needs dpkg-deb); no FUSE needed.
set -euo pipefail
appimage=$(readlink -f "$1")
version=$2
out=$(mkdir -p "$3" && cd "$3" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
root="$work/root"

cd "$work"
cp "$appimage" app.AppImage
chmod +x app.AppImage
./app.AppImage --appimage-extract >/dev/null
mkdir -p "$root/opt"
mv squashfs-root "$root/opt/coreplay"
chmod -R u+rwX,go+rX,go-w "$root/opt/coreplay"

# The command: AppRun sets up the bundled Qt and libraries, like the AppImage does.
mkdir -p "$root/usr/bin"
printf '#!/bin/sh\nexec /opt/coreplay/AppRun "$@"\n' > "$root/usr/bin/coreplay"
chmod 755 "$root/usr/bin/coreplay"

# Menu entry and icons for the system (the AppImage's own entry has its version line).
mkdir -p "$root/usr/share/applications"
grep -v '^X-AppImage-' "$root/opt/coreplay/usr/share/applications/coreplay.desktop" \
  > "$root/usr/share/applications/coreplay.desktop"
cp -r "$root/opt/coreplay/usr/share/icons" "$root/usr/share/"

size=$(du -sk "$root" | cut -f1)
mkdir -p "$root/DEBIAN"
cat > "$root/DEBIAN/control" <<EOF
Package: coreplay
Version: $version
Section: video
Priority: optional
Architecture: amd64
Installed-Size: $size
Depends: libc6 (>= 2.34), libstdc++6, libgcc-s1, zlib1g, libgl1, libegl1, libglx0, libglvnd0, libgbm1, libdrm2, libx11-6, libx11-xcb1, libxcb1, libxcb-dri3-0, libsm6, libice6, libwayland-client0, libfontconfig1, libfreetype6, libharfbuzz0b, libfribidi0, libasound2t64 | libasound2, libcom-err2, libgpg-error0
Maintainer: imCore <noreply@imcore.ai>
Homepage: https://imcore.ai
Description: Coreplay media player
 Plays every common video format and reviews image sequences (EXR, DPX, TIFF,
 PNG) with colour management: A/B compare, LUTs, colour grading, frame-accurate
 stepping and gapless playlists.
EOF
deb="$out/coreplay_${version}_amd64.deb"
dpkg-deb --root-owner-group -Zxz --build "$root" "$deb" >/dev/null
echo "Packed $deb ($(du -h "$deb" | cut -f1))"
