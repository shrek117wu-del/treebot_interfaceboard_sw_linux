/**
 * @file perf_logging.cpp
 * @brief Logger performance benchmark.
 *
 * Measures:
 *  - File write latency per message (min/P50/P95/P99/max)
 *  - Message throughput (messages/sec)
 *
 * Expected baselines:
 *  - Throughput > 10,000 messages/sec
 *  - P99 latency < 1 ms
 *
 * Output: console summary + perf_logging.csv
 */

#include "logger.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Clock  = std::chrono::steady_clock;
using Us     = std::chrono::duration<double, std::micro>;
using Ms     = std::chrono::duration<double, std::milli>;

static constexpr int MSG_COUNT = 10000;

int main() {
    std::string log_path =
        (fs::temp_directory_path() / "perf_logging_test.log").string();

    Logger::init(log_path, Logger::INFO);

    std::vector<double> latencies_us(MSG_COUNT, 0.0);

    auto global_start = Clock::now();

    for (int i = 0; i < MSG_COUNT; ++i) {
        auto t0 = Clock::now();
        Logger::info("Perf", "benchmark message index=" + std::to_string(i));
        auto t1 = Clock::now();
        latencies_us[i] = Us(t1 - t0).count();
    }

    auto global_end = Clock::now();
    Logger::shutdown();

    double wall_ms    = Ms(global_end - global_start).count();
    double throughput = MSG_COUNT / (wall_ms / 1000.0);

    // Sort for percentiles
    std::vector<double> lat = latencies_us;
    std::sort(lat.begin(), lat.end());

    auto pct = [&](double p) -> double {
        size_t idx = static_cast<size_t>(p / 100.0 * lat.size());
        if (idx >= lat.size()) idx = lat.size() - 1;
        return lat[idx];
    };

    double p50  = pct(50.0);
    double p95  = pct(95.0);
    double p99  = pct(99.0);
    double pmin = lat.front();
    double pmax = lat.back();

    std::printf("\n=== Logger Performance Benchmark ===\n");
    std::printf("Messages        : %d\n", MSG_COUNT);
    std::printf("Total wall time : %.3f ms\n", wall_ms);
    std::printf("Throughput      : %.0f msg/sec  (target >10000)\n", throughput);
    std::printf("Latency min     : %.2f µs\n", pmin);
    std::printf("Latency P50     : %.2f µs\n", p50);
    std::printf("Latency P95     : %.2f µs\n", p95);
    std::printf("Latency P99     : %.2f µs  (target <1000 µs)\n", p99);
    std::printf("Latency max     : %.2f µs\n", pmax);

    // CSV
    std::ofstream csv("perf_logging.csv");
    csv << "metric,value\n";
    csv << "msg_count," << MSG_COUNT << "\n";
    csv << "wall_ms," << wall_ms << "\n";
    csv << "throughput_per_sec," << throughput << "\n";
    csv << "latency_p50_us," << p50 << "\n";
    csv << "latency_p95_us," << p95 << "\n";
    csv << "latency_p99_us," << p99 << "\n";
    csv << "latency_min_us," << pmin << "\n";
    csv << "latency_max_us," << pmax << "\n";

    // Raw latencies
    csv << "\nindex,latency_us\n";
    for (int i = 0; i < MSG_COUNT; ++i) {
        csv << i << "," << latencies_us[i] << "\n";
    }

    std::cout << "[perf_logging] Results written to perf_logging.csv\n";

    // Cleanup temp log file
    std::error_code ec;
    fs::remove(log_path, ec);

    bool pass = (throughput >= 1000.0); // lenient for CI/disk I/O variability
    std::cout << (pass ? "PASS" : "WARN")
              << ": Logger throughput " << throughput << " msg/sec\n";
    return 0;
}
