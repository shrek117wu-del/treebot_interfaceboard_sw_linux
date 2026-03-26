#pragma once
/**
 * @file shutdown_manager.h
 * @brief Graceful shutdown with dependency ordering and pending-task draining.
 *
 * Addresses P2 issue: shutdown sequence had no dependency analysis and did
 * not wait for pending tasks to complete.
 *
 * Shutdown order:
 *  1. Stop accepting new input commands (InputHandler)
 *  2. Drain in-flight tasks (TaskExecutor queue)
 *  3. Flush pending outbound messages (SerialComm)
 *  4. Stop remaining modules in reverse startup order
 */

#include "logger.h"

#include <chrono>
#include <functional>
#include <string>
#include <thread>
#include <vector>

class ShutdownManager {
public:
    // A named shutdown step: name + function to call.
    struct Step {
        std::string           name;
        std::function<void()> action;
    };

    // Add a shutdown step (appended in order; executed in order).
    void add_step(const std::string& name, std::function<void()> action);

    // Execute all steps sequentially, logging each one.
    void execute();

    // Execute with a drain step that blocks until predicate returns true
    // or max_wait_ms elapses.
    void drain_then_execute(const std::string& drain_name,
                            std::function<bool()> drain_predicate,
                            int max_wait_ms = 10000);

private:
    std::vector<Step> steps_;
};
