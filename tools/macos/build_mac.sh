#!/bin/bash
# Builds Coreplay for macOS on a Mac set up by setup_build_mac.sh (no Homebrew):
#   tools/macos/build_mac.sh <arm64|x86_64> <version> [<build root>]     (run from the source tree)
# Writes <root>/out/<version>/Coreplay-<version>-macOS-<arch>.dmg. The same steps as the GitHub
# job in .github/workflows/build.yml: the libraries for macOS 12 (built once, kept under a
# fingerprint of their recipe), the app, its bundle (fix_bundle.sh), the macOS 12 check
# (check_minos.sh), the signature, the .dmg, and a start from the mounted .dmg with the
# build's libraries out of reach. An Apple silicon Mac builds both: x86_64 is cross-compiled
# and its start-up check runs under Rosetta.
# Signing: Developer ID when COREPLAY_SIGN_IDENTITY names one in the keychain, else ad-hoc.
# COREPLAY_TESTS=1: a test build (the --test-script driver), for the end-to-end tests only.
set -euo pipefail
arch="$1"; version="$2"; root="${3:-$HOME/CoreplayBuild}"
case "$arch" in arm64|x86_64) ;; *) echo "arch must be arm64 or x86_64"; exit 2 ;; esac
src="$(pwd)"
[ -f "$src/CMakeLists.txt" ] && [ -f "$src/tools/macos/build_deps.sh" ] || { echo "run from the Coreplay source tree"; exit 2; }
T="$root/tools"
QT="$T/Qt/6.8.3/macos"
export PATH="$T/prefix/bin:$T/venv/bin:/usr/bin:/bin:/usr/sbin:/sbin"
export MACOSX_DEPLOYMENT_TARGET=12.0
VCPKG_COMMIT="$(git -C "$T/vcpkg" rev-parse HEAD)"

# 1. The libraries, rebuilt only when build_deps.sh, a triplet or the vcpkg commit changes.
fp="$(cat tools/macos/build_deps.sh tools/macos/triplets/* | shasum -a 256 | cut -c1-16)-${VCPKG_COMMIT:0:12}"
DEPS="$root/deps/$arch-$fp"
if [ ! -f "$DEPS/mpv/lib/pkgconfig/mpv.pc" ]; then
  echo "== libraries for $arch ($fp): building (1-2 hours the first time)"
  export VCPKG_DEFAULT_BINARY_CACHE="$root/vcpkg-bin/$arch"; mkdir -p "$VCPKG_DEFAULT_BINARY_CACHE"
  bash tools/macos/build_deps.sh "$arch" "$DEPS" "$T/vcpkg"
  rm -rf "$DEPS/src"
fi
V="$(ls -d "$DEPS"/vcpkg/*-osx-12)"

# 2. The icon.
mkdir -p build-mac && rm -rf build-mac/coreplay.iconset && mkdir build-mac/coreplay.iconset
for s in 16 32 128 256 512; do
  sips -z $s $s res/coreplay.png --out build-mac/coreplay.iconset/icon_${s}x${s}.png >/dev/null
  sips -z $((s*2)) $((s*2)) res/coreplay.png --out build-mac/coreplay.iconset/icon_${s}x${s}@2x.png >/dev/null
done
iconutil -c icns build-mac/coreplay.iconset -o res/coreplay.icns

# 3. The app. (WrapOpenGL_AGL: Qt 6.8 links Apple's old AGL framework, which the macOS 27
#    SDK no longer has and Coreplay never uses; its search gets the OpenGL framework instead.)
B="build-mac/$arch"
rm -rf "$B"
export PKG_CONFIG_LIBDIR="$DEPS/mpv/lib/pkgconfig:$V/lib/pkgconfig"   # only ours
unset PKG_CONFIG_PATH
cmake -S . -B "$B" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 \
  -DCMAKE_IGNORE_PREFIX_PATH="/opt/homebrew;/usr/local" -DCMAKE_OSX_ARCHITECTURES="$arch" \
  -DWrapOpenGL_AGL="$(xcrun --show-sdk-path)/System/Library/Frameworks/OpenGL.framework" \
  -DCMAKE_PREFIX_PATH="$QT;$V" -DCOREPLAY_VERSION_FULL="$version" -DCOREPLAY_TESTS="$([ "${COREPLAY_TESTS:-}" = 1 ] && echo ON || echo OFF)"
