# Build Guide

## Prerequisites

### Common
- CMake ≥ 3.10
- C++17-capable compiler (GCC ≥ 9, Clang ≥ 10)
- GNU Make (or Ninja)

### For unit / integration tests
- GoogleTest (`libgtest-dev` on Debian/Ubuntu)
- `lcov` for code coverage reports
- `valgrind` for memory leak detection

### For cross-compilation (ARM)
```bash
# ARM 32-bit (T113i)
sudo apt-get install gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf

# ARM 64-bit (Jetson)
sudo apt-get install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu
```

---

## Native Build (x86_64 Linux)

```bash
cd t113i_daemon
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# Binary is at: build/seeway_interface_daemon
```

Optional CMake flags:

| Flag | Default | Description |
|------|---------|-------------|
| `CMAKE_BUILD_TYPE` | (empty) | `Release`, `Debug`, or `RelWithDebInfo` |
| `ARM_TOOLCHAIN` | (none) | Path to a toolchain `.cmake` file |

---

## Cross-Compilation for ARM 32-bit (T113i / Raspberry Pi)

### Using the automated script

```bash
bash scripts/cross-compile/build.sh --toolchain arm
```

### Manually

```bash
mkdir build_armhf && cd build_armhf
cmake ../t113i_daemon \
    -DCMAKE_TOOLCHAIN_FILE=../cmake/arm-linux-gnueabihf.cmake \
    -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### Verify the binary

```bash
bash scripts/cross-compile/verify_arm_binary.sh \
    build_armhf/seeway_interface_daemon arm
```

Expected output:
```
✓ ELF format
✓ ARM 32-bit
✓ Hard-float ABI
✓ Size reasonable
```

---

## Cross-Compilation for AArch64 (Jetson Orin NX)

```bash
bash scripts/cross-compile/build.sh --toolchain aarch64
# or manually:
mkdir build_aarch64 && cd build_aarch64
cmake ../t113i_daemon \
    -DCMAKE_TOOLCHAIN_FILE=../cmake/aarch64-linux-gnu.cmake \
    -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

---

## Building and Running Tests

```bash
# Build tests (requires libgtest-dev)
mkdir build_tests && cd build_tests
cmake ../tests -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)

# Run all tests via CTest
ctest --output-on-failure

# Or use the convenience script:
cd <repo-root>
bash tests/run_all_tests.sh

# With code coverage:
bash tests/run_all_tests.sh --coverage

# With Valgrind:
bash tests/run_all_tests.sh --valgrind
```

---

## macOS Build (experimental)

```bash
brew install cmake
cd t113i_daemon
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(sysctl -n hw.ncpu)
```

> **Note**: GPIO and ADC sysfs paths are Linux-specific. The daemon will
> compile on macOS but hardware interaction will not function.

---

## CMake Options Reference

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `BUILD_TESTS` | BOOL | OFF | Build test suite |
| `ENABLE_COVERAGE` | BOOL | OFF | Add `--coverage` flags (GCC) |
| `CMAKE_TOOLCHAIN_FILE` | PATH | — | Cross-compile toolchain |
| `ARM_SYSROOT` | PATH | — | Optional sysroot for cross-compile |

---

## Troubleshooting Build Failures

### `error: 'std::filesystem' not found`
Add `-DCMAKE_CXX_FLAGS="-lstdc++fs"` or upgrade GCC to ≥ 9.

### `cannot find -lgtest`
```bash
sudo apt-get install libgtest-dev
# If /usr/lib/libgtest.a is missing after install:
cd /usr/src/googletest && sudo cmake . && sudo make install
```

### Cross-compiler not found
Ensure the package is installed:
```bash
arm-linux-gnueabihf-gcc --version  # should print version
```

### Linker errors with `-lrt` or `-lm`
These are glibc libraries; they should always be present on Linux.
If building in a minimal container, install `libc6-dev`.

### `CMake Error: ... cross-compile ...`
When cross-compiling, CMake cannot run the output binary for try-compile tests.
This is expected and harmless for this project.
