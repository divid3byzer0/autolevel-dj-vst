#!/usr/bin/env bash
# ==============================================================================
# AutoLevel DJ: Standalone Hardware Web Service Setup
# ==============================================================================
# Installs AutoLevel DJ Web Server as an autonomous systemd service on port 8080.
# Accessible from any phone, iPad, or laptop at http://patchbox.local:8080
# ==============================================================================

set -e

if [ "$EUID" -ne 0 ]; then
    echo "Please run as root: sudo bash $0"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

TARGET_USER="${SUDO_USER:-patch}"
BIN_SOURCE="${REPO_ROOT}/build-pi/autolevel_web"

if [ ! -f "$BIN_SOURCE" ]; then
    # Look in other possible build locations
    BIN_SOURCE=$(find "${REPO_ROOT}" -name "autolevel_web" -type f -perm /111 2>/dev/null | head -n 1 || true)
fi

if [ -z "$BIN_SOURCE" ] || [ ! -f "$BIN_SOURCE" ]; then
    echo "Error: autolevel_web executable not found. Please compile first using ./scripts/build_pi.sh"
    exit 1
fi

echo "=== AutoLevel DJ Web Service Setup ==="
echo "Installing binary and web assets..."

# 1. Install binary
cp "$BIN_SOURCE" /usr/local/bin/autolevel-web
chmod 755 /usr/local/bin/autolevel-web

# 2. Install web UI assets
mkdir -p /usr/local/share/autolevel-dj/web
cp -R "${REPO_ROOT}/web/"* /usr/local/share/autolevel-dj/web/
chmod -R 755 /usr/local/share/autolevel-dj/web

# 3. Create systemd service
SERVICE_FILE="/etc/systemd/system/autolevel-web.service"
echo "Creating systemd service at $SERVICE_FILE..."

cat << EOF > "$SERVICE_FILE"
[Unit]
Description=AutoLevel DJ Hardware Web Server
After=sound.target jack.service
Wants=jack.service

[Service]
Type=simple
User=${TARGET_USER}
Group=audio
Environment="JACK_NO_AUDIO_RESERVATION=1"
ExecStart=/usr/local/bin/autolevel-web --port 8080 --web /usr/local/share/autolevel-dj/web
Restart=on-failure
RestartSec=3

[Install]
WantedBy=multi-user.target
EOF

# 4. Reload and enable service
systemctl daemon-reload
systemctl enable autolevel-web.service
systemctl restart autolevel-web.service

echo "=========================================================="
echo "   AutoLevel DJ Web Service Successfully Installed!"
echo "   Status: Active & Enabled on boot"
echo "   Port:   8080"
echo "   URL:    http://patchbox.local:8080"
echo "=========================================================="
