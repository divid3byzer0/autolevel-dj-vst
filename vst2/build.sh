#!/usr/bin/env bash
# Builds the 32-bit (x86) Windows VST2 plugin "AutoLevel DJ.dll" with MinGW-w64, on Linux or in
# MSYS2. No JUCE and no Steinberg SDK needed. Usage: vst2/build.sh [output-dir] (default build-vst2)
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(dirname "$here")"
out="${1:-$root/build-vst2}"
mkdir -p "$out"

CXX="${CXX:-}"
if [ -z "$CXX" ]; then
    for c in i686-w64-mingw32-g++-posix i686-w64-mingw32-g++ g++; do
        if command -v "$c" >/dev/null 2>&1; then CXX="$c"; break; fi
    done
fi
core="$root/external/autolevel-core/src"
if [ ! -f "$core/engine/AudioEngine.h" ]; then
    echo "external/autolevel-core is missing. Run: git submodule update --init" >&2
    exit 1
fi

# -msse2 -mfpmath=sse: the engine's flush-to-zero setting works on SSE math, as on x64.
# -mstackrealign: hosts built with MSVC only guarantee 4-byte stack alignment on x86.
# No -ffast-math: strict IEEE maths gives the same result wherever the engine is inlined, as
# in the JUCE build (MSVC, no /fp:fast).
"$CXX" -std=c++17 -shared -O2 -msse2 -mfpmath=sse -mstackrealign \
    -DUNICODE -D_UNICODE \
    -I"$here" -I"$core/engine" -I"$core/dsp" \
    "$here/Plugin.cpp" "$here/Editor.cpp" "$here/exports.def" \
    -o "$out/AutoLevel DJ.dll" \
    -static -static-libgcc -static-libstdc++ -s \
    -lgdiplus -lgdi32 -luser32 -lcomdlg32 -lshell32 -lole32
echo "Built $out/AutoLevel DJ.dll"
