#!/usr/bin/env bash
# scripts/cross-compile/build.sh
# Cross-compile seeway_interface_daemon for arm-linux-gnueabihf (ARMv7-A).
#
# Usage:
#   ./scripts/cross-compile/build.sh [options]
#
# Options:
#   --build-dir DIR      Output directory (default: build_armhf)
#   --sysroot PATH       ARM sysroot path (optional)
#   --jobs N             Parallel jobs (default: nproc)
#   --clean              Remove build dir before configuring
#   --verify             Run verify_arm_binary.sh after build
#   -h, --help           Show help

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DAEMON_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
TOOLCHAIN="${DAEMON_DIR}/cmake/arm-linux-gnueabihf.cmake"

# --------------------------------------------------------------------------
# Defaults
# --------------------------------------------------------------------------
BUILD_DIR="${DAEMON_DIR}/build_armhf"
SYSROOT=""
JOBS=$(nproc 2>/dev/null || echo 4)
CLEAN=0
VERIFY=0

# --------------------------------------------------------------------------
# Parse arguments
# --------------------------------------------------------------------------
while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --sysroot)   SYSROOT="$2";   shift 2 ;;
        --jobs)      JOBS="$2";      shift 2 ;;
        --clean)     CLEAN=1;        shift ;;
        --verify)    VERIFY=1;       shift ;;
        -h|--help)
            head -20 "$0" | grep '^#' | sed 's/^# \{0,2\}//'
            exit 0
            ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

# --------------------------------------------------------------------------
# Checks
# --------------------------------------------------------------------------
if [[ ! -f "${TOOLCHAIN}" ]]; then
    echo "Error: toolchain file not found: ${TOOLCHAIN}"
    exit 1
fi

if ! command -v arm-linux-gnueabihf-gcc &>/dev/null && \
   ! command -v arm-linux-gnueabihf-gcc-9 &>/dev/null; then
    echo "Error: ARM cross-compiler not found."
    echo "Install with: sudo apt-get install gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf"
    exit 1
fi

# --------------------------------------------------------------------------
# Configure
# --------------------------------------------------------------------------
echo "========================================"
echo " Seeway Interface Daemon – ARM Build"
echo "========================================"
echo "  Daemon dir : ${DAEMON_DIR}"
echo "  Build dir  : ${BUILD_DIR}"
echo "  Toolchain  : ${TOOLCHAIN}"
echo "  Sysroot    : ${SYSROOT:-<none>}"
echo "  Jobs       : ${JOBS}"

if [[ "${CLEAN}" -eq 1 ]]; then
    echo "  Cleaning ${BUILD_DIR}..."
    rm -rf "${BUILD_DIR}"
fi

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

# --------------------------------------------------------------------------
# CMake configure
# --------------------------------------------------------------------------
SYSROOT_ARG=""
if [[ -n "${SYSROOT}" ]]; then
    SYSROOT_ARG="-DARMHF_SYSROOT=${SYSROOT}"
fi

cmake "${DAEMON_DIR}" \
    -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN}" \
    ${SYSROOT_ARG} \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_VERBOSE_MAKEFILE=OFF

# --------------------------------------------------------------------------
# Build
# --------------------------------------------------------------------------
echo ""
echo "--- Building ---"
make -j"${JOBS}"

BINARY="${BUILD_DIR}/seeway_interface_daemon"
if [[ ! -f "${BINARY}" ]]; then
    echo "Error: expected binary not found: ${BINARY}"
    exit 1
fi

echo ""
echo "Build successful: ${BINARY}"
ls -lh "${BINARY}"

# --------------------------------------------------------------------------
# Verify
# --------------------------------------------------------------------------
if [[ "${VERIFY}" -eq 1 ]]; then
    echo ""
    "${SCRIPT_DIR}/verify_arm_binary.sh" "${BINARY}"
fi

echo ""
echo "Cross-compile complete."
