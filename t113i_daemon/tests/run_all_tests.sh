#!/usr/bin/env bash
# tests/run_all_tests.sh
# Run all daemon unit + integration tests with optional coverage report.
#
# Usage:
#   ./tests/run_all_tests.sh [options]
#
# Options:
#   --build-dir DIR    CMake build directory (default: build_test)
#   --no-coverage      Skip lcov coverage generation
#   --integration      Also run integration tests (default: unit only)
#   --jobs N           Parallel build jobs (default: $(nproc))
#   -h, --help         Show this help

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DAEMON_DIR="$(dirname "$SCRIPT_DIR")"

# --------------------------------------------------------------------------
# Defaults
# --------------------------------------------------------------------------
BUILD_DIR="${DAEMON_DIR}/build_test"
COVERAGE=1
RUN_INTEGRATION=0
JOBS=$(nproc 2>/dev/null || echo 4)

# --------------------------------------------------------------------------
# Parse arguments
# --------------------------------------------------------------------------
while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --no-coverage) COVERAGE=0; shift ;;
        --integration) RUN_INTEGRATION=1; shift ;;
        --jobs) JOBS="$2"; shift 2 ;;
        -h|--help)
            sed -n '2,20p' "$0"
            exit 0
            ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

# --------------------------------------------------------------------------
# Configure
# --------------------------------------------------------------------------
echo "========================================"
echo " Seeway Interface Daemon – Test Runner"
echo "========================================"
echo "  Daemon dir : ${DAEMON_DIR}"
echo "  Build dir  : ${BUILD_DIR}"
echo "  Jobs       : ${JOBS}"
echo "  Coverage   : ${COVERAGE}"
echo "  Integration: ${RUN_INTEGRATION}"
echo ""

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

# Enable coverage flags if requested
COVERAGE_FLAGS=""
if [[ "${COVERAGE}" -eq 1 ]]; then
    COVERAGE_FLAGS="-DCMAKE_CXX_FLAGS=--coverage -DCMAKE_EXE_LINKER_FLAGS=--coverage"
fi

# --------------------------------------------------------------------------
# CMake configure
# --------------------------------------------------------------------------
cmake "${DAEMON_DIR}" \
    -DENABLE_TESTING=ON \
    -DCMAKE_BUILD_TYPE=Debug \
    ${COVERAGE_FLAGS}

# --------------------------------------------------------------------------
# Build
# --------------------------------------------------------------------------
make -j"${JOBS}" 2>&1 | tee build.log
echo ""
echo "Build complete."

# --------------------------------------------------------------------------
# Run unit tests
# --------------------------------------------------------------------------
echo ""
echo "--- Running unit tests ---"
ctest --test-dir . \
      --label-regex "^(test_logger|test_connection|test_task|test_module|test_config|test_proto|test_utils)" \
      --output-on-failure \
      --parallel "${JOBS}" \
      -R "test_" \
      || true   # collect result below

UNIT_RESULT=$?

# --------------------------------------------------------------------------
# Run integration tests (optional)
# --------------------------------------------------------------------------
INT_RESULT=0
if [[ "${RUN_INTEGRATION}" -eq 1 ]]; then
    echo ""
    echo "--- Running integration tests ---"
    ctest --test-dir . \
          --label-regex "integration" \
          --output-on-failure \
          --parallel 1 \
          -R "integration_" \
          || INT_RESULT=$?
fi

# --------------------------------------------------------------------------
# Coverage report
# --------------------------------------------------------------------------
if [[ "${COVERAGE}" -eq 1 ]] && command -v lcov &>/dev/null; then
    echo ""
    echo "--- Generating coverage report ---"
    lcov --capture \
         --directory "${BUILD_DIR}" \
         --output-file coverage.info \
         --ignore-errors mismatch \
         2>/dev/null || true

    lcov --remove coverage.info \
         '/usr/*' '*/googletest/*' '*/tests/*' \
         --output-file coverage.info \
         2>/dev/null || true

    if command -v genhtml &>/dev/null; then
        genhtml coverage.info \
            --output-directory coverage_html \
            --quiet 2>/dev/null || true
        echo "Coverage HTML report: ${BUILD_DIR}/coverage_html/index.html"
    fi

    # Print summary
    lcov --summary coverage.info 2>/dev/null || true
fi

# --------------------------------------------------------------------------
# Summary
# --------------------------------------------------------------------------
echo ""
echo "========================================"
echo " Test Summary"
echo "========================================"
if [[ "${UNIT_RESULT}" -eq 0 ]]; then
    echo "  Unit tests   : PASSED"
else
    echo "  Unit tests   : FAILED (exit ${UNIT_RESULT})"
fi

if [[ "${RUN_INTEGRATION}" -eq 1 ]]; then
    if [[ "${INT_RESULT}" -eq 0 ]]; then
        echo "  Integration  : PASSED"
    else
        echo "  Integration  : FAILED (exit ${INT_RESULT})"
    fi
fi

exit $(( UNIT_RESULT | INT_RESULT ))
