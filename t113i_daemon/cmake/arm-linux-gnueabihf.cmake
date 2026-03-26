# cmake/arm-linux-gnueabihf.cmake
# ARM hard-float cross-compilation toolchain for T113i daemon.
#
# Targets: ARMv7-A (Raspberry Pi, T113i) with hardware FPU.
# Toolchain: gcc-9 arm-linux-gnueabihf (Debian/Ubuntu cross-tools package)
#
# Install dependencies on build host:
#   sudo apt-get install gcc-9-arm-linux-gnueabihf g++-9-arm-linux-gnueabihf
#   sudo apt-get install gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf
#
# Usage:
#   cmake -DCMAKE_TOOLCHAIN_FILE=../cmake/arm-linux-gnueabihf.cmake ..

set(CMAKE_SYSTEM_NAME      Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

# ---------------------------------------------------------------------------
# Toolchain executables
# Prefer versioned gcc-9; fall back to default arm-linux-gnueabihf-gcc
# ---------------------------------------------------------------------------
find_program(ARM_GCC   arm-linux-gnueabihf-gcc-9
             NAMES     arm-linux-gnueabihf-gcc
             DOC       "ARM cross-compiler (C)")
find_program(ARM_GXX   arm-linux-gnueabihf-g++-9
             NAMES     arm-linux-gnueabihf-g++
             DOC       "ARM cross-compiler (C++)")

if(NOT ARM_GCC)
    message(FATAL_ERROR
        "ARM cross-compiler not found.\n"
        "Install with: sudo apt-get install gcc-arm-linux-gnueabihf")
endif()

set(CMAKE_C_COMPILER   ${ARM_GCC})
set(CMAKE_CXX_COMPILER ${ARM_GXX})

# ---------------------------------------------------------------------------
# Sysroot (optional – set ARMHF_SYSROOT env var or -DARMHF_SYSROOT=...)
# ---------------------------------------------------------------------------
if(DEFINED ARMHF_SYSROOT)
    set(CMAKE_SYSROOT "${ARMHF_SYSROOT}")
    message(STATUS "ARM sysroot: ${CMAKE_SYSROOT}")
endif()

# ---------------------------------------------------------------------------
# Compiler flags
# ---------------------------------------------------------------------------
set(CMAKE_C_FLAGS_INIT
    "-march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard -O2 -pipe")
set(CMAKE_CXX_FLAGS_INIT
    "-march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard -O2 -pipe")

# ---------------------------------------------------------------------------
# Search paths: only look in sysroot for libraries and headers; never
# use host include/lib paths.
# ---------------------------------------------------------------------------
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# ---------------------------------------------------------------------------
# C++ standard
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17 CACHE STRING "C++ standard")
set(CMAKE_CXX_STANDARD_REQUIRED ON)

message(STATUS "Toolchain: arm-linux-gnueabihf (ARMv7-A hard-float)")
message(STATUS "  CC : ${CMAKE_C_COMPILER}")
message(STATUS "  CXX: ${CMAKE_CXX_COMPILER}")
