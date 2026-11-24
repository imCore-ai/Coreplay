#!/bin/bash
# In-app update on Linux, end to end, inside a clean ubuntu:22.04 container, against a LOCAL
# feed (http://127.0.0.1:8765/). Real dpkg, apt, pkexec and polkitd; a polkit rule stands in
# for the user's password ("yes" = typed right, "no" = refused).
#   docker run --rm -v <debs>:/deb:ro -v <AppImages>:/ai:ro -v <feed>:/feed:ro \
#     -v <repo>/tests/e2e:/tests:ro ubuntu:22.04 bash /tests/run_update_linux.sh <old> <new>
# <debs>: coreplay_<old>_amd64.deb (a test build: build-appimage.sh --tests, then
#   tools/linux/make_deb.sh). <AppImages>: Coreplay-<old>-x86_64.AppImage (a test build).
# <feed>: deb/good, deb/badsig, deb/badexe and appimage/good, each the newer release
#   (tools/sign_update.py <new> --file <file> --out <dir>; badsig: one byte changed in
#   latest.json; badexe: one byte changed in the package).
# badsig and badexe must be refused; good must be downloaded, verified and installed: Coreplay
# closes, the new version takes its place and starts. A refused password keeps the old version
# and starts it again.
old="$1"; new="$2"
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq >/dev/null
apt-get install -y -qq policykit-1 dbus xvfb xauth python3 procps libgl1-mesa-dri >/dev/null 2>&1
useradd -m tester
mkdir -p /run/dbus && dbus-daemon --system --fork
/usr/lib/policykit-1/polkitd --no-debug >/tmp/polkitd.log 2>&1 &
Xvfb :99 -screen 0 1600x1000x24 >/dev/null 2>&1 &
python3 -m http.server 8765 --bind 127.0.0.1 --directory /feed >/dev/null 2>&1 &
sleep 2
pass=0; fail=0
check() { if eval "$2"; then echo "PASS  $1"; pass=$((pass+1)); else echo "FAIL  $1"; fail=$((fail+1)); fi; }
ver() { dpkg-query -W -f='${Version}' coreplay 2>/dev/null; }
running() { pgrep -u tester -f "$1" >/dev/null; }
# A Coreplay from the .deb is running (it shows as /opt/coreplay/AppRun; /proc/<pid>/exe of
# another user cannot be read in a container without ptrace).
deb_running() { running "^/opt/coreplay/"; }
# app <name> <feed> <script> [env]: one test run as the user, its result lines and PASS/FAIL.
app() {
  su tester -c "cd ~ && $4 DISPLAY=:99 COREPLAY_UPDATE_URL=http://127.0.0.1:8765/$2/ timeout 300 $5 --test-script /tests/$3 --test-log /tmp/app-$1.log" >/tmp/app-$1.out 2>&1
  log=/tmp/app-$1.log
  grep -E "PASS|FAIL" $log | sed 's/^/   app: /'
  check "$1: test script passed" '! grep -q FAIL $log && grep -q PASS $log'
}
password() {
  mkdir -p /etc/polkit-1/localauthority/50-local.d
  printf '[test]\nIdentity=unix-user:tester\nAction=org.freedesktop.policykit.exec\nResultAny=%s\nResultInactive=%s\nResultActive=%s\n' $1 $1 $1 \
    > /etc/polkit-1/localauthority/50-local.d/coreplay-test.pkla
  sleep 1
}
fresh_deb() { pkill -u tester -f /opt/coreplay; sleep 1; apt-get install -y -qq --allow-downgrades /deb/coreplay_${old}_amd64.deb >/dev/null 2>&1; }

echo "== .deb: refused feeds"
fresh_deb; password yes
for f in badsig badexe; do
  app deb-$f deb/$f x_update_$f.txt "" /usr/bin/coreplay
  check "deb-$f: still $old" '[ "$(ver)" = "$old" ]'
done

for p in yes no; do
  echo "== .deb: good feed, password $p"
  fresh_deb; password $p
  check "deb-$p: $old installed before" '[ "$(ver)" = "$old" ]'
  app deb-$p deb/good x_update_install.txt "" /usr/bin/coreplay
  # Coreplay has ended; apt runs (or is refused) and Coreplay starts again.
  for i in $(seq 1 120); do deb_running && break; sleep 1; done
  sleep 3
  if [ $p = yes ]; then check "deb-yes: $new installed after" '[ "$(ver)" = "$new" ]'
  else check "deb-no: still $old after a refused password" '[ "$(ver)" = "$old" ]'; fi
  check "deb-$p: Coreplay started again" deb_running
  sleep 5
  check "deb-$p: and is still running 5 s later" deb_running
  check "deb-$p: not the test run" '! running "test-script"'
  check "deb-$p: the downloaded .deb was removed" '[ -z "$(ls /tmp/Coreplay-update/*.deb 2>/dev/null)" ]'
done
pkill -u tester -f /opt/coreplay; sleep 1

echo "== AppImage: good feed"
apt-get remove -y -qq coreplay >/dev/null 2>&1
ai=/home/tester/Apps/Coreplay.AppImage
su tester -c "mkdir -p ~/Apps && cp /ai/Coreplay-${old}-x86_64.AppImage $ai && chmod +x $ai"
want=$(sha256sum /feed/appimage/good/Coreplay-${new}-x86_64.AppImage | cut -d' ' -f1)
app ai-good appimage/good x_update_install.txt "APPIMAGE_EXTRACT_AND_RUN=1" $ai
for i in $(seq 1 120); do running "^$ai" && break; sleep 1; done
sleep 3
check "ai-good: the AppImage is now $new" '[ "$(sha256sum $ai | cut -d" " -f1)" = "$want" ]'
check "ai-good: Coreplay started again" 'running "^$ai"'
check "ai-good: no .update file left" '[ ! -e $ai.update ]'
pkill -u tester -f "$ai"
echo "TOTAL pass=$pass fail=$fail"
[ $fail = 0 ]
