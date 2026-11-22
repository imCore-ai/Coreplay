#!/bin/bash
# "Open each file in a new window", end to end: a test instance plays a file; three
# launches at once (an Explorer multi-select) hand over; the running window starts ONE
# second window and forwards the rest of the selection to it. Then, with the setting
# off, a launch replaces the file in the first window. Close any real Coreplay first:
# this uses the same hand-over name.
HERE="$(cd "$(dirname "$0")" && pwd)"; cd "$HERE/../../build" || exit 1
M="$COREPLAY_TEST_MEDIA"; O="$COREPLAY_TEST_OUT"
export COREPLAY_TEST_CHILD_SCRIPT="$HERE/x_newwindow_child.txt" COREPLAY_TEST_CHILD_LOG="$O/t21_child.log"
rm -f "$O/t21_child.log"
./coreplay.exe --test-instance --test-script "$HERE/x_newwindow_owner.txt" --test-log "$O/t21_newwindow.log" &
P=$!
sleep 4
./coreplay.exe "$M/gap_a.mp4" & ./coreplay.exe "$M/gap_b.mp4" & ./coreplay.exe "$M/patches_709.mp4" &
wait %2 %3 %4 2>/dev/null
sleep 8
./coreplay.exe "$M/prores_25.mov"
wait $P; rc=$?
for i in $(seq 1 40); do grep -q "state" "$O/t21_child.log" 2>/dev/null && break; sleep 0.5; done
crc=$(grep -c FAIL "$O/t21_child.log" 2>/dev/null || echo 1); [ -f "$O/t21_child.log" ] || crc=1
echo "== t21_newwindow: exit $rc ($(grep -c PASS "$O/t21_newwindow.log") pass, $(grep -c FAIL "$O/t21_newwindow.log") fail); child window: $(grep -c PASS "$O/t21_child.log" 2>/dev/null) pass, $crc fail"
grep -E "FAIL|handovers" "$O/t21_newwindow.log" "$O/t21_child.log"
exit $(( rc > 0 || crc > 0 ))
