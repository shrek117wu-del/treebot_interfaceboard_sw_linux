#!/usr/bin/env bash
# tests/run_all_tests.sh
# ---------------------------------------------------------------------------
# One-command test execution for the T113i daemon test suite.
#
# Usage:
#   cd <repo-root>
#   bash tests/run_all_tests.sh [--coverage] [--valgrind]
#
# Options:
#   --coverage   Enable lcov code-coverage report (requires lcov + GCC)
#   --valgrind   Run unit tests under Valgrind for memory leak detection
# ---------------------------------------------------------------------------
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build_tests"

COVERAGE=false
VALGRIND=false
for arg in "$@"; do
    case "$arg" in
        --coverage) COVERAGE=true ;;
        --valgrind) VALGRIND=true ;;
    esac
done

# ---------------------------------------------------------------------------
# 1. Configure & build
# ---------------------------------------------------------------------------
echo "============================================================"
echo " T113i Daemon – Test Suite"
echo "============================================================"
echo ""

CMAKE_OPTS="-DCMAKE_BUILD_TYPE=Debug"
if $COVERAGE; then
    CMAKE_OPTS="${CMAKE_OPTS} -DENABLE_COVERAGE=ON"
fi

echo ">>> Configuring tests..."
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"
cmake "${SCRIPT_DIR}" ${CMAKE_OPTS} -DCMAKE_CXX_STANDARD=17 2>&1 | tail -5

echo ""
echo ">>> Building tests..."
make -j"$(nproc)" 2>&1 | tail -20

# ---------------------------------------------------------------------------
# 2. Unit tests
# ---------------------------------------------------------------------------
echo ""
echo "------------------------------------------------------------"
echo " Unit Tests"
echo "------------------------------------------------------------"

UNIT_TESTS=(
    test_logger
    test_connection_monitor
    test_task_manager
    test_module_initializer
    test_config_loader
    test_protocol
    test_utils
)

UNIT_PASS=0
UNIT_FAIL=0

for t in "${UNIT_TESTS[@]}"; do
    if [ ! -f "./${t}" ]; then
        echo "  SKIP (binary not found): ${t}"
        continue
    fi
    echo -n "  ${t} ... "
    if $VALGRIND; then
        if valgrind --error-exitcode=1 --leak-check=full "./${t}" \
                --gtest_brief=1 > /tmp/${t}_vg.log 2>&1; then
            echo "PASS (valgrind clean)"
            ((UNIT_PASS++))
        else
            echo "FAIL"
            cat /tmp/${t}_vg.log | grep -E "(ERROR|definitely|possibly)" | head -5
            ((UNIT_FAIL++))
        fi
    else
        if "./${t}" --gtest_brief=1 > /tmp/${t}.log 2>&1; then
            echo "PASS"
            ((UNIT_PASS++))
        else
            echo "FAIL"
            cat /tmp/${t}.log | tail -10
            ((UNIT_FAIL++))
        fi
    fi
done

echo ""
echo "  Unit tests: ${UNIT_PASS} passed, ${UNIT_FAIL} failed"

# ---------------------------------------------------------------------------
# 3. Integration tests
# ---------------------------------------------------------------------------
echo ""
echo "------------------------------------------------------------"
echo " Integration Tests"
echo "------------------------------------------------------------"

INTEG_TESTS=(
    integration_test_tcp_loopback
    integration_test_error_recovery
    integration_test_protocol_compliance
)
# Long-running is optional (skip in CI unless explicitly enabled)
if [ "${RUN_LONG_RUNNING:-0}" = "1" ]; then
    INTEG_TESTS+=(integration_test_long_running)
fi

INTEG_PASS=0
INTEG_FAIL=0

for t in "${INTEG_TESTS[@]}"; do
    if [ ! -f "./${t}" ]; then
        echo "  SKIP: ${t}"
        continue
    fi
    echo -n "  ${t} ... "
    if "./${t}" --gtest_brief=1 > /tmp/${t}.log 2>&1; then
        echo "PASS"
        ((INTEG_PASS++))
    else
        echo "FAIL"
        cat /tmp/${t}.log | tail -10
        ((INTEG_FAIL++))
    fi
done

echo ""
echo "  Integration tests: ${INTEG_PASS} passed, ${INTEG_FAIL} failed"

# ---------------------------------------------------------------------------
# 4. Performance tests
# ---------------------------------------------------------------------------
echo ""
echo "------------------------------------------------------------"
echo " Performance Tests"
echo "------------------------------------------------------------"

PERF_TESTS=(perf_main_loop perf_logging perf_memory perf_task_execution)
PERF_PASS=0
PERF_FAIL=0

for t in "${PERF_TESTS[@]}"; do
    if [ ! -f "./${t}" ]; then
        echo "  SKIP: ${t}"
        continue
    fi
    echo -n "  ${t} ... "
    if "./${t}" > /tmp/${t}.log 2>&1; then
        echo "PASS"
        ((PERF_PASS++))
    else
        echo "FAIL"
        cat /tmp/${t}.log | tail -5
        ((PERF_FAIL++))
    fi
done

# Generate performance report if Python is available
if command -v python3 &>/dev/null && [ -f "${SCRIPT_DIR}/performance_report.py" ]; then
    python3 "${SCRIPT_DIR}/performance_report.py" --build-dir "${BUILD_DIR}" \
        > /tmp/perf_report.json 2>&1 || true
fi

echo ""
echo "  Performance tests: ${PERF_PASS} passed, ${PERF_FAIL} failed"

# ---------------------------------------------------------------------------
# 5. Code coverage report
# ---------------------------------------------------------------------------
if $COVERAGE && command -v lcov &>/dev/null; then
    echo ""
    echo "------------------------------------------------------------"
    echo " Code Coverage"
    echo "------------------------------------------------------------"
    lcov --capture --directory . --output-file coverage.info \
         --ignore-errors inconsistent 2>/dev/null || true
    lcov --remove coverage.info '/usr/*' '*/tests/*' '*/gtest/*' \
         --output-file coverage_filtered.info 2>/dev/null || true
    if command -v genhtml &>/dev/null; then
        genhtml coverage_filtered.info --output-directory coverage_html \
                --quiet 2>/dev/null || true
        echo "  Coverage report: ${BUILD_DIR}/coverage_html/index.html"
    fi
    lcov --summary coverage_filtered.info 2>/dev/null || true
fi

# ---------------------------------------------------------------------------
# 6. Summary
# ---------------------------------------------------------------------------
echo ""
echo "============================================================"
TOTAL_PASS=$((UNIT_PASS + INTEG_PASS + PERF_PASS))
TOTAL_FAIL=$((UNIT_FAIL + INTEG_FAIL + PERF_FAIL))
echo " TOTAL: ${TOTAL_PASS} passed, ${TOTAL_FAIL} failed"
echo "============================================================"
echo ""

if [ "${TOTAL_FAIL}" -gt 0 ]; then
    exit 1
fi
exit 0
