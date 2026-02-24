#!/usr/bin/env bash
set -euo pipefail

TAP_IF="${1:-tap0}"

if ip link show "${TAP_IF}" >/dev/null 2>&1; then
    sudo ip link set "${TAP_IF}" down || true
    sudo ip tuntap del dev "${TAP_IF}" mode tap || true
fi

echo "[tap-down] ${TAP_IF} removed"
