/**
 * @file perf_logging.cpp
 * @brief Performance test: Logger write latency and throughput.
 *
 * Measures per-message write latency (P50/P95/P99) and aggregate throughput
 * when logging to a file.
 *
 * Output: perf_logging_results.csv
 */

#include "logger.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static double percentile(std::vector<double>& v, double pct) {
    if (v.empty()) return 0.0;
    size_t idx = static_cast<size_t>(pct / 100.0 * v.size());
    if (idx >= v.size()) idx = v.size() - 1;
    return v[idx];
}

int main() {
    constexpr int kMessages = 10000;
    auto log_path = (fs::temp_directory_path() / "perf_logging_test.log").string();

    printf("=== Logging Performance Test ===\n");
    printf("Messages: %d\n", kMessages);
    printf("Log file: %s\n\n", log_path.c_str());

    Logger::init(log_path, Logger::DEBUG);

    std::vector<double> latencies_us;
    latencies_us.reserve(kMessages);

    auto t_start = std::chrono::steady_clock::now();

    for (int i = 0; i < kMessages; ++i) {
        auto t0 = std::chrono::steady_clock::now();
        Logger::info("PERF", "benchmark message " + std::to_string(i));
        auto t1 = std::chrono::steady_clock::now();
        double us = std::chrono::duration<double, std::micro>(t1 - t0).count();
        latencies_us.push_back(us);
    }

    auto t_end = std::chrono::steady_clock::now();
    Logger::shutdown();

    double total_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
    double throughput = kMessages / (total_ms / 1000.0);

    std::sort(latencies_us.begin(), latencies_us.end());
    double p50 = percentile(latencies_us, 50.0);
    double p95 = percentile(latencies_us, 95.0);
    double p99 = percentile(latencies_us, 99.0);
    double p50_ms = p50 / 1000.0;
    double p95_ms = p95 / 1000.0;
    double p99_ms = p99 / 1000.0;

    printf("--- Results ---\n");
    printf("Total time:   %.1f ms\n", total_ms);
    printf("Throughput:   %.0f msg/s\n", throughput);
    printf("Latency P50:  %.3f ms\n", p50_ms);
    printf("Latency P95:  %.3f ms\n", p95_ms);
    printf("Latency P99:  %.3f ms\n", p99_ms);

    // --- Write CSV ---
    std::ofstream csv("perf_logging_results.csv");
    csv << "metric,value\n";
    csv << "messages," << kMessages << "\n";
    csv << "total_ms," << total_ms << "\n";
    csv << "throughput_msg_per_s," << throughput << "\n";
    csv << "latency_p50_ms," << p50_ms << "\n";
    csv << "latency_p95_ms," << p95_ms << "\n";
    csv << "latency_p99_ms," << p99_ms << "\n";

    printf("Results written to perf_logging_results.csv\n");

    // Clean up temp log
    fs::remove(log_path);

    // Target: throughput > 1000 msg/s (conservative for disk I/O)
    if (throughput > 1000.0) {
        printf("PASS: Throughput %.0f msg/s > 1000 msg/s target\n", throughput);
    } else {
        printf("WARN: Throughput %.0f msg/s below 1000 msg/s target\n", throughput);
    }

    return 0;
}
