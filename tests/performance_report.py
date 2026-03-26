#!/usr/bin/env python3
"""
performance_report.py – Parse performance CSV results and generate a JSON report.

Usage:
    python3 performance_report.py [--build-dir BUILD_DIR] [--output report.json]

Reads:
  - perf_main_loop_results.csv
  - perf_task_execution_results.csv
  - perf_memory_results.csv
  - perf_logging_results.csv

Outputs:
  - JSON report with metrics, thresholds, and pass/fail status
"""

import argparse
import csv
import json
import os
import sys
from datetime import datetime, timezone


# ---------------------------------------------------------------------------
# Performance thresholds
# ---------------------------------------------------------------------------
THRESHOLDS = {
    "main_loop": {
        "cpu_reduction_pct_min": 50.0,
    },
    "task_execution": {
        "enqueue_p99_ms_max": 50.0,
        "throughput_tasks_per_s_min": 100.0,
    },
    "memory": {
        "total_rss_kb_max": 30720,   # 30 MB
        "stability_growth_5s_kb_max": 1024,
    },
    "logging": {
        "throughput_msg_per_s_min": 1000.0,
        "latency_p99_ms_max": 10.0,
    },
}


def read_csv_as_dict(filepath: str) -> dict:
    """Read a key,value CSV file into a dict with float values."""
    result = {}
    if not os.path.exists(filepath):
        return result
    with open(filepath, newline="") as f:
        reader = csv.reader(f)
        next(reader, None)  # skip header
        for row in reader:
            if len(row) >= 2:
                key = row[0].strip()
                try:
                    result[key] = float(row[1].strip())
                except ValueError:
                    result[key] = row[1].strip()
    return result


def check_threshold(value, threshold_key: str, spec: dict) -> dict:
    """Return a pass/fail result for a single metric."""
    result = {"value": value, "threshold_key": threshold_key}
    if threshold_key.endswith("_min"):
        limit = spec[threshold_key]
        result["limit"] = limit
        result["pass"] = value >= limit
        result["comparison"] = ">="
    elif threshold_key.endswith("_max"):
        limit = spec[threshold_key]
        result["limit"] = limit
        result["pass"] = value <= limit
        result["comparison"] = "<="
    else:
        result["pass"] = True
    return result


def analyse_main_loop(build_dir: str) -> dict:
    data = read_csv_as_dict(os.path.join(build_dir, "perf_main_loop_results.csv"))
    report = {"raw": data, "checks": {}}
    if not data:
        report["status"] = "no_data"
        return report

    # Compute CPU reduction %
    load_100 = data.get("100", {})  # may be keyed differently
    # Try to extract from the CSV rows
    rows_100 = {k: v for k, v in data.items() if "100" in k}
    rows_500 = {k: v for k, v in data.items() if "500" in k}

    cpu_load_100 = None
    cpu_load_500 = None
    for k, v in data.items():
        if k == "100":
            cpu_load_100 = v
        elif k == "500":
            cpu_load_500 = v

    if cpu_load_100 and cpu_load_500 and float(cpu_load_100) > 0:
        reduction = (1.0 - float(cpu_load_500) / float(cpu_load_100)) * 100.0
        spec = THRESHOLDS["main_loop"]
        report["cpu_reduction_pct"] = reduction
        report["checks"]["cpu_reduction_pct"] = check_threshold(
            reduction, "cpu_reduction_pct_min", spec)
    report["status"] = "ok"
    return report


def analyse_task_execution(build_dir: str) -> dict:
    data = read_csv_as_dict(os.path.join(build_dir, "perf_task_execution_results.csv"))
    report = {"raw": data, "checks": {}}
    if not data:
        report["status"] = "no_data"
        return report

    spec = THRESHOLDS["task_execution"]
    for key in spec:
        metric = key.replace("_min", "").replace("_max", "")
        if metric in data:
            report["checks"][metric] = check_threshold(
                float(data[metric]), key, spec)

    report["status"] = "ok"
    return report


def analyse_memory(build_dir: str) -> dict:
    data = read_csv_as_dict(os.path.join(build_dir, "perf_memory_results.csv"))
    report = {"raw": data, "checks": {}}
    if not data:
        report["status"] = "no_data"
        return report

    spec = THRESHOLDS["memory"]
    # baseline = most recent RSS sample
    samples = {k: v for k, v in data.items() if k.startswith("sample_")}
    if samples:
        last_sample = max(samples.items(), key=lambda x: x[0])
        report["last_rss_kb"] = last_sample[1]
        report["checks"]["total_rss_kb"] = check_threshold(
            last_sample[1], "total_rss_kb_max", spec)

    if "stability_growth_5s" in data:
        report["checks"]["stability_growth_5s_kb"] = check_threshold(
            data["stability_growth_5s"], "stability_growth_5s_kb_max", spec)

    report["status"] = "ok"
    return report


def analyse_logging(build_dir: str) -> dict:
    data = read_csv_as_dict(os.path.join(build_dir, "perf_logging_results.csv"))
    report = {"raw": data, "checks": {}}
    if not data:
        report["status"] = "no_data"
        return report

    spec = THRESHOLDS["logging"]
    for key in spec:
        metric = key.replace("_min", "").replace("_max", "")
        if metric in data:
            report["checks"][metric] = check_threshold(
                float(data[metric]), key, spec)

    report["status"] = "ok"
    return report


def overall_pass(report: dict) -> bool:
    for section in ["main_loop", "task_execution", "memory", "logging"]:
        sec = report.get(section, {})
        for check in sec.get("checks", {}).values():
            if isinstance(check, dict) and not check.get("pass", True):
                return False
    return True


def main():
    parser = argparse.ArgumentParser(description="Generate performance report")
    parser.add_argument("--build-dir", default=".",
                        help="Directory containing CSV result files")
    parser.add_argument("--output", default="-",
                        help="Output JSON file (- = stdout)")
    args = parser.parse_args()

    report = {
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "build_dir": os.path.abspath(args.build_dir),
        "thresholds": THRESHOLDS,
        "main_loop":       analyse_main_loop(args.build_dir),
        "task_execution":  analyse_task_execution(args.build_dir),
        "memory":          analyse_memory(args.build_dir),
        "logging":         analyse_logging(args.build_dir),
    }

    report["overall_pass"] = overall_pass(report)

    output = json.dumps(report, indent=2)

    if args.output == "-":
        print(output)
    else:
        with open(args.output, "w") as f:
            f.write(output)
        print(f"Report written to {args.output}", file=sys.stderr)

    sys.exit(0 if report["overall_pass"] else 1)


if __name__ == "__main__":
    main()
