#!/bin/bash
# The coreplay:// link, end to end. A test window runs; other launches are started with a
# link as their argument, the way the system starts Coreplay for a link (the whole link is
# one argument). They must hand the link to the running window.
HERE="$(cd "$(dirname "$0")" && pwd)"; cd "${COREPLAY_BUILD:-$HERE/../../build}" || exit 1
M="$COREPLAY_TEST_MEDIA"; O="$COREPLAY_TEST_OUT"
exe=./coreplay.exe; [ -x ./coreplay.exe ] || exe=./coreplay
$exe --test-instance --test-script "$HERE/x_link_inner.txt" --test-log "$O/t31_link.log" &
P=$!
sleep 4
# 1. Open at a frame.
$exe "coreplay://open?src=$M/h264_23976.mp4&frame=48"
sleep 4
# 2. Add to the playlist. The switches beside the link must be ignored: no second window,
#    and no second test run.
$exe "coreplay://open?src=$M/prores_25.mov&add=1" --new --test-script "$HERE/x_gl_fail.txt" --test-log "$O/t31_must_not_exist.log"
sleep 3
# 3. A refused link: the window says so and changes nothing.
$exe "coreplay://open?src=C:/Windows/System32/calc.exe"
sleep 3
# 4. A second clip for Compare.
$exe "coreplay://open?src=$M/cmp_red.mp4&b=$M/cmp_blue.mp4"
wait $P; rc=$?
if [ -e "$O/t31_must_not_exist.log" ]; then echo "FAIL  a switch beside a link was obeyed"; rc=$((rc + 1)); fi
echo "== t31_link: exit $rc ($(grep -c PASS "$O/t31_link.log") pass, $(grep -c FAIL "$O/t31_link.log") fail)"
grep -E "FAIL" "$O/t31_link.log"
exit $rc
