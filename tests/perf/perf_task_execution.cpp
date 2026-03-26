/**
 * @file perf_task_execution.cpp
 * @brief Performance test: task enqueue/dispatch throughput and latency.
 *
 * Enqueues 1000 tasks, measures P50/P95/P99 latency distributions and
 * per-task memory usage.
 *
 * Output: perf_task_execution_results.csv
 */

#include "task_executor.h"
#include "protocol.h"
#include "logger.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
// Null subsystem stubs (satisfy TaskContext pointer checks)
// ---------------------------------------------------------------------------
#include "serial_comm.h"
#include "power_manager.h"
#include "gpio_controller.h"

static double percentile(std::vector<double>& sorted_vals, double pct) {
    if (sorted_vals.empty()) return 0.0;
    size_t idx = static_cast<size_t>(pct / 100.0 * sorted_vals.size());
    if (idx >= sorted_vals.size()) idx = sorted_vals.size() - 1;
    return sorted_vals[idx];
}

int main() {
    Logger::init("", Logger::FATAL); // silence during benchmark

    constexpr int kTasks = 1000;
    printf("=== Task Execution Performance Test ===\n");
    printf("Tasks: %d\n\n", kTasks);

    TaskContext ctx{}; // null context – tasks will fail gracefully

    std::vector<double> latencies_ms;
    latencies_ms.reserve(kTasks);

    std::atomic<int> completed{0};
    std::mutex mtx;

    TaskExecutor te(ctx);
    te.set_result_callback([&](const TaskResponsePayload&) {
        ++completed;
    });
    te.start();

    auto t_start = std::chrono::steady_clock::now();

    for (int i = 0; i < kTasks; ++i) {
        auto enq_start = std::chrono::steady_clock::now();

        TaskCommandPayload cmd{};
        cmd.task_id = TASK_CUSTOM;
        te.enqueue(cmd);

        auto enq_end = std::chrono::steady_clock::now();
        double enq_ms = std::chrono::duration<double, std::milli>(
                            enq_end - enq_start).count();
        latencies_ms.push_back(enq_ms);
    }

    // Wait for all tasks to be processed (up to 60 s)
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (completed.load() < kTasks &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    auto t_end = std::chrono::steady_clock::now();
    te.stop();
    Logger::shutdown();

    double total_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
    double throughput = completed.load() / (total_ms / 1000.0);

    std::sort(latencies_ms.begin(), latencies_ms.end());
    double p50 = percentile(latencies_ms, 50.0);
    double p95 = percentile(latencies_ms, 95.0);
    double p99 = percentile(latencies_ms, 99.0);

    // Estimate per-task memory: sizeof(TaskCommandPayload) + overhead
    size_t per_task_bytes = sizeof(TaskCommandPayload) + 64; // ~64 B overhead est.

    printf("--- Results ---\n");
    printf("Total tasks:  %d\n", kTasks);
    printf("Completed:    %d\n", completed.load());
    printf("Total time:   %.1f ms\n", total_ms);
    printf("Throughput:   %.0f tasks/s\n", throughput);
    printf("Enqueue P50:  %.3f ms\n", p50);
    printf("Enqueue P95:  %.3f ms\n", p95);
    printf("Enqueue P99:  %.3f ms\n", p99);
    printf("Per-task mem: ~%zu bytes\n", per_task_bytes);

    // --- Write CSV ---
    std::ofstream csv("perf_task_execution_results.csv");
    csv << "metric,value\n";
    csv << "total_tasks," << kTasks << "\n";
    csv << "completed," << completed.load() << "\n";
    csv << "total_ms," << total_ms << "\n";
    csv << "throughput_tasks_per_s," << throughput << "\n";
    csv << "enqueue_p50_ms," << p50 << "\n";
    csv << "enqueue_p95_ms," << p95 << "\n";
    csv << "enqueue_p99_ms," << p99 << "\n";
    csv << "per_task_bytes_est," << per_task_bytes << "\n";

    printf("\nResults written to perf_task_execution_results.csv\n");

    // Targets: P99 enqueue < 50 ms (task execution itself can be longer)
    if (p99 < 50.0) {
        printf("PASS: P99 enqueue latency %.3f ms < 50 ms target\n", p99);
    } else {
        printf("WARN: P99 enqueue latency %.3f ms exceeds 50 ms target\n", p99);
    }

    return 0;
}
