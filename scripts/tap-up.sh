#!/usr/bin/env bash
set -euo pipefail

TAP_IF="${1:-tap0}"
HOST_CIDR="${2:-192.168.76.1/24}"
PIDFILE="/tmp/szcz_dnsmasq_${TAP_IF}.pid"
LEASEFILE="/tmp/szcz_dnsmasq_${TAP_IF}.leases"

HOST_IP="${HOST_CIDR%/*}"
NAT_CIDR="$(echo "${HOST_CIDR}" | sed -E 's@([0-9]+\.[0-9]+\.[0-9]+)\.[0-9]+/24@\1.0/24@')"
if [[ "${NAT_CIDR}" == "${HOST_CIDR}" ]]; then
    NAT_CIDR="${HOST_CIDR}"
fi
OUT_IF="${OUT_IF:-$(ip route show default 2>/dev/null | awk '/default/ {print $5; exit}')}"

if [[ -z "${OUT_IF}" ]]; then
    echo "[tap-up] ERROR: could not detect default uplink interface" >&2
    exit 1
fi

if ! ip link show "${TAP_IF}" >/dev/null 2>&1; then
    sudo ip tuntap add dev "${TAP_IF}" mode tap user "${USER}"
fi

sudo ip addr replace "${HOST_CIDR}" dev "${TAP_IF}"
sudo ip link set "${TAP_IF}" up

sudo sysctl -w net.ipv4.ip_forward=1 >/dev/null

sudo iptables -C FORWARD -i "${TAP_IF}" -o "${OUT_IF}" -j ACCEPT 2>/dev/null || \
    sudo iptables -A FORWARD -i "${TAP_IF}" -o "${OUT_IF}" -j ACCEPT
sudo iptables -C FORWARD -i "${OUT_IF}" -o "${TAP_IF}" -m state --state ESTABLISHED,RELATED -j ACCEPT 2>/dev/null || \
    sudo iptables -A FORWARD -i "${OUT_IF}" -o "${TAP_IF}" -m state --state ESTABLISHED,RELATED -j ACCEPT
sudo iptables -t nat -C POSTROUTING -s "${NAT_CIDR}" -o "${OUT_IF}" -j MASQUERADE 2>/dev/null || \
    sudo iptables -t nat -A POSTROUTING -s "${NAT_CIDR}" -o "${OUT_IF}" -j MASQUERADE

if [[ -f "${PIDFILE}" ]] && sudo kill -0 "$(cat "${PIDFILE}")" 2>/dev/null; then
    :
else
    sudo rm -f "${PIDFILE}" "${LEASEFILE}"
    if ! sudo dnsmasq \
        --interface="${TAP_IF}" \
        --bind-interfaces \
        --listen-address="${HOST_IP}" \
        --dhcp-range=192.168.76.50,192.168.76.150,255.255.255.0,12h \
        --dhcp-option=option:router,"${HOST_IP}" \
        --dhcp-option=option:dns-server,"${HOST_IP}" \
        --pid-file="${PIDFILE}" \
        --dhcp-leasefile="${LEASEFILE}" \
        --except-interface=lo; then
        echo "[tap-up] WARNING: dnsmasq failed to start; DHCP/DNS on tap may be unavailable"
    fi
fi

echo "[tap-up] ${TAP_IF} ready at ${HOST_CIDR} via ${OUT_IF} (NAT + DHCP/DNS)"
