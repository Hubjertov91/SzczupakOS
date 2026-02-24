#!/usr/bin/env bash
set -euo pipefail

TAP_IF="${1:-tap0}"
HOST_CIDR="${2:-192.168.76.1/24}"
PIDFILE="/tmp/szcz_dnsmasq_${TAP_IF}.pid"

NAT_CIDR="$(echo "${HOST_CIDR}" | sed -E 's@([0-9]+\.[0-9]+\.[0-9]+)\.[0-9]+/24@\1.0/24@')"
if [[ "${NAT_CIDR}" == "${HOST_CIDR}" ]]; then
    NAT_CIDR="${HOST_CIDR}"
fi
OUT_IF="${OUT_IF:-$(ip route show default 2>/dev/null | awk '/default/ {print $5; exit}')}"

if [[ -f "${PIDFILE}" ]]; then
    sudo kill "$(cat "${PIDFILE}")" 2>/dev/null || true
    sudo rm -f "${PIDFILE}"
fi

if [[ -n "${OUT_IF}" ]]; then
    sudo iptables -D FORWARD -i "${TAP_IF}" -o "${OUT_IF}" -j ACCEPT 2>/dev/null || true
    sudo iptables -D FORWARD -i "${OUT_IF}" -o "${TAP_IF}" -m state --state ESTABLISHED,RELATED -j ACCEPT 2>/dev/null || true
    sudo iptables -t nat -D POSTROUTING -s "${NAT_CIDR}" -o "${OUT_IF}" -j MASQUERADE 2>/dev/null || true
fi

if ip link show "${TAP_IF}" >/dev/null 2>&1; then
    sudo ip link set "${TAP_IF}" down || true
    sudo ip tuntap del dev "${TAP_IF}" mode tap || true
fi

echo "[tap-down] ${TAP_IF} removed"
