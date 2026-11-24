#!/bin/bash
# In-app update on a Mac, end to end, against a LOCAL feed (http://127.0.0.1:8765/):
#   tests/e2e/run_update_mac.sh <old test build .dmg> <feed dir> <new version>
# <old test build .dmg>: a test build (COREPLAY_TESTS=1 tools/macos/build_mac.sh), an OLDER version.
# <feed dir>: good/ badsig/ badexe/, each the newer release's .dmg + latest.json + latest.json.sig
#   (tools/sign_update.py <version> --file <dmg> --out <dir>; badsig: one byte changed in
#   latest.json; badexe: one byte changed in the .dmg).
# The old app is copied to ~/Applications/CoreplayUpdateTest/ (a folder the user can write to,
# as /Applications is for an admin user). badsig and badexe must be refused; good must be
# downloaded, verified and installed: Coreplay closes, the new version takes its place and starts.
set -u
dmg="$1"; feed="$2"; want="$3"
HERE="$(cd "$(dirname "$0")" && pwd)"
O="${COREPLAY_TEST_OUT:-$HOME/CoreplayBuild/test-out}"; mkdir -p "$O"
dir="$HOME/Applications/CoreplayUpdateTest"; app="$dir/Coreplay.app"
install_old() {
  rm -rf "$dir"; mkdir -p "$dir"
  local mnt; mnt="$(mktemp -d)"
  hdiutil attach -quiet -nobrowse -readonly -mountpoint "$mnt" "$dmg" && ditto "$mnt/Coreplay.app" "$app"
  hdiutil detach -quiet "$mnt"; rmdir "$mnt"
}
version() { /usr/libexec/PlistBuddy -c "Print :CFBundleShortVersionString" "$app/Contents/Info.plist" 2>/dev/null; }
python3 -m http.server 8765 --bind 127.0.0.1 --directory "$feed" >/dev/null 2>&1 &
SRV=$!; sleep 1
total=0
install_old
old="$(version)"
echo "old app: $old in $app"
for f in badsig badexe; do
  COREPLAY_UPDATE_URL="http://127.0.0.1:8765/$f/" "$app/Contents/MacOS/Coreplay" --test-script "$HERE/x_update_$f.txt" \
    --test-log "$O/update_mac_$f.log" >/dev/null 2>&1
  rc=$?; total=$((total + rc))
  echo "== update_mac_$f: exit $rc ($(grep -c PASS "$O/update_mac_$f.log") pass, $(grep -c FAIL "$O/update_mac_$f.log") fail)"
  grep FAIL "$O/update_mac_$f.log"
done
[ "$(version)" = "$old" ] || { echo "FAIL  a refused update changed the app"; total=$((total + 1)); }

COREPLAY_UPDATE_URL="http://127.0.0.1:8765/good/" "$app/Contents/MacOS/Coreplay" --test-script "$HERE/x_update_install.txt" \
  --test-log "$O/update_mac_install.log" >/dev/null 2>&1
echo "== update_mac_install: $(grep -c PASS "$O/update_mac_install.log") pass, $(grep -c FAIL "$O/update_mac_install.log") fail"
grep FAIL "$O/update_mac_install.log" && total=$((total + 1))
# The update runs after Coreplay has closed: wait for the new version in place and running.
for i in $(seq 1 60); do [ "$(version)" = "$want" ] && break; sleep 1; done
if [ "$(version)" = "$want" ]; then echo "PASS  the app in place is $want (was $old)"; else echo "FAIL  the app in place is $(version), not $want"; total=$((total + 1)); fi
pid=""
for i in $(seq 1 30); do pid="$(pgrep -f "$app/Contents/MacOS/Coreplay" | head -1)"; [ -n "$pid" ] && break; sleep 1; done
if [ -n "$pid" ]; then echo "PASS  the new version started (pid $pid)"; kill "$pid"; else echo "FAIL  the new version did not start"; total=$((total + 1)); fi
leftover="$(ls -d "$dir"/*.update.* "$dir"/*.old.* 2>/dev/null)"
[ -z "$leftover" ] && echo "PASS  nothing left over" || { echo "FAIL  left over: $leftover"; total=$((total + 1)); }
codesign --verify --deep "$app" && echo "PASS  the new app's signature is intact" || total=$((total + 1))
kill $SRV 2>/dev/null
echo "TOTAL FAILURES: $total"
exit $(( total > 0 ))
