# T113i Daemon – Documentation

Welcome to the T113i Interface Board Software documentation.

## Contents

| Document | Description |
|----------|-------------|
| [ARCHITECTURE.md](ARCHITECTURE.md) | System components, data flow, and thread model |
| [PROTOCOL.md](PROTOCOL.md) | Binary frame specification and message reference |
| [BUILD_GUIDE.md](BUILD_GUIDE.md) | Build instructions (native + cross-compile) |
| [CONFIGURATION.md](CONFIGURATION.md) | `daemon.conf` parameter reference |
| [PERFORMANCE_NOTES.md](PERFORMANCE_NOTES.md) | CPU, latency, and memory benchmarks |
| [TROUBLESHOOTING.md](TROUBLESHOOTING.md) | Common issues and diagnostic procedures |
| [CONTRIBUTING.md](CONTRIBUTING.md) | Code style, testing requirements, and PR process |

## Quick Start

```bash
# Build (native x86_64)
cd t113i_daemon
mkdir build && cd build
cmake .. && make -j$(nproc)

# Run tests
cd <repo-root>
bash tests/run_all_tests.sh

# Cross-compile for ARM
bash scripts/cross-compile/build.sh --toolchain arm
```

## Project Status

| Component | Status |
|-----------|--------|
| Logger | ✅ Complete |
| ConnectionMonitor | ✅ Complete |
| TaskExecutor | ✅ Complete |
| ModuleInitializer | ✅ Complete |
| ShutdownManager | ✅ Complete |
| Protocol | ✅ Complete |
| Unit Tests | ✅ 7 test suites |
| Integration Tests | ✅ 4 scenarios |
| Performance Tests | ✅ 4 benchmarks |
| Cross-compile (armhf) | ✅ Supported |
| Cross-compile (aarch64) | ✅ Supported |
| CI/CD | ✅ GitHub Actions |

## Performance Highlights

| Metric | Value |
|--------|-------|
| CPU usage (500 ms loop) | ~1.4 % |
| CPU usage (100 ms loop) | ~8.2 % |
| CPU reduction | **82.9 %** |
| Task enqueue P99 latency | < 1 ms |
| Log throughput | > 10 000 msg/s |
| Memory footprint | < 5 MB |

## Support

For issues and diagnostics, see [TROUBLESHOOTING.md](TROUBLESHOOTING.md).
