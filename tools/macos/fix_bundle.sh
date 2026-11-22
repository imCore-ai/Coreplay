#!/bin/bash
# Makes Coreplay.app need nothing outside itself (run after macdeployqt, before signing):
#   tools/macos/fix_bundle.sh build/Coreplay.app
# Homebrew's libraries find each other through search paths (LC_RPATH) such as ../lib and
# /opt/homebrew/lib. In the app they all sit in Contents/Frameworks, so each library gets
# @loader_path (its own folder) and every search path outside the app is removed. Then a
# check fails the build when any library still points outside the app or needs a file the
# app does not have. (A Mac without Homebrew crashed at launch: libOpenImageIO.3.2.dylib did
# not find libOpenImageIO_Util.3.2.dylib next to it. The build machine has Homebrew, so its
# start-up check passed.) Works with macOS's bash 3.2.
set -euo pipefail
app="$1"
fw="$app/Contents/Frameworks"
list="$(mktemp)"
find "$app/Contents" -type f | while IFS= read -r f; do
  if file -b "$f" | grep -q "Mach-O"; then echo "$f"; fi
done > "$list"

rpaths() { otool -l "$1" | awk '/cmd LC_RPATH/ {getline; getline; print $2}'; }
add_rpath() {
  if ! rpaths "$1" | grep -qx "$2"; then install_name_tool -add_rpath "$2" "$1" 2>/dev/null || true; fi
}

# 1. No search path outside the app.
while IFS= read -r b; do
  rpaths "$b" | while IFS= read -r rp; do
    case "$rp" in
      /opt/homebrew*|/usr/local*|/Users/*|/private/*) install_name_tool -delete_rpath "$rp" "$b" 2>/dev/null || true ;;
    esac
  done
done < "$list"

# 2. Every library finds the others in Frameworks, and its own name (install name) is its
#    place in the app, not Homebrew's.
for b in "$fw"/*.dylib; do
  [ -f "$b" ] || continue
  install_name_tool -id "@rpath/$(basename "$b")" "$b" 2>/dev/null || true
  add_rpath "$b" "@loader_path"
done
for f in "$fw"/*.framework; do
  [ -d "$f" ] || continue
  n="$(basename "$f" .framework)"
  b="$f/Versions/A/$n"
  [ -f "$b" ] || continue
  install_name_tool -id "@rpath/$n.framework/Versions/A/$n" "$b" 2>/dev/null || true
  add_rpath "$b" "@loader_path/../../.."
done
find "$app/Contents/PlugIns" -type f -name '*.dylib' | while IFS= read -r b; do
  add_rpath "$b" "@loader_path/../../Frameworks"
done
add_rpath "$app/Contents/MacOS/Coreplay" "@executable_path/../Frameworks"

# 3. Check: nothing points outside the app, and every @rpath library is in Frameworks.
bad=0
while IFS= read -r b; do
  for d in $(otool -L "$b" | tail -n +2 | awk '{print $1}'); do
    case "$d" in
      /opt/homebrew*|/usr/local*|/Users/*)
        echo "outside the app: ${b#$app/} -> $d"; bad=1 ;;
      @rpath/*)
        name="${d#@rpath/}"
        if [ ! -e "$fw/$name" ]; then echo "missing from Frameworks: $name (needed by ${b#$app/})"; bad=1; fi ;;
    esac
  done
  for rp in $(rpaths "$b"); do
    case "$rp" in /opt/homebrew*|/usr/local*|/Users/*) echo "search path outside the app: ${b#$app/} -> $rp"; bad=1 ;; esac
  done
done < "$list"
rm -f "$list"
if [ "$bad" -ne 0 ]; then echo "Coreplay.app needs files outside itself"; exit 1; fi
echo "Coreplay.app: every library resolves inside the app"
