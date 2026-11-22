#!/bin/bash
# No usable GPU driver: Coreplay notices it after the first paint, switches to the
# shipped software OpenGL at the next start, and uses mpv's fast scaling there.
HERE="$(cd "$(dirname "$0")" && pwd)"; cd "$HERE/../../build" || exit 1
O="$COREPLAY_TEST_OUT"; rc=0
./coreplay.exe --test-script "$HERE/x_gl_gpu.txt" --test-log "$O/t23_gl_gpu.log" || rc=1
COREPLAY_TEST_GL_FAIL=1 ./coreplay.exe --test-script "$HERE/x_gl_fail.txt" --test-log "$O/t23_gl_fail.log" || rc=1
./coreplay.exe --test-script "$HERE/x_gl_software.txt" --test-log "$O/t23_gl_software.log" || rc=1
p=$(cat "$O"/t23_gl_*.log | grep -c PASS); f=$(cat "$O"/t23_gl_*.log | grep -c FAIL)
echo "== t23_gl: exit $rc ($p pass, $f fail)"
grep -h FAIL "$O"/t23_gl_*.log
exit $rc
