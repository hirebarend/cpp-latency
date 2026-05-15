#!/usr/bin/env bash
set -euo pipefail

REPO_URL="https://github.com/hirebarend/cpp-latency/archive/refs/heads/main.zip"
WORK_DIR="${WORK_DIR:-$HOME/cpp-latency}"

SERVER_HOST="${SERVER_HOST:-${1:-}}"
SERVER_PORT="${SERVER_PORT:-9000}"
ITERATIONS="${ITERATIONS:-500}"
WARMUP="${WARMUP:-50}"
INTERVAL_MS="${INTERVAL_MS:-10}"

if [[ -z "${SERVER_HOST}" ]]; then
    echo "usage: SERVER_HOST=<droplet-ip> $0"
    echo "   or: $0 <droplet-ip>"
    echo
    echo "optional env: SERVER_PORT (default 9000), ITERATIONS (500),"
    echo "              WARMUP (50), INTERVAL_MS (10)"
    exit 2
fi

sudo apt update
sudo apt install -y clang cmake build-essential curl unzip

mkdir -p "${WORK_DIR}"
cd "${WORK_DIR}"

curl -fsSL -o source.zip "${REPO_URL}"
rm -rf cpp-latency-main
unzip -q source.zip
cd cpp-latency-main

chmod +x build.sh
./build.sh

echo
echo "Build info:"
echo "  host arch : $(uname -m)"
if [[ -r /proc/device-tree/model ]]; then
    echo "  pi model  : $(tr -d '\0' < /proc/device-tree/model)"
fi
if grep -q '^model name' /proc/cpuinfo; then
    echo "  cpu model : $(grep -m1 '^model name' /proc/cpuinfo | sed 's/.*: //')"
fi
echo "  cpu flags : $(grep -m1 '^Features' /proc/cpuinfo | sed 's/.*: //')"
file ./build/client | sed 's/^/  binary    : /'
echo

echo "Running client against ${SERVER_HOST}:${SERVER_PORT}"
echo "  iterations=${ITERATIONS} warmup=${WARMUP} interval-ms=${INTERVAL_MS}"
echo

exec ./build/client \
    --host "${SERVER_HOST}" \
    --port "${SERVER_PORT}" \
    --iterations "${ITERATIONS}" \
    --warmup "${WARMUP}" \
    --interval-ms "${INTERVAL_MS}"
