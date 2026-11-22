#!/bin/bash
# Single-instance hand-over, end to end: a test instance starts; while it is still
# starting, three more launches (like an Explorer multi-select, one process per file,
# one of them "Add to Coreplay playlist") hand their files over. All three must land in
# ONE playlist, even though the first instance takes 5 s to start (a cold first start).
# A later, separate open replaces the playlist.
HERE="$(cd "$(dirname "$0")" && pwd)"; cd "$HERE/../../build" || exit 1
M="$COREPLAY_TEST_MEDIA"; O="$COREPLAY_TEST_OUT"
./coreplay.exe --test-instance --test-slow-start 5000 --test-script "$HERE/x_instance_inner.txt" --test-log "$O/t17_instance.log" &
P=$!
sleep 1   # the test instance takes the lock first; its 5 s slow start comes after
./coreplay.exe "$M/h264_23976.mp4" & ./coreplay.exe "$M/prores_25.mov" & ./coreplay.exe --add "$M/patches_709.mp4" &
wait %2 %3 %4 2>/dev/null
sleep 7
./coreplay.exe "$M/png_seq/comp.0001.png"
wait $P; rc=$?
echo "== t17_instance: exit $rc ($(grep -c PASS "$O/t17_instance.log") pass, $(grep -c FAIL "$O/t17_instance.log") fail)"
grep -E "FAIL|playlist|title" "$O/t17_instance.log"
exit $rc
