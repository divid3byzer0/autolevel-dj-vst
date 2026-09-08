#!/usr/bin/env bash
# ==============================================================================
# AutoLevel DJ - Raspberry Pi 4 Build & Installation Script
# ==============================================================================
# Target: Raspberry Pi 4 Model B (4GB) running Patchbox OS or 64-bit Raspberry Pi OS
# Output: VST3, LV2 plugin bundle, and Standalone JACK/ALSA application
# ==============================================================================

set -e

echo "=== AutoLevel DJ: Raspberry Pi 4 Build Setup ==="

# 1. Update and install build dependencies
echo "[1/5] Checking and installing system dependencies..."
sudo apt-get update
sudo apt-get install -y \
    pkg-config \
    cmake \
    ninja-build \
    build-essential \
    git \
    libasound2-dev \
    libjack-jackd2-dev \
    libfreetype6-dev \
    libfontconfig1-dev \
    libgtk-3-dev \
    libgl1-mesa-dev \
    libx11-dev \
    libxcomposite-dev \
    libxcursor-dev \
    libxext-dev \
    libxinerama-dev \
    libxrandr-dev \
    libxrender-dev

# Ensure FreeType headers are visible to the compiler
if [ -d /usr/include/freetype2 ]; then
    sudo ln -sf /usr/include/freetype2/ft2build.h /usr/include/ft2build.h 2>/dev/null || true
    sudo ln -sf /usr/include/freetype2/freetype /usr/include/freetype 2>/dev/null || true
fi
export CPATH="/usr/include/freetype2:${CPATH}"

# 2. Configure CMake
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

echo "[2/5] Configuring CMake..."
cd "${REPO_ROOT}"

# Prevent Ninja from spawning too many parallel g++ processes.
export CMAKE_BUILD_PARALLEL_LEVEL=2

# Only wipe build directory if explicitly asked or if previous config was broken
if [ "$1" = "--clean" ] || [ -d "build-pi" -a ! -f "build-pi/build.ninja" ]; then
    echo "Cleaning build directory..."
    rm -rf build-pi
fi

cmake -B build-pi -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_STANDARD=20

# 3. Compile VST3, LV2, Standalone, and unit test suite
echo "[3/5] Building AutoLevel DJ (VST3, LV2, Standalone)..."
cmake --build build-pi --config Release -j2

# 4. Run DSP unit tests
echo "[4/5] Running DSP unit tests to verify bit-perfect accuracy..."
./build-pi/dsp_test

# 5. Install plugins
echo "[5/5] Installing plugins to user and system audio directories..."
# Find the actual non-root user's home if running under sudo or root
NON_ROOT_USER="${SUDO_USER:-patch}"
NON_ROOT_HOME=$(eval echo "~$NON_ROOT_USER" 2>/dev/null || echo "/home/patch")

mkdir -p "$HOME/.vst3" "$HOME/.lv2"
mkdir -p "$NON_ROOT_HOME/.vst3" "$NON_ROOT_HOME/.lv2"
sudo mkdir -p /usr/local/lib/lv2 /var/modep/lv2 2>/dev/null || true

# Copy LV2
SRC_LV2=""
if [ -d "build-pi/AutoLevelDJ_artefacts/Release/LV2/AutoLevel DJ.lv2" ]; then
    SRC_LV2="build-pi/AutoLevelDJ_artefacts/Release/LV2/AutoLevel DJ.lv2"
elif [ -d "build-pi/AutoLevelDJ_artefacts/LV2/AutoLevel DJ.lv2" ]; then
    SRC_LV2="build-pi/AutoLevelDJ_artefacts/LV2/AutoLevel DJ.lv2"
fi

if [ -n "$SRC_LV2" ]; then
    cp -R "$SRC_LV2" "$HOME/.lv2/"
    cp -R "$SRC_LV2" "$NON_ROOT_HOME/.lv2/" 2>/dev/null || true
    sudo cp -R "$SRC_LV2" /usr/local/lib/lv2/
    sudo cp -R "$SRC_LV2" /var/modep/lv2/ 2>/dev/null || true
    
    # Ensure world-readable permissions so the 'modep' user can read them
    sudo chmod -R 755 "/usr/local/lib/lv2/AutoLevel DJ.lv2"
    sudo chmod -R 755 "/var/modep/lv2/AutoLevel DJ.lv2" 2>/dev/null || true
    sudo chown -R modep:modep "/var/modep/lv2/AutoLevel DJ.lv2" 2>/dev/null || true
fi

# Copy VST3
SRC_VST3=""
if [ -d "build-pi/AutoLevelDJ_artefacts/Release/VST3/AutoLevel DJ.vst3" ]; then
    SRC_VST3="build-pi/AutoLevelDJ_artefacts/Release/VST3/AutoLevel DJ.vst3"
elif [ -d "build-pi/AutoLevelDJ_artefacts/VST3/AutoLevel DJ.vst3" ]; then
    SRC_VST3="build-pi/AutoLevelDJ_artefacts/VST3/AutoLevel DJ.vst3"
fi

if [ -n "$SRC_VST3" ]; then
    cp -R "$SRC_VST3" "$HOME/.vst3/"
    cp -R "$SRC_VST3" "$NON_ROOT_HOME/.vst3/" 2>/dev/null || true
    sudo cp -R "$SRC_VST3" /usr/local/lib/vst3/ 2>/dev/null || true
fi

# If MODEP is running, restart it to index new plugins
if systemctl is-active --quiet modep-mod-ui 2>/dev/null; then
    echo "Restarting MODEP services to index new plugins..."
    sudo systemctl restart modep-mod-ui modep-mod-host || true
fi

echo "=========================================================="
echo "   AutoLevel DJ successfully built and installed!"
echo "   LV2:        $HOME/.lv2/AutoLevel DJ.lv2"
echo "   VST3:       $HOME/.vst3/AutoLevel DJ.vst3"
echo "   Standalone: ${REPO_ROOT}/build-pi/AutoLevelDJ_artefacts/Release/Standalone/AutoLevel DJ"
echo "=========================================================="
