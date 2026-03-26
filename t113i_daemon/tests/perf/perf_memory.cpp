/**
 * @file perf_memory.cpp
 * @brief Memory occupation analysis per component.
 *
 * Instantiates each major component, measures resident set size (RSS) before
 * and after construction, and reports per-component memory overhead.
 *
 * Output: console summary + perf_memory.csv
 *
 * Target baselines:
 *  - Logger    < 1 MB overhead
 *  - TaskExecutor per-task: ~128 bytes per queued task
 *  - Total daemon footprint: < 50 MB RSS
 */

#include "logger.h"
#include "connection_monitor.h"
#include "task_executor.h"
#include "config_loader.h"
#include "serial_comm.h"
#include "power_manager.h"
#include "gpio_controller.h"
#include "shutdown_manager.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Read RSS from /proc/self/status (Linux only)
// Returns resident set size in KB.
// ---------------------------------------------------------------------------
static long read_rss_kb() {
    std::ifstream f("/proc/self/status");
    if (!f.is_open()) return -1;
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("VmRSS:", 0) == 0) {
            long kb = 0;
            std::sscanf(line.c_str(), "VmRSS: %ld kB", &kb);
            return kb;
        }
    }
    return -1;
}

struct MemSample {
    std::string component;
    long        rss_before_kb;
    long        rss_after_kb;
    long        delta_kb;
};

static MemSample measure(const std::string& name,
                          std::function<void()> construct)
{
    long before = read_rss_kb();
    construct();
    long after  = read_rss_kb();
    return {name, before, after, after - before};
}

int main() {
    Logger::init("", Logger::FATAL);

    std::vector<MemSample> samples;

    // -----------------------------------------------------------------------
    // Logger
    // -----------------------------------------------------------------------
    {
        auto s = measure("Logger", []() {
            Logger::init("/tmp/perf_mem_test.log", Logger::DEBUG);
            for (int i = 0; i < 1000; ++i) {
                Logger::info("Perf", "memory test message " + std::to_string(i));
            }
            Logger::shutdown();
        });
        samples.push_back(s);
    }

    // -----------------------------------------------------------------------
    // ConnectionMonitor
    // -----------------------------------------------------------------------
    {
        auto s = measure("ConnectionMonitor", []() {
            volatile ConnectionMonitor* cm = new ConnectionMonitor(5000);
            delete cm;
        });
        samples.push_back(s);
    }

    // -----------------------------------------------------------------------
    // TaskExecutor queue (64 tasks × ~128 bytes each = ~8 KB expected)
    // -----------------------------------------------------------------------
    {
        auto s = measure("TaskExecutor_64tasks", []() {
            PowerManager   pm;
            GpioController gpio;
            auto ch  = std::make_unique<TcpClientChannel>("127.0.0.1", 19991, 100);
            SerialComm comm(std::move(ch));
            TaskContext ctx;
            ctx.comm  = &comm;
            ctx.power = &pm;
            ctx.gpio  = &gpio;

            TaskExecutor exec(ctx);
            // Fill queue without starting worker
            for (size_t i = 0; i < TaskExecutor::MAX_QUEUE_DEPTH; ++i) {
                TaskCommandPayload cmd{};
                cmd.task_id = TASK_CUSTOM;
                exec.enqueue(cmd);
            }
            // exec destructor stops the worker
        });
        samples.push_back(s);
    }

    // -----------------------------------------------------------------------
    // GpioController
    // -----------------------------------------------------------------------
    {
        auto s = measure("GpioController", []() {
            GpioController gpio;
            for (int i = 0; i < 32; ++i) {
                gpio.add_pin({0, i, i + 10, true, false, "pin" + std::to_string(i)});
            }
        });
        samples.push_back(s);
    }

    // -----------------------------------------------------------------------
    // ShutdownManager
    // -----------------------------------------------------------------------
    {
        auto s = measure("ShutdownManager", []() {
            ShutdownManager sm;
            for (int i = 0; i < 10; ++i) {
                sm.add_step("step" + std::to_string(i), [](){});
            }
        });
        samples.push_back(s);
    }

    // -----------------------------------------------------------------------
    // Print results
    // -----------------------------------------------------------------------
    long total_rss = read_rss_kb();

    std::printf("\n=== Memory Occupation Analysis ===\n");
    std::printf("%-30s %12s %12s %12s\n",
                "component", "before_kb", "after_kb", "delta_kb");
    for (const auto& s : samples) {
        std::printf("%-30s %12ld %12ld %12ld\n",
                    s.component.c_str(),
                    s.rss_before_kb, s.rss_after_kb, s.delta_kb);
    }
    std::printf("\nTotal process RSS: %ld KB (%.1f MB)  (target <50 MB)\n",
                total_rss, total_rss / 1024.0);

    // -----------------------------------------------------------------------
    // CSV output
    // -----------------------------------------------------------------------
    std::ofstream csv("perf_memory.csv");
    csv << "component,rss_before_kb,rss_after_kb,delta_kb\n";
    for (const auto& s : samples) {
        csv << s.component << "," << s.rss_before_kb << ","
            << s.rss_after_kb << "," << s.delta_kb << "\n";
    }
    csv << "total_rss_kb," << total_rss << ",," << "\n";
    std::cout << "[perf_memory] Results written to perf_memory.csv\n";

    // Pass criteria
    bool pass = (total_rss > 0 && total_rss < 51200); // < 50 MB
    std::cout << (pass ? "PASS" : "WARN") << ": Total RSS < 50 MB\n";

    Logger::shutdown();
    return 0;
}
