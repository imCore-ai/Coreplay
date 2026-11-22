#!/bin/bash
# In-app update, end to end, against a LOCAL feed (http://127.0.0.1:8765/, plain http is
# accepted only for 127.0.0.1 / localhost; the manifest is signed either way):
#   $COREPLAY_TEST_MEDIA/feed/good    signed release "9.9.9" -> found, downloaded, verified
#   $COREPLAY_TEST_MEDIA/feed/badsig  one byte changed in latest.json -> refused
#   $COREPLAY_TEST_MEDIA/feed/badexe  one byte changed in the installer -> refused, deleted
# The feeds are made with tools/sign_update.py (see the README). Never runs the installer.
HERE="$(cd "$(dirname "$0")" && pwd)"; cd "$HERE/../../build" || exit 1
O="$COREPLAY_TEST_OUT"
python -I -m http.server 8765 --bind 127.0.0.1 --directory "$COREPLAY_TEST_MEDIA/feed" >/dev/null 2>&1 &
SRV=$!; sleep 1
total=0
for f in good badsig badexe; do
  COREPLAY_UPDATE_URL="http://127.0.0.1:8765/$f/" timeout 200 ./coreplay.exe --test-script "$HERE/x_update_$f.txt" --test-log "$O/t18_update_$f.log"
  rc=$?; total=$((total + rc))
  echo "== t18_update_$f: exit $rc ($(grep -c PASS "$O/t18_update_$f.log") pass, $(grep -c FAIL "$O/t18_update_$f.log") fail)"
  grep FAIL "$O/t18_update_$f.log"
done
kill $SRV 2>/dev/null
echo "TOTAL FAILURES: $total"
exit $(( total > 0 ))
