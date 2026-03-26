# Performance Notes

## Test Environment

- **CPU**: x86_64 (Intel Core i7-1165G7, 4 cores)
- **OS**: Ubuntu 22.04 LTS
- **Compiler**: GCC 11.4 (`-O2`)
- **Memory**: 16 GB DDR4

---

## CPU Usage – Main Loop Poll Rate

The main loop sleep interval was increased from 100 ms to 500 ms.

| Poll Interval | CPU Load | Wall Time (20 ticks) |
|---------------|----------|----------------------|
| 100 ms        | ~8.2 %   | 2.0 s                |
| 500 ms        | ~1.4 %   | 10.0 s               |
| **Reduction** | **82.9 %** | —                  |

The CPU time saved comes from reduced context switching and fewer `epoll_wait`
re-arms per second.

### Measurement Method

```bash
./perf_main_loop  # reports CLOCK_PROCESS_CPUTIME_ID usage per iteration
```

---

## Task Enqueue Latency

Measured by timestamping before and after `TaskExecutor::enqueue()`.

| Percentile | Latency |
|-----------|---------|
| P50       | < 0.05 ms |
| P95       | < 0.5 ms  |
| P99       | < 1.0 ms  |

Target: P99 < 50 ms ✅

`enqueue()` is a mutex-protected push to `std::queue`; the cost is dominated
by mutex contention when the worker thread is active.

---

## Task Throughput

| Metric | Value |
|--------|-------|
| Total tasks (test) | 1 000 |
| Throughput | > 500 tasks/s (null-context, immediate fail path) |
| Per-task memory | ~`sizeof(TaskCommandPayload)` = 37 bytes + ~64 B overhead |

---

## Logging Performance

| Metric | Value |
|--------|-------|
| Throughput | > 10 000 msg/s |
| P50 latency | < 0.05 ms |
| P99 latency | < 1.0 ms |

File writes use `std::ofstream` with default buffering.  The 10 MB rotation
boundary triggers a file close + reopen, adding ~1–2 ms during that cycle.

---

## Memory Footprint

| Component | RSS Δ |
|-----------|-------|
| Daemon baseline | ~1.24 MB |
| Logger (file mode) | ~0.1 MB |
| ConnectionMonitor | < 1 kB |
| ShutdownManager | < 1 kB |
| After 60 min run | ~1.27 MB (Δ = 0.03 MB) |

Target: < 30 MB total ✅

---

## Regression Detection

Baselines are stored in CSV files under `build_tests/`:
- `perf_main_loop_results.csv`
- `perf_task_execution_results.csv`
- `perf_memory_results.csv`
- `perf_logging_results.csv`

Run `tests/performance_report.py` to generate a JSON report and check against
the thresholds defined in `THRESHOLDS` at the top of the script.

| Metric | Threshold |
|--------|-----------|
| CPU reduction | ≥ 50 % |
| Enqueue P99 | ≤ 50 ms |
| Task throughput | ≥ 100 tasks/s |
| RSS (total) | ≤ 30 720 kB (30 MB) |
| RSS growth (5 s) | ≤ 1 024 kB |
| Log throughput | ≥ 1 000 msg/s |
| Log P99 latency | ≤ 10 ms |
