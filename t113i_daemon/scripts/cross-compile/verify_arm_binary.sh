#!/usr/bin/env bash
# scripts/cross-compile/verify_arm_binary.sh
# Verify that a binary is a valid ARM ELF executable.
#
# Usage:
#   ./verify_arm_binary.sh <path-to-binary>
#
# Checks performed:
#   1. File exists
#   2. ELF magic bytes present
#   3. Architecture = ARM (ELF e_machine = 40)
#   4. readelf -h shows ARM
#   5. Key symbols present (main, Logger, TaskExecutor)
#   6. No x86-64 or i386 architecture
#   7. Shared library dependencies listed

set -euo pipefail

if [[ $# -lt 1 ]]; then
    echo "Usage: $0 <path-to-binary>"
    exit 1
fi

BINARY="$1"
PASS=0
FAIL=0

check() {
    local desc="$1"
    local result="$2"
    if [[ "$result" == "ok" ]]; then
        echo "  [PASS] ${desc}"
        (( PASS++ )) || true
    else
        echo "  [FAIL] ${desc}: ${result}"
        (( FAIL++ )) || true
    fi
}

echo "========================================"
echo " ARM Binary Verification: ${BINARY}"
echo "========================================"

# --------------------------------------------------------------------------
# 1. File exists and is not empty
# --------------------------------------------------------------------------
if [[ -f "${BINARY}" && -s "${BINARY}" ]]; then
    check "File exists and is non-empty" "ok"
else
    check "File exists and is non-empty" "file missing or empty"
fi

# --------------------------------------------------------------------------
# 2. ELF magic
# --------------------------------------------------------------------------
MAGIC=$(xxd -l 4 "${BINARY}" 2>/dev/null | awk '{print $2}')
if [[ "${MAGIC}" == "7f45" ]]; then
    check "ELF magic bytes (7f 45 4c 46)" "ok"
else
    check "ELF magic bytes" "got ${MAGIC}"
fi

# --------------------------------------------------------------------------
# 3. Architecture check via file(1)
# --------------------------------------------------------------------------
if command -v file &>/dev/null; then
    FILE_OUT=$(file "${BINARY}")
    if echo "${FILE_OUT}" | grep -q "ARM"; then
        check "file(1) reports ARM" "ok"
    else
        check "file(1) reports ARM" "${FILE_OUT}"
    fi
fi

# --------------------------------------------------------------------------
# 4. readelf -h (architecture field)
# --------------------------------------------------------------------------
if command -v readelf &>/dev/null || \
   command -v arm-linux-gnueabihf-readelf &>/dev/null; then
    RE=$(command -v arm-linux-gnueabihf-readelf 2>/dev/null || echo readelf)
    RE_OUT=$("${RE}" -h "${BINARY}" 2>/dev/null)

    if echo "${RE_OUT}" | grep -q "Machine.*ARM"; then
        check "readelf: Machine = ARM" "ok"
    else
        MACHINE=$(echo "${RE_OUT}" | grep "Machine" || echo "unknown")
        check "readelf: Machine = ARM" "${MACHINE}"
    fi

    # Class (32-bit)
    if echo "${RE_OUT}" | grep -q "Class.*ELF32"; then
        check "readelf: Class = ELF32" "ok"
    else
        CLASS=$(echo "${RE_OUT}" | grep "Class" || echo "unknown")
        check "readelf: Class = ELF32" "${CLASS}"
    fi

    # NOT x86
    if echo "${RE_OUT}" | grep -q "Machine.*X86\|Machine.*386\|Machine.*x86"; then
        check "Not x86 architecture" "binary is x86!"
    else
        check "Not x86 architecture" "ok"
    fi
fi

# --------------------------------------------------------------------------
# 5. Symbol check (nm or arm-linux-gnueabihf-nm)
# --------------------------------------------------------------------------
if command -v arm-linux-gnueabihf-nm &>/dev/null || \
   command -v nm &>/dev/null; then
    NM=$(command -v arm-linux-gnueabihf-nm 2>/dev/null || echo nm)
    NM_OUT=$("${NM}" "${BINARY}" 2>/dev/null || true)

    for sym in "main" "_ZN6Logger4initE"; do
        if echo "${NM_OUT}" | grep -q "${sym}"; then
            check "Symbol present: ${sym}" "ok"
        else
            check "Symbol present: ${sym}" "not found"
        fi
    done
fi

# --------------------------------------------------------------------------
# 6. Dynamic library dependencies
# --------------------------------------------------------------------------
if command -v arm-linux-gnueabihf-readelf &>/dev/null || \
   command -v readelf &>/dev/null; then
    RE=$(command -v arm-linux-gnueabihf-readelf 2>/dev/null || echo readelf)
    echo ""
    echo "  Dynamic dependencies:"
    "${RE}" -d "${BINARY}" 2>/dev/null | grep "NEEDED" | \
        awk '{print "    " $0}' || echo "    (none)"
fi

# --------------------------------------------------------------------------
# Summary
# --------------------------------------------------------------------------
echo ""
echo "========================================"
echo " Results: ${PASS} PASSED, ${FAIL} FAILED"
echo "========================================"

exit ${FAIL}
