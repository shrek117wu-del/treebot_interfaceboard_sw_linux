#!/usr/bin/env bash
# scripts/cross-compile/build.sh
# ---------------------------------------------------------------------------
# Automated cross-compilation script for the T113i daemon (ARM 32-bit).
#
# Usage:
#   bash scripts/cross-compile/build.sh [--toolchain arm|aarch64]
#                                       [--jobs N]
#                                       [--sysroot PATH]
#
# Requirements:
#   apt-get install gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf
# ---------------------------------------------------------------------------
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
DAEMON_DIR="${REPO_ROOT}/t113i_daemon"

# Defaults
TOOLCHAIN="arm"
JOBS="$(nproc)"
SYSROOT=""

for arg in "$@"; do
    case "$arg" in
        --toolchain=*) TOOLCHAIN="${arg#*=}" ;;
        --jobs=*)      JOBS="${arg#*=}" ;;
        --sysroot=*)   SYSROOT="${arg#*=}" ;;
        --toolchain)   shift; TOOLCHAIN="$1" ;;
        --jobs)        shift; JOBS="$1" ;;
        --sysroot)     shift; SYSROOT="$1" ;;
    esac
done

# Select toolchain file
case "$TOOLCHAIN" in
    arm|armhf)
        TOOLCHAIN_FILE="${REPO_ROOT}/cmake/arm-linux-gnueabihf.cmake"
        BUILD_DIR="${REPO_ROOT}/build_armhf"
        OUTPUT_BINARY="seeway_interface_daemon"
        ;;
    aarch64|arm64)
        TOOLCHAIN_FILE="${REPO_ROOT}/cmake/aarch64-linux-gnu.cmake"
        BUILD_DIR="${REPO_ROOT}/build_aarch64"
        OUTPUT_BINARY="seeway_interface_daemon"
        ;;
    *)
        echo "ERROR: Unknown toolchain '${TOOLCHAIN}'. Use arm or aarch64."
        exit 1
        ;;
esac

echo "============================================================"
echo " T113i Daemon – Cross-Compilation (${TOOLCHAIN})"
echo "============================================================"
echo " Toolchain file : ${TOOLCHAIN_FILE}"
echo " Build directory: ${BUILD_DIR}"
echo " Parallel jobs  : ${JOBS}"
[ -n "${SYSROOT}" ] && echo " Sysroot        : ${SYSROOT}"
echo ""

# ---------------------------------------------------------------------------
# Verify toolchain is installed
# ---------------------------------------------------------------------------
if [ "$TOOLCHAIN" = "arm" ] || [ "$TOOLCHAIN" = "armhf" ]; then
    COMPILER="arm-linux-gnueabihf-gcc"
else
    COMPILER="aarch64-linux-gnu-gcc"
fi

if ! command -v "$COMPILER" &>/dev/null; then
    echo "ERROR: Cross-compiler '${COMPILER}' not found."
    echo "Install with:"
    if [ "$TOOLCHAIN" = "arm" ] || [ "$TOOLCHAIN" = "armhf" ]; then
        echo "  sudo apt-get install gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf"
    else
        echo "  sudo apt-get install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu"
    fi
    exit 1
fi

echo ">>> Using compiler: $("${COMPILER}" --version | head -1)"
echo ""

# ---------------------------------------------------------------------------
# Configure
# ---------------------------------------------------------------------------
echo ">>> Configuring..."
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

CMAKE_OPTS=(
    "${DAEMON_DIR}"
    "-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE}"
    "-DCMAKE_BUILD_TYPE=Release"
    "-DCMAKE_VERBOSE_MAKEFILE=OFF"
)

if [ -n "${SYSROOT}" ]; then
    CMAKE_OPTS+=("-DARM_SYSROOT=${SYSROOT}")
fi

cmake "${CMAKE_OPTS[@]}"
echo ""

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------
echo ">>> Building (${JOBS} parallel jobs)..."
make -j"${JOBS}"
echo ""

# ---------------------------------------------------------------------------
# Verify binary
# ---------------------------------------------------------------------------
echo ">>> Verifying output binary..."
BINARY="${BUILD_DIR}/${OUTPUT_BINARY}"

if [ ! -f "${BINARY}" ]; then
    echo "ERROR: Binary not found: ${BINARY}"
    exit 1
fi

bash "${SCRIPT_DIR}/verify_arm_binary.sh" "${BINARY}" "${TOOLCHAIN}"

echo ""
echo "============================================================"
echo " Build SUCCESS: ${BINARY}"
echo "============================================================"
