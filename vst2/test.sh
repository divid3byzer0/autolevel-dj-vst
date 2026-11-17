#!/usr/bin/env bash
# Builds the VST2 test host and runs it against the built plugin under Wine (32-bit), on a
# virtual display when there is none. Usage: vst2/test.sh [build-dir] (default build-vst2)
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(dirname "$here")"
out="${1:-$root/build-vst2}"
core="$root/external/autolevel-core/src"
CXX="${CXX:-$(command -v i686-w64-mingw32-g++-posix || command -v i686-w64-mingw32-g++)}"

# Same maths flags as build.sh, so the test's own copy of the engine computes identically.
"$CXX" -std=c++17 -O2 -msse2 -mfpmath=sse -mstackrealign -DUNICODE -D_UNICODE \
    -I"$here" -I"$core/engine" -I"$core/dsp" "$here/tests/testhost.cpp" \
    -o "$out/testhost.exe" -static -lgdi32 -luser32

export WINEDEBUG=-all
export WINEPREFIX="${WINEPREFIX:-$out/wineprefix}"
xvfb_pid=""
if [ -z "${DISPLAY:-}" ]; then
    export DISPLAY=:91
    Xvfb "$DISPLAY" -screen 0 1280x1024x24 >/dev/null 2>&1 &
    xvfb_pid=$!
    sleep 2
fi
cd "$out"
status=0
timeout 600 wine testhost.exe "AutoLevel DJ.dll" "${SHOTS:-}" || status=$?
wineserver -k 2>/dev/null || true
[ -n "$xvfb_pid" ] && kill "$xvfb_pid" 2>/dev/null || true
exit $status
