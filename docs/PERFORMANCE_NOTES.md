# Performance Notes

Benchmark results and tuning recommendations for the Seeway Interface Daemon.

---

## CPU Usage: Main Loop Polling Interval

The main loop sleep interval directly controls CPU consumption.

| Interval | Iterations/sec | Relative CPU | Notes |
|----------|---------------|-------------|-------|
| 100 ms   | 10            | baseline    | Original design |
| 500 ms   | 2             | ~80% lower  | Current design |

**Key optimization (Phase 1):** Sleep interval changed from 100 ms to 500 ms,
reducing daemon CPU usage by approximately 80%.  All time-sensitive work
(frame RX, task execution) is done in dedicated threads—not in the main loop.

To reproduce:
```bash
./tests/perf/perf_main_loop
cat perf_main_loop.csv
```

---

## Task Execution Latency

Measured with 1,000 `TASK_CUSTOM` commands on an x86-64 development machine.

| Metric | Target | Typical (x86) |
|--------|--------|---------------|
| P50    | < 5 ms | < 1 ms |
| P95    | < 20 ms | < 3 ms |
| P99    | < 50 ms | < 10 ms |
| Throughput | > 100 tasks/sec | > 1000 tasks/sec |

On T113i hardware (ARM Cortex-A7 @ 1.2 GHz) latencies will be higher,
but the target thresholds above are intentionally conservative.

To reproduce:
```bash
./tests/perf/perf_task_execution
cat perf_task_execution.csv
```

---

## Logger Performance

| Metric | Target | Typical (x86, tmpfs) |
|--------|--------|----------------------|
| Throughput | > 10,000 msg/sec | > 50,000 msg/sec |
| P99 latency | < 1 ms | < 0.5 ms |

Logger uses `std::mutex` for thread safety.  File writes are synchronous
(`std::ofstream::flush()` after each message).

**Tuning options:**
- Set `log_level=2` (WARN) or higher to reduce write frequency
- Use a RAM-backed filesystem (`tmpfs`) for the log file in production
- Consider disabling file logging (`log_file=`) if syslog is preferred

To reproduce:
```bash
./tests/perf/perf_logging
cat perf_logging.csv
```

---

## Memory Footprint

Measured with `VmRSS` from `/proc/self/status` on a typical embedded Linux.

| Component | Expected Footprint |
|-----------|-------------------|
| Logger (1000 messages buffered) | < 1 MB |
| TaskExecutor (64-task queue full) | < 100 KB |
| GpioController (32 pins) | < 50 KB |
| Total daemon RSS | < 10 MB (idle), < 50 MB (stressed) |

Per-task memory: `sizeof(TaskCommandPayload)` = 37 bytes + queue overhead ≈ 128 bytes.

To reproduce:
```bash
./tests/perf/perf_memory
cat perf_memory.csv
```

---

## Throughput vs. Polling Interval (Regression Baseline)

Run this benchmark in CI to detect performance regressions:

```bash
./tests/perf/perf_main_loop
python3 tests/performance_report.py --dir .
```

Expected thresholds (defined in `tests/performance_report.py`):
- CPU reduction: ≥ 70%
- Task P50: ≤ 5 ms
- Task P95: ≤ 20 ms
- Task P99: ≤ 50 ms
- Logger throughput: ≥ 10,000 msg/sec
- Total RSS: ≤ 50 MB

---

## ARM vs x86 Performance Notes

On the T113i (ARM Cortex-A7 @ 1.2 GHz, single-core relevant for daemon):

- Expected CPU overhead: +3–5× compared to x86-64
- Task P99 target relaxed to < 200 ms for ARM in production
- Logger file I/O depends on eMMC speed (typically 20–80 MB/s)
- TCP throughput limited by 100 Mbps Ethernet or UART speed

---

## Profiling with perf (Linux)

```bash
# Build with debug info
cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo
make

# Profile for 10 seconds
perf record -g ./seeway_interface_daemon daemon.conf &
sleep 10
kill %1
perf report --stdio | head -50
```
