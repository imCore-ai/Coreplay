#!/bin/bash
# Fails when any program or library in the app needs a newer macOS than promised:
#   tools/macos/check_minos.sh build/Coreplay.app 12.0
# (Each Mach-O file records the oldest macOS it runs on. The 0.3.x downloads needed macOS 15
# because their Homebrew libraries did, and macOS 13 / 14 refused to start them.)
set -euo pipefail
app="$1"; want="$2"
bad=0; n=0
while IFS= read -r f; do
  file -b "$f" | grep -q "Mach-O" || continue
  n=$((n + 1))
  for v in $(otool -l "$f" | awk '/LC_BUILD_VERSION/ {b=1} b && /minos/ {print $2; b=0} /LC_VERSION_MIN_MACOSX/ {m=1} m && /version/ {print $2; m=0}'); do
    if [ "$(printf '%s\n%s\n' "$v" "$want" | sort -V | tail -1)" != "$want" ]; then
      echo "needs macOS $v: ${f#$app/}"; bad=1
    fi
  done
done < <(find "$app/Contents" -type f)
if [ "$bad" -ne 0 ]; then echo "Coreplay.app needs a macOS newer than $want"; exit 1; fi
echo "Coreplay.app: all $n programs and libraries run on macOS $want"
