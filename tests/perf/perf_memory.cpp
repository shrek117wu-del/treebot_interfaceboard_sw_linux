/**
 * @file perf_memory.cpp
 * @brief Performance test: per-component memory profiling and stability.
 *
 * Measures RSS memory before and after creating key components, estimating
 * baseline memory footprint.  Also samples growth over a short stability run.
 *
 * Output: perf_memory_results.csv
 */

#include "logger.h"
#include "connection_monitor.h"
#include "shutdown_manager.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>

// ---------------------------------------------------------------------------
// Read the current process RSS in KB from /proc/self/status
// ---------------------------------------------------------------------------
static long read_rss_kb() {
    FILE* f = fopen("/proc/self/status", "r");
    if (!f) return -1;
    long rss = -1;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "VmRSS: %ld kB", &rss) == 1) break;
    }
    fclose(f);
    return rss;
}

int main() {
    printf("=== Memory Performance Test ===\n");

    long baseline_kb = read_rss_kb();
    printf("Baseline RSS: %ld kB\n", baseline_kb);

    // --- Logger ---
    long before_logger = read_rss_kb();
    Logger::init("", Logger::FATAL);
    long after_logger = read_rss_kb();
    long logger_delta = after_logger - before_logger;
    printf("Logger RSS delta: %ld kB\n", logger_delta);

    // --- ConnectionMonitor ---
    long before_cm = read_rss_kb();
    {
        ConnectionMonitor cm(5000);
        (void)cm.state();
        long after_cm = read_rss_kb();
        printf("ConnectionMonitor RSS delta: %ld kB\n", after_cm - before_cm);
    }

    // --- ShutdownManager ---
    long before_sm = read_rss_kb();
    {
        ShutdownManager sm;
        sm.add_step("noop", [] {});
        long after_sm = read_rss_kb();
        printf("ShutdownManager RSS delta: %ld kB\n", after_sm - before_sm);
    }

    // --- Stability run: sample RSS every second for 5 seconds ---
    printf("\nStability sampling (5 s)...\n");
    std::vector<long> samples;
    for (int i = 0; i < 5; ++i) {
        samples.push_back(read_rss_kb());
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    long growth = samples.back() - samples.front();
    printf("RSS over 5 s: %ld → %ld kB  (growth: %ld kB)\n",
           samples.front(), samples.back(), growth);

    Logger::shutdown();

    // --- Write CSV ---
    std::ofstream csv("perf_memory_results.csv");
    csv << "component,rss_delta_kb\n";
    csv << "baseline," << baseline_kb << "\n";
    csv << "logger," << logger_delta << "\n";
    csv << "stability_growth_5s," << growth << "\n";

    for (size_t i = 0; i < samples.size(); ++i) {
        csv << "sample_" << i << "s," << samples[i] << "\n";
    }

    printf("Results written to perf_memory_results.csv\n");

    // Target: total memory < 30 MB (30720 kB)
    if (samples.back() < 30720) {
        printf("PASS: RSS %ld kB < 30 MB target\n", samples.back());
    } else {
        printf("WARN: RSS %ld kB exceeds 30 MB target\n", samples.back());
    }

    return 0;
}
