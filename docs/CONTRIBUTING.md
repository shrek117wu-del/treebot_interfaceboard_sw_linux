# Contributing Guide

Thank you for contributing to the Seeway Interface Daemon project.

---

## Code Style

### C++ conventions
- Standard: **C++17**
- Formatter: `clang-format` (Google style, 4-space indent)
- All new headers use `#pragma once`
- Doxygen comments for all public APIs (`/** @brief ... */`)
- No raw `std::cout`/`std::cerr` in library code – use `Logger::info/warn/error`
- Thread-safety documented with `@par Thread-Safety` alias

### Naming
| Kind | Convention | Example |
|------|-----------|---------|
| Classes | PascalCase | `ConnectionMonitor` |
| Methods | snake_case | `check_health()` |
| Constants | ALL_CAPS | `MAX_QUEUE_DEPTH` |
| Member vars | trailing `_` | `running_` |
| Files | snake_case | `task_executor.cpp` |

### File structure
```
include/module_name.h   – Public API and documentation
src/module_name.cpp     – Implementation
tests/unit/test_module_name.cpp  – Unit tests
```

---

## Test Requirements

All new features or bug fixes **must** include tests.

### Running tests
```bash
cd t113i_daemon
mkdir -p build_test && cd build_test
cmake .. -DENABLE_TESTING=ON -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)
ctest --output-on-failure
```

### Coverage requirement
- Target: **≥ 80%** line coverage for all new modules
- Generate report:
  ```bash
  cmake .. -DCMAKE_CXX_FLAGS="--coverage" ...
  make && ctest
  lcov --capture --directory . --output-file cov.info
  genhtml cov.info -o cov_html
  ```

### Test categories
| Category | Directory | When to add |
|----------|-----------|-------------|
| Unit | `tests/unit/` | Every new class/function |
| Integration | `tests/integration/` | Cross-module behaviour |
| Performance | `tests/perf/` | New performance-critical paths |

---

## Pull Request Process

1. **Fork** the repository and create a feature branch:
   ```bash
   git checkout -b feature/my-improvement
   ```

2. **Write tests** before or alongside your code changes

3. **Ensure all tests pass**:
   ```bash
   cd t113i_daemon
   ./tests/run_all_tests.sh
   ```

4. **Run static analysis**:
   ```bash
   cppcheck --std=c++17 --enable=warning -I include src/
   ```

5. **Update documentation** if you change the public API or configuration

6. **Commit** using conventional commit format:
   ```
   feat(task_executor): add priority queue support
   fix(config_loader): handle missing section headers
   test(logger): add thread-safety test
   docs(protocol): add BATTERY_STATUS payload definition
   ```

7. **Open a PR** with:
   - Summary of changes
   - Test coverage summary
   - Any breaking changes noted

---

## Commit Format

```
<type>(<scope>): <short description>

[optional body]

[optional footer: Closes #issue]
```

**Types:** `feat`, `fix`, `test`, `docs`, `refactor`, `perf`, `ci`, `chore`

**Scopes:** `logger`, `config_loader`, `serial_comm`, `task_executor`,
           `connection_monitor`, `protocol`, `gpio`, `power`, `cmake`, `ci`

---

## Adding a New Module

1. Create `include/my_module.h` with Doxygen comments
2. Create `src/my_module.cpp`
3. Add to `CMakeLists.txt` `SOURCES` list
4. Add to `tests/CMakeLists.txt` `DAEMON_SOURCES` list
5. Create `tests/unit/test_my_module.cpp`
6. Register test in `tests/CMakeLists.txt` via `add_daemon_test()`

---

## Security Policy

- No credentials or secrets in source code
- All user-supplied data (config values) must be validated before use
- Buffer sizes must be checked before `std::memcpy` / `reinterpret_cast`
- Use `strnlen`/`strncpy` instead of `strlen`/`strcpy` for fixed-size buffers
- Report security vulnerabilities privately to the maintainers
