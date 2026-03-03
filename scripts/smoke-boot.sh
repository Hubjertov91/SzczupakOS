#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

arch="${ARCH:-x86_64}"
platform="${PLATFORM:-pc}"
qemu_bin="${QEMU_BIN:-qemu-system-x86_64}"
timeout_s="${SMOKE_TIMEOUT_SEC:-20}"
log_file="${SMOKE_LOG:-build/smoke-boot.log}"

if [[ "$arch" != "x86_64" ]]; then
    echo "[smoke] unsupported ARCH=$arch (currently only x86_64)"
    exit 1
fi

if ! command -v "$qemu_bin" >/dev/null 2>&1; then
    echo "[smoke] missing emulator: $qemu_bin"
    exit 1
fi

if ! command -v timeout >/dev/null 2>&1; then
    echo "[smoke] missing required tool: timeout"
    exit 1
fi

echo "[smoke] building images for ARCH=$arch PLATFORM=$platform"
make ARCH="$arch" PLATFORM="$platform" "build/os.iso" "disk.img"

mkdir -p "$(dirname "$log_file")"
rm -f "$log_file"

echo "[smoke] booting QEMU for up to ${timeout_s}s"
set +e
timeout "$timeout_s" "$qemu_bin" \
  -cdrom build/os.iso \
  -drive file=disk.img,format=raw,if=ide \
  -boot order=d \
  -m 512M \
  -nographic \
  -serial stdio \
  -monitor none \
  -nic none >"$log_file" 2>&1
qemu_status=$?
set -e

if [[ $qemu_status -ne 0 && $qemu_status -ne 124 ]]; then
    echo "[smoke] QEMU exited with unexpected code: $qemu_status"
    exit $qemu_status
fi

if ! grep -aFq "[KERNEL] Starting SzczupakOS..." "$log_file"; then
    echo "[smoke] missing boot marker in log: [KERNEL] Starting SzczupakOS..."
    exit 1
fi

if grep -aFq "[KERNEL] System ready, entering idle loop" "$log_file"; then
    echo "[smoke] reached kernel ready marker"
elif grep -aFq "[KERNEL] Using shell from" "$log_file"; then
    echo "[smoke] reached userspace handoff marker"
else
    echo "[smoke] missing progress marker (ready or userspace handoff)"
    exit 1
fi

if grep -aFq "KERNEL PANIC" "$log_file" || \
   grep -aFq "[SYSTEM HALTED]" "$log_file" || \
   grep -aFq "[PANIC] CPU halted." "$log_file"; then
    echo "[smoke] panic marker found in boot log"
    exit 1
fi

echo "[smoke] PASS"
