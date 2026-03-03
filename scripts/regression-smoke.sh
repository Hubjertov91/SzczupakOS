#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

arch="${ARCH:-x86_64}"
platform="${PLATFORM:-pc}"
qemu_bin="${QEMU_BIN:-qemu-system-x86_64}"
timeout_s="${REGRESSION_TIMEOUT_SEC:-35}"
log_file="${REGRESSION_LOG:-build/regression-smoke.log}"

if [[ "$arch" != "x86_64" ]]; then
    echo "[regression] unsupported ARCH=$arch (currently only x86_64)"
    exit 1
fi

if ! command -v "$qemu_bin" >/dev/null 2>&1; then
    echo "[regression] missing emulator: $qemu_bin"
    exit 1
fi

if ! command -v timeout >/dev/null 2>&1; then
    echo "[regression] missing required tool: timeout"
    exit 1
fi

echo "[regression] building images for ARCH=$arch PLATFORM=$platform"
make ARCH="$arch" PLATFORM="$platform" "build/os.iso" "disk.img"

mkdir -p "$(dirname "$log_file")"
rm -f "$log_file"

echo "[regression] booting QEMU and running shell regression commands"
set +e
{
    sleep 2
    printf "sysinfo\n"
    sleep 1
    printf "ls\n"
    sleep 1
    printf "touch /REGTEST.TXT\n"
    sleep 1
    printf "ls\n"
    sleep 1
    printf "netstat\n"
    sleep 1
    printf "echo REGRESSION_DONE\n"
    sleep 1
} | timeout "$timeout_s" "$qemu_bin" \
    -cdrom build/os.iso \
    -drive file=disk.img,format=raw,if=ide \
    -boot order=d \
    -m 512M \
    -nographic \
    -serial stdio \
    -monitor none \
    -nic user,model=rtl8139 >"$log_file" 2>&1
qemu_status=$?
set -e

if [[ $qemu_status -ne 0 && $qemu_status -ne 124 ]]; then
    echo "[regression] QEMU exited with unexpected code: $qemu_status"
    exit "$qemu_status"
fi

require_marker() {
    local marker="$1"
    if ! grep -aFq "$marker" "$log_file"; then
        echo "[regression] missing marker: $marker"
        exit 1
    fi
}

reject_marker() {
    local marker="$1"
    if grep -aFq "$marker" "$log_file"; then
        echo "[regression] panic/fault marker found: $marker"
        exit 1
    fi
}

require_marker "[KERNEL] Starting SzczupakOS..."
require_marker "Running Processes:"
require_marker "REGTEST.TXT"
require_marker "=== Net Stats ==="
require_marker "REGRESSION_DONE"

reject_marker "KERNEL PANIC"
reject_marker "[SYSTEM HALTED]"
reject_marker "[PANIC] CPU halted."

echo "[regression] PASS"
