#!/bin/bash
# Runs the end-to-end scripts one at a time against a test build
# (.\build.ps1 -Tests). The window opens without taking keyboard focus.
#   COREPLAY_TEST_MEDIA=<media-dir> COREPLAY_TEST_OUT=<out-dir> tests/e2e/run.sh [t1_png t5_movie ...]
HERE="$(cd "$(dirname "$0")" && pwd)"
: "${COREPLAY_TEST_MEDIA:?set COREPLAY_TEST_MEDIA (see make_media.sh)}"
: "${COREPLAY_TEST_OUT:?set COREPLAY_TEST_OUT}"
export COREPLAY_TEST_MEDIA COREPLAY_TEST_OUT
mkdir -p "$COREPLAY_TEST_OUT"
cd "${COREPLAY_BUILD:-$HERE/../../build}" || exit 1   # COREPLAY_BUILD: another build folder (Linux)
exe=./coreplay.exe; [ -x ./coreplay.exe ] || exe=./coreplay
tests=("$@")
[ ${#tests[@]} -eq 0 ] && tests=($(cd "$HERE" && ls t*.txt | sed 's/\.txt$//'))
# Every script starts from default settings: test runs keep their own (CoreplayTest, never
# the user's), and an interrupted run can leave them changed (for example Loop turned off).
reset_settings() {
  if [ "$exe" = "./coreplay.exe" ]; then
    MSYS_NO_PATHCONV=1 reg delete 'HKCU\Software\CoreplayTest' /f >/dev/null 2>&1
  else
    rm -rf "${XDG_CONFIG_HOME:-$HOME/.config}/CoreplayTest"
  fi
}
total=0
for t in "${tests[@]}"; do
  reset_settings
  limit=120
  case "$t" in *export*) limit=900 ;; esac   # real encodes take longer
  timeout $limit $exe --test-script "$HERE/$t.txt" --test-log "$COREPLAY_TEST_OUT/$t.log"
  rc=$?
  total=$((total + rc))
  echo "== $t: exit $rc  ($(grep -c PASS "$COREPLAY_TEST_OUT/$t.log") pass, $(grep -c FAIL "$COREPLAY_TEST_OUT/$t.log") fail)"
  grep FAIL "$COREPLAY_TEST_OUT/$t.log"
done
echo "TOTAL FAILURES: $total"
exit $(( total > 0 ))
