#!/usr/bin/env bash
# ==============================================================================
# AutoLevel DJ: Standalone Headless Systemd Service Setup
# ==============================================================================
# Configures AutoLevel DJ to run automatically at system boot with JACK
# for instant-on hardware operation (no browser or manual action needed).
# ==============================================================================

set -e

if [ "$EUID" -ne 0 ]; then
    echo "Please run as root: sudo bash $0"
    exit 1
fi

TARGET_USER="${SUDO_USER:-$USER}"
HOME_DIR=$(eval echo "~$TARGET_USER")

# Check for standalone binary
STANDALONE_BIN="${HOME_DIR}/.vst3/AutoLevel DJ"
if [ ! -f "$STANDALONE_BIN" ]; then
    # Look in build directory
    STANDALONE_BIN=$(find "${HOME_DIR}" -name "AutoLevel DJ" -type f -perm /111 2>/dev/null | grep -v "\.vst3" | head -n 1 || true)
fi

echo "=== AutoLevel DJ Headless Appliance Service ==="
echo "Target User: $TARGET_USER"

# Create systemd user service directory
SERVICE_DIR="/etc/systemd/system"
cat << EOF > "${SERVICE_DIR}/autolevel-dj.service"
[Unit]
Description=AutoLevel DJ Headless Standalone Audio Processor
After=sound.target jack.service patchbox-init.service
Wants=jack.service

[Service]
Type=simple
User=${TARGET_USER}
Environment="DISPLAY=:0"
Environment="JACK_NO_AUDIO_RESERVATION=1"
ExecStartPre=/bin/sleep 2
ExecStart=${HOME_DIR}/.vst3/AutoLevel\ DJ
Restart=on-failure
RestartSec=3

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
echo "Service file created at /etc/systemd/system/autolevel-dj.service"
echo ""
echo "To enable instant-on start at boot:"
echo "  sudo systemctl enable autolevel-dj.service"
echo "  sudo systemctl start autolevel-dj.service"
echo ""
echo "To check status:"
echo "  sudo systemctl status autolevel-dj.service"
