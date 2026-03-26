# Documentation Index

Welcome to the Seeway Interface Daemon documentation for the T113i interface board.

---

## Quick Start

```bash
# Build
cd t113i_daemon
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# Run
./seeway_interface_daemon /etc/seeway_interface/daemon.conf

# Run tests
cmake .. -DENABLE_TESTING=ON && make && ctest
```

---

## Documentation Sections

| Document | Description |
|----------|-------------|
| [ARCHITECTURE.md](ARCHITECTURE.md) | System design, module interactions, data flow, thread model |
| [PROTOCOL.md](PROTOCOL.md) | Binary frame format, all message types, CRC algorithm |
| [API_REFERENCE.md](API_REFERENCE.md) | Public API for all modules (overview + Doxygen link) |
| [BUILD_GUIDE.md](BUILD_GUIDE.md) | x86-64 and ARM build instructions, dependency setup |
| [CONFIGURATION.md](CONFIGURATION.md) | daemon.conf parameters, defaults, validation, tips |
| [PERFORMANCE_NOTES.md](PERFORMANCE_NOTES.md) | Benchmark results, CPU/memory metrics, latency |
| [TROUBLESHOOTING.md](TROUBLESHOOTING.md) | Q&A, log interpretation, debug techniques |
| [CONTRIBUTING.md](CONTRIBUTING.md) | Code style, test requirements, PR process |

---

## Repository Structure

```
treebot_interfaceboard_sw_linux/
├── t113i_daemon/                  # T113i daemon (this project)
│   ├── src/                       # C++ source files
│   ├── include/                   # Public headers
│   ├── config/                    # Example daemon.conf
│   ├── tests/                     # Unit / perf / integration tests
│   │   ├── unit/                  # GoogleTest unit tests
│   │   ├── perf/                  # Performance benchmarks
│   │   ├── integration/           # TCP loopback, error recovery, etc.
│   │   ├── mocks/                 # Mock implementations
│   │   ├── fixtures/              # Test config files
│   │   ├── run_all_tests.sh       # Single-command test runner
│   │   └── performance_report.py  # Aggregate perf CSV → report
│   ├── cmake/                     # Cross-compilation toolchain files
│   ├── scripts/cross-compile/    # Build & verify scripts
│   ├── Doxyfile                   # Doxygen configuration
│   └── CMakeLists.txt
├── docs/                          # Project documentation (this folder)
├── .github/workflows/             # CI/CD pipelines
├── seeway_interface_driver/       # Jetson ROS2 driver
├── seeway_interface_hardware/     # ros2_control hardware interface
└── seeway_interface_msgs/         # ROS2 message definitions
```

---

## Generating API Docs

```bash
cd t113i_daemon
# Install doxygen (and graphviz for call graphs)
sudo apt-get install doxygen graphviz

doxygen Doxyfile
# Output: docs/doxygen/html/index.html
```

---

## CI/CD Pipelines

| Workflow | Trigger | Description |
|----------|---------|-------------|
| `test.yml` | Push / PR | Unit + integration tests, coverage, cppcheck |
| `cross-compile.yml` | Push / PR | Build for armhf and aarch64, verify binary |

---

## Phase History

| Phase | Contents |
|-------|----------|
| Phase 1 | Logger, ConnectionMonitor, TaskManager, ModuleInitializer, ShutdownManager, Protocol handshake |
| Phase 2 | Unit tests (GoogleTest), performance benchmarks, integration tests, cross-compilation, documentation |
