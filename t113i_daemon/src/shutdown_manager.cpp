/**
 * @file shutdown_manager.cpp
 * @brief ShutdownManager implementation.
 */

#include "shutdown_manager.h"

void ShutdownManager::add_step(const std::string& name,
                                std::function<void()> action)
{
    steps_.push_back({name, std::move(action)});
}

void ShutdownManager::execute() {
    Logger::info("Shutdown", "Starting graceful shutdown...");
    for (auto& step : steps_) {
        Logger::info("Shutdown", "Step: " + step.name);
        try {
            step.action();
        } catch (const std::exception& e) {
            Logger::error("Shutdown",
                step.name + " threw exception: " + e.what());
        }
    }
    Logger::info("Shutdown", "Shutdown complete");
}

void ShutdownManager::drain_then_execute(
    const std::string& drain_name,
    std::function<bool()> drain_predicate,
    int max_wait_ms)
{
    Logger::info("Shutdown",
        "Draining: " + drain_name + " (max " +
        std::to_string(max_wait_ms) + " ms)");

    int elapsed = 0;
    const int poll_ms = 100;
    while (elapsed < max_wait_ms) {
        if (drain_predicate()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(poll_ms));
        elapsed += poll_ms;
    }
    if (elapsed >= max_wait_ms && !drain_predicate()) {
        Logger::warn("Shutdown",
            drain_name + " did not drain within " +
            std::to_string(max_wait_ms) + " ms");
    }

    execute();
}
