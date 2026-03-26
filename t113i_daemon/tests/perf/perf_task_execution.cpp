/**
 * @file perf_task_execution.cpp
 * @brief Performance benchmark: task throughput and latency distribution.
 *
 * Enqueues 1000+ tasks and measures:
 *  - Total throughput (tasks/sec)
 *  - Latency percentiles: P50, P95, P99
 *
 * Expected baselines:
 *  - P50 < 5 ms
 *  - P95 < 20 ms
 *  - P99 < 50 ms
 *
 * Output: console summary + perf_task_execution.csv
 */

#include "task_executor.h"
#include "logger.h"
#include "serial_comm.h"
#include "power_manager.h"
#include "gpio_controller.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

using Clock = std::chrono::steady_clock;
using Ms    = std::chrono::duration<double, std::milli>;

static constexpr int TASK_COUNT = 1000;

int main() {
    Logger::init("", Logger::FATAL); // suppress daemon output

    // Build minimal task context
    PowerManager   power;
    GpioController gpio;
    auto ch   = std::make_unique<TcpClientChannel>("127.0.0.1", 19990, 100);
    SerialComm comm(std::move(ch));

    TaskContext ctx;
    ctx.comm  = &comm;
    ctx.power = &power;
    ctx.gpio  = &gpio;

    TaskExecutor exec(ctx);

    // Track per-task latency: issue time → completion time
    std::vector<double>  latencies_ms(TASK_COUNT, 0.0);
    std::vector<Clock::time_point> issue_times(TASK_COUNT);
    std::atomic<int>     completed{0};

    exec.set_result_callback([&](const TaskResponsePayload& resp) {
        auto done = Clock::now();
        (void)resp;
        int i = completed.fetch_add(1);
        if (i < TASK_COUNT) {
            latencies_ms[i] = Ms(done - issue_times[i]).count();
        }
    });

    exec.start();

    // Enqueue all tasks, recording issue time for each
    auto global_start = Clock::now();
    for (int i = 0; i < TASK_COUNT; ++i) {
        TaskCommandPayload cmd{};
        cmd.task_id = TASK_CUSTOM;
        cmd.arg     = static_cast<uint32_t>(i);
        std::snprintf(cmd.name, sizeof(cmd.name), "perf_%d", i);

        issue_times[i] = Clock::now();
        while (!exec.enqueue(cmd)) {
            // Back-pressure: wait for queue to drain a bit
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    // Wait for all tasks to complete (max 30 s)
    for (int w = 0; w < 3000 && completed.load() < TASK_COUNT; ++w) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    auto global_end = Clock::now();
    exec.stop();

    int   done          = completed.load();
    double wall_sec     = Ms(global_end - global_start).count() / 1000.0;
    double throughput   = done / wall_sec;

    // Sort latencies for percentile computation
    std::vector<double> lat(latencies_ms.begin(),
                             latencies_ms.begin() + done);
    std::sort(lat.begin(), lat.end());

    auto pct = [&](double p) -> double {
        if (lat.empty()) return 0.0;
        size_t idx = static_cast<size_t>(p / 100.0 * lat.size());
        if (idx >= lat.size()) idx = lat.size() - 1;
        return lat[idx];
    };

    double p50  = pct(50.0);
    double p95  = pct(95.0);
    double p99  = pct(99.0);
    double pmin = lat.empty() ? 0.0 : lat.front();
    double pmax = lat.empty() ? 0.0 : lat.back();

    std::printf("\n=== Task Execution Performance ===\n");
    std::printf("Tasks completed : %d / %d\n", done, TASK_COUNT);
    std::printf("Wall time       : %.3f s\n", wall_sec);
    std::printf("Throughput      : %.1f tasks/sec\n", throughput);
    std::printf("Latency min     : %.3f ms\n", pmin);
    std::printf("Latency P50     : %.3f ms  (target <5 ms)\n", p50);
    std::printf("Latency P95     : %.3f ms  (target <20 ms)\n", p95);
    std::printf("Latency P99     : %.3f ms  (target <50 ms)\n", p99);
    std::printf("Latency max     : %.3f ms\n", pmax);

    // CSV output
    std::ofstream csv("perf_task_execution.csv");
    csv << "metric,value\n";
    csv << "tasks_completed," << done << "\n";
    csv << "throughput_per_sec," << throughput << "\n";
    csv << "latency_p50_ms," << p50 << "\n";
    csv << "latency_p95_ms," << p95 << "\n";
    csv << "latency_p99_ms," << p99 << "\n";
    csv << "latency_min_ms," << pmin << "\n";
    csv << "latency_max_ms," << pmax << "\n";

    // Also write all raw latencies
    csv << "\nindex,latency_ms\n";
    for (int i = 0; i < static_cast<int>(lat.size()); ++i) {
        csv << i << "," << lat[i] << "\n";
    }
    std::cout << "[perf_task] Results written to perf_task_execution.csv\n";

    Logger::shutdown();
    return (done >= TASK_COUNT) ? 0 : 1;
}
