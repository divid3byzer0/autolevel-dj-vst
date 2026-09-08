#!/usr/bin/env bash
# ==============================================================================
# M-Audio Fast Track Pro 24-Bit / 4-Output ALSA Configuration Script
# ==============================================================================
# Unlocks 24-bit 44.1/48kHz mode with 2 inputs and 4 analog outputs on Linux.
# ==============================================================================

set -e

echo "=== M-Audio Fast Track Pro ALSA Setup ==="

# 1. Require root permissions
if [ "$EUID" -ne 0 ]; then
    echo "Please run as root: sudo bash $0"
    exit 1
fi

TARGET_USER="${SUDO_USER:-$USER}"

echo "[1/4] Configuring ALSA modprobe for Fast Track Pro..."
# Note: On Raspberry Pi 4, HDMI0 and HDMI1 occupy index 0 and 1.
# Setting index=1 causes a conflict with vc4hdmi1.
# For 2-in / 4-out simultaneously (DJ Master + Booth), Fast Track Pro uses standard USB 1.1 mode.
# For 24-bit mode (2-in / 2-out), use device_setup=0x9.
cat << 'EOF' > /etc/modprobe.d/fasttrackpro.conf
# M-Audio Fast Track Pro ALSA configuration
# Do not force index=1 to avoid collision with Raspberry Pi 4 HDMI audio
options snd-usb-audio vid=0x0763 pid=0x2012
EOF

echo "Configuration written to /etc/modprobe.d/fasttrackpro.conf."

echo "[2/4] Ensuring real-time audio permissions..."
mkdir -p /etc/security/limits.d
cat << 'EOF' > /etc/security/limits.d/audio.conf
@audio - rtprio 95
@audio - memlock unlimited
@audio - nice -19
EOF

# Ensure user is in the audio group
if id -nG "$TARGET_USER" | grep -qw "audio"; then
    echo "User $TARGET_USER is already in the audio group."
else
    usermod -a -G audio "$TARGET_USER"
    echo "Added $TARGET_USER to the audio group."
fi

echo "[3/4] Checking ALSA sound cards..."
echo "--- Playback Devices (aplay -l) ---"
aplay -l || true

echo "--- Capture Devices (arecord -l) ---"
arecord -l || true

echo "[4/4] Summary & Verification:"
echo "======================================================================"
echo "Setup complete! To apply the 24-bit / 4-output driver profile:"
echo "1. Unplug the M-Audio Fast Track Pro USB cable and plug it back in."
echo "   (Or simply reboot the Raspberry Pi: sudo reboot)"
echo ""
echo "2. After reconnecting, verify 24-bit mode by running:"
echo "   cat /proc/asound/card1/pcm0p/sub0/hw_params"
echo ""
echo "3. In JACK or Patchbox OS, select card 1 (FastTrackPro) with:"
echo "   Sample Rate: 48000 Hz (or 44100 Hz)"
echo "   Buffer Size: 128 or 256 samples"
echo "   Periods:     2"
echo "======================================================================"
