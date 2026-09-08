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

# 2. Configure CMake with ARM Cortex-A72 optimization
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

echo "[2/5] Configuring CMake (Release build with Cortex-A72 optimizations)..."
cd "${REPO_ROOT}"

# Remove stale build folder if previous configuration failed
if [ -d "build-pi" ] && [ ! -f "build-pi/build.ninja" ]; then
    echo "Cleaning incomplete build directory..."
    rm -rf build-pi
fi

# Enable Cortex-A72 tuning flags for Raspberry Pi 4
export CXXFLAGS="-O3 -mcpu=cortex-a72 -mtune=cortex-a72"

cmake -B build-pi -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_STANDARD=20

# 3. Compile VST3, LV2, Standalone, and unit test suite
echo "[3/5] Building AutoLevel DJ (VST3, LV2, Standalone)..."
cmake --build build-pi --config Release -j$(nproc)

# 4. Run DSP unit tests
echo "[4/5] Running DSP unit tests to verify bit-perfect accuracy..."
./build-pi/dsp_test

# 5. Install plugins
echo "[5/5] Installing plugins to user and system audio directories..."
mkdir -p "$HOME/.vst3"
mkdir -p "$HOME/.lv2"

cp -R "build-pi/AutoLevelDJ_artefacts/Release/VST3/AutoLevel DJ.vst3" "$HOME/.vst3/" || \
cp -R "build-pi/AutoLevelDJ_artefacts/VST3/AutoLevel DJ.vst3" "$HOME/.vst3/"

cp -R "build-pi/AutoLevelDJ_artefacts/Release/LV2/AutoLevel DJ.lv2" "$HOME/.lv2/" || \
cp -R "build-pi/AutoLevelDJ_artefacts/LV2/AutoLevel DJ.lv2" "$HOME/.lv2/"

# If MODEP is installed, also copy into MODEP LV2 directory
if [ -d "/var/modep/lv2" ]; then
    echo "MODEP detected. Installing to /var/modep/lv2..."
    sudo cp -R "$HOME/.lv2/AutoLevel DJ.lv2" /var/modep/lv2/
    sudo chown -R modep:modep "/var/modep/lv2/AutoLevel DJ.lv2" 2>/dev/null || true
fi

echo "=========================================================="
echo "   AutoLevel DJ successfully built and installed!"
echo "   LV2:        $HOME/.lv2/AutoLevel DJ.lv2"
echo "   VST3:       $HOME/.vst3/AutoLevel DJ.vst3"
echo "   Standalone: ${REPO_ROOT}/build-pi/AutoLevelDJ_artefacts/Release/Standalone/AutoLevel DJ"
echo "=========================================================="
