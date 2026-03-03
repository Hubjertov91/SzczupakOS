#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

profiles_default="x86_64:pc"
profiles="${MATRIX_PROFILES:-$profiles_default}"

echo "[matrix] profiles: $profiles"

status=0
for profile in $profiles; do
    arch="${profile%%:*}"
    platform="${profile##*:}"

    if [[ -z "$arch" || -z "$platform" || "$arch" == "$platform" ]]; then
        echo "[matrix] invalid profile '$profile' (expected ARCH:PLATFORM)"
        status=1
        continue
    fi

    echo "[matrix] === ARCH=$arch PLATFORM=$platform ==="
    if ! make k ARCH="$arch" PLATFORM="$platform"; then
        echo "[matrix] kernel build FAILED for $profile"
        status=1
        continue
    fi

    if ! make u ARCH="$arch"; then
        echo "[matrix] userland build FAILED for $profile"
        status=1
        continue
    fi

    echo "[matrix] OK: $profile"
done

if [[ $status -ne 0 ]]; then
    echo "[matrix] one or more profiles failed"
fi

exit $status
