#!/usr/bin/env bash
set -euo pipefail

TAP_IF="${1:-tap0}"
HOST_CIDR="${2:-192.168.76.1/24}"

if ! ip link show "${TAP_IF}" >/dev/null 2>&1; then
    sudo ip tuntap add dev "${TAP_IF}" mode tap user "${USER}"
fi

sudo ip addr replace "${HOST_CIDR}" dev "${TAP_IF}"
sudo ip link set "${TAP_IF}" up

echo "[tap-up] ${TAP_IF} ready at ${HOST_CIDR}"
