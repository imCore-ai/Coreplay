#!/bin/bash
# Linux build of Coreplay (Ubuntu 26.04), run inside the build container (tools/linux/Dockerfile):
#   docker build -t coreplay-linux-build tools/linux
#   docker run --rm -v "$PWD:/src:ro" -v "<out>:/out" coreplay-linux-build bash /src/build-linux.sh [--tests] [--deb]
# Builds in /out/build; with --deb also writes /out/coreplay_<version>_amd64.deb.
set -euo pipefail
SRC=/src
OUT=${OUT:-/out}
tests=OFF
deb=0
for a in "$@"; do
  case "$a" in
    --tests) tests=ON ;;
    --deb) deb=1 ;;
  esac
done
# Same version rule as build.ps1: major.minor from CMakeLists + the commit count.
count=$(git -c safe.directory='*' -C "$SRC" rev-list --count HEAD 2>/dev/null || echo 0)
base=$(sed -n 's/^project(Coreplay VERSION \([0-9]*\.[0-9]*\).*/\1/p' "$SRC/CMakeLists.txt")
version="$base.$count"
B="$OUT/build"
cmake -S "$SRC" -B "$B" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCOREPLAY_TESTS=$tests \
  -DOpenColorIO_DIR=/usr/share/cmake -DCOREPLAY_VERSION_FULL="$version"
ninja -C "$B"
echo "Built $B/coreplay ($version)"
[ $deb = 1 ] && bash "$SRC/tools/linux/make_deb.sh" "$B/coreplay" "$version" "$OUT"
exit 0
