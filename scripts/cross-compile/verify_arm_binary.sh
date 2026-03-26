#!/usr/bin/env bash
# scripts/cross-compile/verify_arm_binary.sh
# ---------------------------------------------------------------------------
# Verify that a cross-compiled ELF binary targets the correct architecture.
#
# Usage:
#   bash verify_arm_binary.sh <binary> [arm|aarch64]
# ---------------------------------------------------------------------------
set -euo pipefail

BINARY="${1:?Usage: $0 <binary> [arm|aarch64]}"
TARGET="${2:-arm}"

echo "=== Binary Verification: $(basename "${BINARY}") ==="

if [ ! -f "${BINARY}" ]; then
    echo "ERROR: File not found: ${BINARY}"
    exit 1
fi

# ---------------------------------------------------------------------------
# 1. ELF format check (requires 'file')
# ---------------------------------------------------------------------------
if command -v file &>/dev/null; then
    FILE_OUTPUT="$(file "${BINARY}")"
    echo "file: ${FILE_OUTPUT}"

    if ! echo "${FILE_OUTPUT}" | grep -q "ELF"; then
        echo "ERROR: Binary is not an ELF executable"
        exit 1
    fi
    echo "  ✓ ELF format"

    case "$TARGET" in
        arm|armhf)
            if echo "${FILE_OUTPUT}" | grep -qiE "ARM|32-bit"; then
                echo "  ✓ ARM 32-bit"
            else
                echo "  WARN: Expected ARM 32-bit, got: ${FILE_OUTPUT}"
            fi
            if echo "${FILE_OUTPUT}" | grep -qi "hard-float\|HF"; then
                echo "  ✓ Hard-float ABI"
            else
                echo "  INFO: Hard-float ABI marker not found (may still be correct)"
            fi
            ;;
        aarch64|arm64)
            if echo "${FILE_OUTPUT}" | grep -qiE "AArch64|ARM aarch64|64-bit"; then
                echo "  ✓ AArch64 64-bit"
            else
                echo "  WARN: Expected AArch64, got: ${FILE_OUTPUT}"
            fi
            ;;
    esac
else
    echo "  SKIP: 'file' command not available"
fi

# ---------------------------------------------------------------------------
# 2. Machine type via readelf
# ---------------------------------------------------------------------------
if command -v readelf &>/dev/null; then
    READELF_OUTPUT="$(readelf -h "${BINARY}" 2>/dev/null || true)"

    case "$TARGET" in
        arm|armhf)
            ARCH_EXPECTED="ARM"
            ;;
        aarch64|arm64)
            ARCH_EXPECTED="AArch64"
            ;;
    esac

    if echo "${READELF_OUTPUT}" | grep -q "Machine:.*${ARCH_EXPECTED}"; then
        echo "  ✓ Machine type: ${ARCH_EXPECTED}"
    else
        MACHINE_LINE="$(echo "${READELF_OUTPUT}" | grep "Machine:" || echo "  (not found)")"
        echo "  WARN: ${MACHINE_LINE}"
    fi
elif command -v arm-linux-gnueabihf-readelf &>/dev/null; then
    arm-linux-gnueabihf-readelf -h "${BINARY}" | grep "Machine:" || true
fi

# ---------------------------------------------------------------------------
# 3. Binary size
# ---------------------------------------------------------------------------
SIZE_BYTES="$(stat -c%s "${BINARY}" 2>/dev/null || stat -f%z "${BINARY}" 2>/dev/null || echo 0)"
SIZE_KB=$(( SIZE_BYTES / 1024 ))
echo "  Binary size: ${SIZE_KB} kB"

if [ "${SIZE_KB}" -gt 0 ] && [ "${SIZE_KB}" -lt 100000 ]; then
    echo "  ✓ Size reasonable (< 100 MB)"
else
    echo "  WARN: Unusual binary size: ${SIZE_KB} kB"
fi

# ---------------------------------------------------------------------------
# 4. Symbol table (optional)
# ---------------------------------------------------------------------------
if command -v nm &>/dev/null || command -v arm-linux-gnueabihf-nm &>/dev/null; then
    NM_CMD="${NM:-nm}"
    if ! command -v nm &>/dev/null && command -v arm-linux-gnueabihf-nm &>/dev/null; then
        NM_CMD="arm-linux-gnueabihf-nm"
    fi
    SYMBOLS="$("${NM_CMD}" "${BINARY}" 2>/dev/null | wc -l || echo 0)"
    echo "  Symbol count: ${SYMBOLS}"
fi

# ---------------------------------------------------------------------------
# 5. Dynamic dependencies
# ---------------------------------------------------------------------------
if command -v objdump &>/dev/null; then
    DEPS="$(objdump -p "${BINARY}" 2>/dev/null | grep "NEEDED" | awk '{print $2}' | tr '\n' ' ' || true)"
    if [ -n "${DEPS}" ]; then
        echo "  Dynamic deps: ${DEPS}"
    else
        echo "  Statically linked (no NEEDED entries)"
    fi
fi

echo ""
echo "Verification complete for: ${BINARY}"
