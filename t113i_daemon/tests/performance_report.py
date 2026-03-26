#!/usr/bin/env python3
"""
performance_report.py – Aggregate performance test CSV files into a
consolidated report and optional comparison against baselines.

Usage:
    python3 tests/performance_report.py [--dir BUILD_DIR] [--baseline FILE]
                                        [--output OUTPUT.csv]

CSV files consumed:
    perf_main_loop.csv
    perf_task_execution.csv
    perf_memory.csv
    perf_logging.csv

Baselines (defined in this script or loaded from a JSON file):
    CPU reduction        >= 70%
    Task P50             < 5 ms
    Task P95             < 20 ms
    Task P99             < 50 ms
    Logger throughput    > 10,000 msg/sec
    Total RSS            < 50 MB (51200 KB)
"""

import argparse
import csv
import json
import os
import sys
from pathlib import Path
from typing import Dict, Optional, Tuple

# ---------------------------------------------------------------------------
# Baseline thresholds
# ---------------------------------------------------------------------------
DEFAULT_BASELINES = {
    "cpu_reduction_pct":         {"min": 70.0,    "unit": "%"},
    "task_p50_ms":               {"max": 5.0,     "unit": "ms"},
    "task_p95_ms":               {"max": 20.0,    "unit": "ms"},
    "task_p99_ms":               {"max": 50.0,    "unit": "ms"},
    "logger_throughput_per_sec": {"min": 10000.0, "unit": "msg/s"},
    "total_rss_kb":              {"max": 51200.0,  "unit": "KB"},
}


# ---------------------------------------------------------------------------
# CSV reader helpers
# ---------------------------------------------------------------------------
def read_kv_csv(path: Path) -> Dict[str, float]:
    """Read metric,value CSV into a dict."""
    result = {}
    if not path.exists():
        return result
    with open(path) as f:
        reader = csv.reader(f)
        for row in reader:
            if len(row) == 2:
                try:
                    result[row[0].strip()] = float(row[1].strip())
                except ValueError:
                    pass
    return result


# ---------------------------------------------------------------------------
# Collect metrics
# ---------------------------------------------------------------------------
def collect_metrics(build_dir: Path) -> Dict[str, Optional[float]]:
    metrics: Dict[str, Optional[float]] = {}

    # ---- perf_main_loop.csv ----
    ml = read_kv_csv(build_dir / "perf_main_loop.csv")
    cpu_100 = ml.get("100ms_poll")  # actually stored as csv rows; skip for now
    # The main_loop CSV has scenario,wall_ms,cpu_ms,cpu_wall_pct rows
    main_loop_rows = {}
    path = build_dir / "perf_main_loop.csv"
    if path.exists():
        with open(path) as f:
            reader = csv.DictReader(f)
            for row in reader:
                main_loop_rows[row.get("scenario", "")] = row
    cpu_100_ms = float(main_loop_rows.get("100ms_poll", {}).get("cpu_ms", 0) or 0)
    cpu_500_ms = float(main_loop_rows.get("500ms_poll", {}).get("cpu_ms", 0) or 0)
    if cpu_100_ms > 0:
        metrics["cpu_reduction_pct"] = (1.0 - cpu_500_ms / cpu_100_ms) * 100.0
    else:
        metrics["cpu_reduction_pct"] = None

    # ---- perf_task_execution.csv ----
    task = read_kv_csv(build_dir / "perf_task_execution.csv")
    metrics["task_p50_ms"]   = task.get("latency_p50_ms")
    metrics["task_p95_ms"]   = task.get("latency_p95_ms")
    metrics["task_p99_ms"]   = task.get("latency_p99_ms")
    metrics["task_throughput_per_sec"] = task.get("throughput_per_sec")

    # ---- perf_logging.csv ----
    log = read_kv_csv(build_dir / "perf_logging.csv")
    metrics["logger_throughput_per_sec"] = log.get("throughput_per_sec")
    metrics["logger_p99_us"]             = log.get("latency_p99_us")

    # ---- perf_memory.csv ----
    mem = read_kv_csv(build_dir / "perf_memory.csv")
    metrics["total_rss_kb"] = mem.get("total_rss_kb")

    return metrics


# ---------------------------------------------------------------------------
# Check against baselines
# ---------------------------------------------------------------------------
def check_baselines(
    metrics: Dict[str, Optional[float]],
    baselines: Dict
) -> Tuple[int, int]:
    passed = failed = 0
    print("\n{:<35} {:>12} {:>12} {:>8}".format(
        "Metric", "Measured", "Threshold", "Result"))
    print("-" * 72)

    for key, threshold in baselines.items():
        value = metrics.get(key)
        unit  = threshold.get("unit", "")

        if value is None:
            print(f"  {'[SKIP]':<6} {key:<30} {'N/A':>12}")
            continue

        if "min" in threshold:
            ok = value >= threshold["min"]
            thr_str = f">= {threshold['min']} {unit}"
        else:
            ok = value <= threshold["max"]
            thr_str = f"<= {threshold['max']} {unit}"

        status = "PASS" if ok else "FAIL"
        if ok:
            passed += 1
        else:
            failed += 1

        print(f"  [{status}] {key:<30} {value:>10.2f} {unit}  {thr_str}")

    return passed, failed


# ---------------------------------------------------------------------------
# Write summary CSV
# ---------------------------------------------------------------------------
def write_summary(path: Path, metrics: Dict[str, Optional[float]]):
    with open(path, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["metric", "value"])
        for k, v in metrics.items():
            writer.writerow([k, "" if v is None else f"{v:.4f}"])
    print(f"\nSummary written to: {path}")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(
        description="Aggregate performance benchmark results")
    parser.add_argument("--dir", default=".", help="Build directory with CSV files")
    parser.add_argument("--baseline", help="JSON file with custom baseline thresholds")
    parser.add_argument("--output", default="perf_summary.csv", help="Output CSV")
    args = parser.parse_args()

    build_dir = Path(args.dir)

    baselines = DEFAULT_BASELINES.copy()
    if args.baseline:
        with open(args.baseline) as f:
            baselines.update(json.load(f))

    print("=" * 72)
    print(" Seeway Interface Daemon – Performance Report")
    print("=" * 72)
    print(f"  Source: {build_dir}")

    metrics = collect_metrics(build_dir)

    passed, failed = check_baselines(metrics, baselines)

    write_summary(build_dir / args.output, metrics)

    print("\n" + "=" * 72)
    print(f"  Total: {passed} PASSED, {failed} FAILED")
    print("=" * 72)

    return 1 if failed > 0 else 0


if __name__ == "__main__":
    sys.exit(main())
