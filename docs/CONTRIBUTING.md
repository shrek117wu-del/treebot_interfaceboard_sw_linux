# Contributing Guide

## Code Style

This project uses **C++17** throughout.

### Formatting

- Indent with **4 spaces** (no tabs)
- Maximum line length: **100 characters**
- Opening braces on the same line as the statement (`K&R` style)
- Use `clang-format` with default LLVM style (run before committing)

```bash
clang-format -i t113i_daemon/src/*.cpp t113i_daemon/include/*.h
```

### Naming Conventions

| Symbol | Convention | Example |
|--------|-----------|---------|
| Classes | `PascalCase` | `ConnectionMonitor` |
| Methods | `snake_case` | `check_health()` |
| Member variables | `snake_case_` (trailing `_`) | `reconnect_attempts_` |
| Constants | `UPPER_SNAKE_CASE` | `MAX_RETRIES` |
| Type aliases | `PascalCase` | `ResultCb` |
| File names | `snake_case` | `connection_monitor.cpp` |

### Documentation Comments

All public interfaces must have Doxygen-compatible comments:

```cpp
/**
 * @brief Brief description.
 *
 * Optional longer description.
 *
 * @param param_name Description of the parameter.
 * @return Description of the return value.
 */
```

---

## Testing Requirements

All contributions must maintain or improve test coverage (target: ≥ 80 %).

### Before submitting a PR:

1. **Run unit tests** and ensure they all pass:
   ```bash
   bash tests/run_all_tests.sh
   ```

2. **Run Valgrind** on unit tests (zero leaks required):
   ```bash
   bash tests/run_all_tests.sh --valgrind
   ```

3. **Add tests** for new functionality. Tests go in:
   - `tests/unit/test_<component>.cpp` for unit tests
   - `tests/integration/integration_test_<scenario>.cpp` for integration tests

4. **Register tests** in `tests/CMakeLists.txt` using `add_gtest_target()`.

### Test guidelines

- Each `TEST_F` / `TEST` should test a single behaviour
- Use descriptive names: `TEST(ComponentTest, WhatItDoes)`
- Avoid sleeping more than 500 ms per test (keep suite under 5 s)
- Use `ASSERT_*` for fatal failures, `EXPECT_*` for non-fatal

---

## Commit Message Format

```
<type>(<scope>): <short summary>

<optional body>
```

Types: `feat`, `fix`, `test`, `docs`, `refactor`, `perf`, `ci`, `chore`

Examples:
```
feat(connection_monitor): add exponential backoff on reconnect
fix(task_executor): guard against null TaskContext before enqueue
test(logger): add concurrent-write stress test
docs(PROTOCOL): document TASK_RESPONSE payload layout
```

---

## Pull Request Process

1. **Fork** the repository and create a feature branch:
   ```bash
   git checkout -b feat/my-feature
   ```

2. **Make changes** and run tests locally.

3. **Push** and open a PR against `main`.

4. **PR description** must include:
   - Summary of changes
   - Test coverage impact
   - Breaking changes (if any)

5. PRs must pass:
   - ✅ Unit tests (GitHub Actions: `unit-tests.yml`)
   - ✅ Cross-compile (GitHub Actions: `cross-compile.yml`)
   - ✅ Code review approval

---

## Adding a New Module

1. Create `t113i_daemon/include/<module>.h` with full Doxygen comments
2. Create `t113i_daemon/src/<module>.cpp`
3. Add to `SOURCES` list in `t113i_daemon/CMakeLists.txt`
4. Add to `DAEMON_SOURCES` list in `tests/CMakeLists.txt`
5. Create `tests/unit/test_<module>.cpp` and register in `tests/CMakeLists.txt`
6. Update `docs/ARCHITECTURE.md` if the module changes the component graph

---

## Security

- Never commit credentials, tokens, or private keys
- GPIO numbers must be validated against 0–511 range
- All buffer sizes should use bounded types (`uint16_t`, not `int`)
- Use `std::string` or bounds-checked arrays; avoid raw `char*` buffers