ninja -C "$B"
app="$B/Coreplay.app"
plutil -replace LSMinimumSystemVersion -string 12.0 "$app/Contents/Info.plist"

# 4. The bundle: Qt and the libraries inside it, nothing outside, everything for macOS 12.
"$QT/bin/macdeployqt" "$app" -always-overwrite -libpath="$DEPS/mpv/lib" -libpath="$V/lib"
rm -f "$app/Contents/PlugIns/imageformats/libqpdf.dylib" "$app/Contents/PlugIns/imageformats/libqsvg.dylib" \
      "$app/Contents/PlugIns/iconengines/libqsvgicon.dylib" \
      "$app/Contents/PlugIns/platforminputcontexts/libqtvirtualkeyboardplugin.dylib"
bash tools/macos/fix_bundle.sh "$app"
bash tools/macos/check_minos.sh "$app" 12.0
# The universal Qt leaves both chips in every Qt file: keep this build's chip only.
find "$app/Contents" -type f | while IFS= read -r f; do
  if file -b "$f" | grep -q "universal binary"; then lipo -thin "$arch" "$f" -output "$f.thin" && mv "$f.thin" "$f"; fi
done
bad="$(find "$app/Contents" -type f -print0 | xargs -0 file | grep "Mach-O" | grep -v "$arch" || true)"
[ -z "$bad" ] || { echo "files for another chip in the app:"; echo "$bad"; exit 1; }

# 5. Signature.
if [ -n "${COREPLAY_SIGN_IDENTITY:-}" ]; then
  ent="$B/entitlements.plist"
  cat > "$ent" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>com.apple.security.cs.allow-jit</key><true/>
  <key>com.apple.security.cs.allow-unsigned-executable-memory</key><true/>
</dict></plist>
PLIST
  sign() { codesign --force --timestamp --options runtime --sign "$COREPLAY_SIGN_IDENTITY" "$@"; }
  find "$app/Contents" -type f \( -name '*.dylib' -o -name '*.so' \) -print0 | xargs -0 -n 20 codesign --force --timestamp --options runtime --sign "$COREPLAY_SIGN_IDENTITY"
  for f in "$app"/Contents/Frameworks/*.framework; do [ -e "$f" ] && sign "$f"; done
  sign --entitlements "$ent" "$app"
  codesign --verify --deep --strict --verbose=2 "$app"
else
  codesign --force --deep --sign - "$app"   # ad-hoc: runs, but not trusted by Gatekeeper
  codesign --verify --deep "$app"
fi

# 6. The .dmg.
out="$root/out/$version"; mkdir -p "$out"
dmg="$out/Coreplay-$version-macOS-$arch.dmg"
rm -rf "$B/dmg" && mkdir "$B/dmg" && cp -R "$app" "$B/dmg/" && ln -s /Applications "$B/dmg/Applications"
for i in 1 2 3 4 5; do
  hdiutil create -quiet -volname Coreplay -srcfolder "$B/dmg" -ov -format UDZO "$dmg" && break
  [ $i -eq 5 ] && exit 1
  echo "hdiutil busy, retry $i"; sleep 10
done
[ -n "${COREPLAY_SIGN_IDENTITY:-}" ] && codesign --force --timestamp --sign "$COREPLAY_SIGN_IDENTITY" "$dmg"
hdiutil verify -quiet "$dmg"

# 7. Start-up check: from the mounted .dmg, with the build's libraries out of reach.
mv "$DEPS" "$DEPS-hidden"
mnt="$(mktemp -d)"
cleanup() { hdiutil detach -quiet "$mnt" 2>/dev/null || true; [ -d "$DEPS-hidden" ] && mv "$DEPS-hidden" "$DEPS"; }
trap cleanup EXIT
hdiutil attach -quiet -nobrowse -readonly -mountpoint "$mnt" "$dmg"
exe="$mnt/Coreplay.app/Contents/MacOS/Coreplay"
[ "$(lipo -archs "$exe")" = "$arch" ] || { echo "the app is $(lipo -archs "$exe"), not $arch"; exit 1; }
"$exe" > "$B/start.log" 2>&1 & pid=$!
sleep 10
if kill -0 $pid 2>/dev/null; then
  echo "Coreplay $version ($arch) is running from the .dmg after 10 s"; kill $pid; sleep 1
else
  echo "Coreplay exited at start:"; cat "$B/start.log"; exit 1
fi
cleanup; trap - EXIT
shasum -a 256 "$dmg"
ls -la "$dmg"
