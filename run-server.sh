#!/usr/bin/env bash
set -euo pipefail

REPO_URL="https://github.com/hirebarend/cpp-latency/archive/refs/heads/main.zip"
WORK_DIR="${WORK_DIR:-$HOME/cpp-latency}"
PORT="${PORT:-9000}"

sudo apt update
sudo apt install -y clang cmake build-essential curl unzip ufw

if command -v ufw >/dev/null 2>&1; then
    echo "Opening TCP port ${PORT} via ufw..."
    sudo ufw allow "${PORT}/tcp" || true
    if ! sudo ufw status | grep -q "Status: active"; then
        echo "Note: ufw is inactive, so the rule is staged but not enforced."
        echo "      If you later run 'sudo ufw enable', first run 'sudo ufw allow OpenSSH'"
        echo "      or 'sudo ufw allow 22/tcp' to avoid locking yourself out."
    fi
else
    echo "ufw not available; skipping local firewall step."
fi

echo "Reminder: if the DigitalOcean Cloud Firewall is attached to this droplet,"
echo "          add an inbound TCP rule for port ${PORT} in the control panel."
echo

mkdir -p "${WORK_DIR}"
cd "${WORK_DIR}"

curl -fsSL -o source.zip "${REPO_URL}"
rm -rf cpp-latency-main
unzip -q source.zip
cd cpp-latency-main

chmod +x build.sh
./build.sh

echo
echo "Starting server on port ${PORT} (Ctrl-C to stop)."
echo "To keep it running after logout, use one of:"
echo "  nohup ./build/server ${PORT} > server.log 2>&1 &"
echo "  tmux new -s latency './build/server ${PORT}'"
echo

exec ./build/server "${PORT}"
