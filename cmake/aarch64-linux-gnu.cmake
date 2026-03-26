# cmake/aarch64-linux-gnu.cmake
# ---------------------------------------------------------------------------
# CMake toolchain file for cross-compiling to 64-bit ARM (AArch64).
# Target: Jetson Orin NX / NVIDIA Orin / Raspberry Pi 4 (64-bit)
# Host:   x86_64 Linux with aarch64-linux-gnu-gcc installed
#
# Usage:
#   cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/aarch64-linux-gnu.cmake
# ---------------------------------------------------------------------------

set(CMAKE_SYSTEM_NAME    Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# ---------------------------------------------------------------------------
# Toolchain prefix
# ---------------------------------------------------------------------------
set(TOOLCHAIN_PREFIX "aarch64-linux-gnu")

find_program(AARCH64_GCC "${TOOLCHAIN_PREFIX}-gcc-12"
             NAMES "${TOOLCHAIN_PREFIX}-gcc-11"
                   "${TOOLCHAIN_PREFIX}-gcc-10"
                   "${TOOLCHAIN_PREFIX}-gcc-9"
                   "${TOOLCHAIN_PREFIX}-gcc")
find_program(AARCH64_GXX "${TOOLCHAIN_PREFIX}-g++-12"
             NAMES "${TOOLCHAIN_PREFIX}-g++-11"
                   "${TOOLCHAIN_PREFIX}-g++-10"
                   "${TOOLCHAIN_PREFIX}-g++-9"
                   "${TOOLCHAIN_PREFIX}-g++")

if(NOT AARCH64_GCC)
    set(AARCH64_GCC "${TOOLCHAIN_PREFIX}-gcc")
    set(AARCH64_GXX "${TOOLCHAIN_PREFIX}-g++")
endif()

set(CMAKE_C_COMPILER   ${AARCH64_GCC})
set(CMAKE_CXX_COMPILER ${AARCH64_GXX})

# ---------------------------------------------------------------------------
# Sysroot (optional)
# ---------------------------------------------------------------------------
if(DEFINED AARCH64_SYSROOT)
    set(CMAKE_SYSROOT ${AARCH64_SYSROOT})
endif()

# ---------------------------------------------------------------------------
# Target CPU flags (generic AArch64 + crypto extensions)
# ---------------------------------------------------------------------------
set(AARCH64_FLAGS "-march=armv8-a+crypto -O2")

set(CMAKE_C_FLAGS_INIT   ${AARCH64_FLAGS})
set(CMAKE_CXX_FLAGS_INIT ${AARCH64_FLAGS})

# ---------------------------------------------------------------------------
# Search path settings
# ---------------------------------------------------------------------------
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
