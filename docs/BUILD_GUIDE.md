# Build Guide

Complete instructions for building the Seeway Interface Daemon on x86-64
(development/CI) and cross-compiling for ARM targets (T113i, Jetson).

---

## Prerequisites

### Common
- CMake ≥ 3.10
- GCC ≥ 9 or Clang ≥ 10
- C++17 support

### Ubuntu / Debian
```bash
sudo apt-get update
sudo apt-get install -y \
    build-essential cmake ninja-build \
    libpthread-stubs0-dev
```

### macOS (native build only)
```bash
brew install cmake ninja
```

---

## Native x86-64 Build

```bash
cd t113i_daemon
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

The binary is at `build/seeway_interface_daemon`.

### CMake options

| Option | Default | Description |
|--------|---------|-------------|
| `CMAKE_BUILD_TYPE` | `Release` | `Debug`, `Release`, `RelWithDebInfo` |
| `ENABLE_TESTING` | `OFF` | Build unit/integration tests |
| `ARM_TOOLCHAIN` | — | Path to ARM toolchain file |

---

## Build with Tests

```bash
cd t113i_daemon
mkdir -p build_test && cd build_test
cmake .. \
    -DCMAKE_BUILD_TYPE=Debug \
    -DENABLE_TESTING=ON
make -j$(nproc)

# Run all unit tests
ctest --output-on-failure -R "^test_"

# Run all tests including integration
ctest --output-on-failure
```

GoogleTest is automatically fetched via `FetchContent` if not found in system.

### Coverage report
```bash
cmake .. \
    -DCMAKE_BUILD_TYPE=Debug \
    -DENABLE_TESTING=ON \
    -DCMAKE_CXX_FLAGS="--coverage" \
    -DCMAKE_EXE_LINKER_FLAGS="--coverage"
make -j$(nproc)
ctest
lcov --capture --directory . --output-file coverage.info
genhtml coverage.info --output-directory coverage_html
# View: coverage_html/index.html
```

---

## Cross-Compile for ARM (armhf – T113i / Raspberry Pi)

### Install toolchain
```bash
sudo apt-get install -y \
    gcc-arm-linux-gnueabihf \
    g++-arm-linux-gnueabihf
```

### Build
```bash
cd t113i_daemon
mkdir -p build_armhf && cd build_armhf
cmake .. \
    -DCMAKE_TOOLCHAIN_FILE=../cmake/arm-linux-gnueabihf.cmake \
    -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# Verify the binary
../scripts/cross-compile/verify_arm_binary.sh seeway_interface_daemon
```

Or use the convenience script:
```bash
cd t113i_daemon
./scripts/cross-compile/build.sh --verify
```

---

## Cross-Compile for AArch64 (Jetson Orin NX)

### Install toolchain
```bash
sudo apt-get install -y \
    gcc-aarch64-linux-gnu \
    g++-aarch64-linux-gnu
```

### Build
```bash
cd t113i_daemon
mkdir -p build_aarch64 && cd build_aarch64
cmake .. \
    -DCMAKE_SYSTEM_NAME=Linux \
    -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
    -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc \
    -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++ \
    -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

---

## Deploying to Target

```bash
# Copy binary and config
scp build_armhf/seeway_interface_daemon user@t113i-board:/usr/local/bin/
scp config/daemon.conf user@t113i-board:/etc/seeway_interface/

# Run manually
ssh user@t113i-board /usr/local/bin/seeway_interface_daemon \
    /etc/seeway_interface/daemon.conf
```

### Systemd service
```ini
[Unit]
Description=Seeway Interface Daemon
After=network.target

[Service]
ExecStart=/usr/local/bin/seeway_interface_daemon /etc/seeway_interface/daemon.conf
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
```

---

## Generating API Documentation

```bash
cd t113i_daemon
# Requires doxygen (and graphviz for graphs)
sudo apt-get install -y doxygen graphviz
doxygen Doxyfile
# Output: docs/doxygen/html/index.html
```

---

## Troubleshooting

### CMake cannot find Threads
```bash
sudo apt-get install -y libpthread-stubs0-dev
```

### `rt` library not found
On modern glibc (≥ 2.17), `clock_gettime` is in libc; remove `rt` from
`target_link_libraries` or ignore the linker warning.

### Cross-compiler not found
```bash
which arm-linux-gnueabihf-gcc
# If empty:
sudo apt-get install gcc-arm-linux-gnueabihf
```
