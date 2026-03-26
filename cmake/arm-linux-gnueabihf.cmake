# cmake/arm-linux-gnueabihf.cmake
# ---------------------------------------------------------------------------
# CMake toolchain file for cross-compiling to ARM 32-bit (hard-float ABI).
# Target: Raspberry Pi / T113i (ARMv7-A, NEON FPU, hard-float)
# Host:   x86_64 Linux with arm-linux-gnueabihf-gcc installed
#
# Usage:
#   cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/arm-linux-gnueabihf.cmake
# ---------------------------------------------------------------------------

set(CMAKE_SYSTEM_NAME    Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

# ---------------------------------------------------------------------------
# Toolchain prefix – adjust if your distro uses a different version suffix
# ---------------------------------------------------------------------------
set(TOOLCHAIN_PREFIX "arm-linux-gnueabihf")

# Prefer versioned compilers; fall back to unversioned
find_program(ARM_GCC  "${TOOLCHAIN_PREFIX}-gcc-12"
             NAMES "${TOOLCHAIN_PREFIX}-gcc-11"
                   "${TOOLCHAIN_PREFIX}-gcc-10"
                   "${TOOLCHAIN_PREFIX}-gcc-9"
                   "${TOOLCHAIN_PREFIX}-gcc")
find_program(ARM_GXX  "${TOOLCHAIN_PREFIX}-g++-12"
             NAMES "${TOOLCHAIN_PREFIX}-g++-11"
                   "${TOOLCHAIN_PREFIX}-g++-10"
                   "${TOOLCHAIN_PREFIX}-g++-9"
                   "${TOOLCHAIN_PREFIX}-g++")

if(NOT ARM_GCC)
    # Fall back to unversioned names
    set(ARM_GCC  "${TOOLCHAIN_PREFIX}-gcc")
    set(ARM_GXX  "${TOOLCHAIN_PREFIX}-g++")
endif()

set(CMAKE_C_COMPILER   ${ARM_GCC})
set(CMAKE_CXX_COMPILER ${ARM_GXX})

# ---------------------------------------------------------------------------
# Sysroot (optional – set if cross-compiling with a dedicated sysroot)
# ---------------------------------------------------------------------------
if(DEFINED ARM_SYSROOT)
    set(CMAKE_SYSROOT ${ARM_SYSROOT})
endif()

# ---------------------------------------------------------------------------
# Target CPU flags
# ---------------------------------------------------------------------------
set(ARM_ARCH_FLAGS "-march=armv7-a -mfpu=neon -mfloat-abi=hard")

set(CMAKE_C_FLAGS_INIT   "${ARM_ARCH_FLAGS} -O2")
set(CMAKE_CXX_FLAGS_INIT "${ARM_ARCH_FLAGS} -O2")

# ---------------------------------------------------------------------------
# Search path settings – prevent using host binaries in sysroot mode
# ---------------------------------------------------------------------------
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
