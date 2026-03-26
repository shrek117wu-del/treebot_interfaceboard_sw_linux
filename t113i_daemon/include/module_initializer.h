#pragma once
/**
 * @file module_initializer.h
 * @brief Retry-with-backoff helper for module start() calls.
 *
 * Addresses P0 issue: module initialization failures only printed a warning.
 * ModuleInitializer::start_with_retry attempts up to MAX_RETRIES times with
 * exponential backoff before giving up.
 */

#include "logger.h"

#include <chrono>
#include <string>
#include <thread>

class ModuleInitializer {
public:
    static constexpr int MAX_RETRIES      = 3;
    static constexpr int INITIAL_DELAY_MS = 500;

    // Start a module with retry and exponential backoff.
    // Module must expose a bool start() method.
    // Returns true if start() succeeded within MAX_RETRIES attempts.
    template<typename Module>
    static bool start_with_retry(Module& mod, const std::string& name) {
        int delay_ms = INITIAL_DELAY_MS;
        for (int attempt = 1; attempt <= MAX_RETRIES; ++attempt) {
            if (mod.start()) {
                Logger::info("ModuleInit", name + " started successfully");
                return true;
            }
            if (attempt < MAX_RETRIES) {
                Logger::warn("ModuleInit",
                    name + " start failed (attempt " +
                    std::to_string(attempt) + "/" + std::to_string(MAX_RETRIES) +
                    "), retrying in " + std::to_string(delay_ms) + " ms");
                std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
                delay_ms *= 2; // exponential backoff
            } else {
                Logger::error("ModuleInit",
                    name + " failed to start after " +
                    std::to_string(MAX_RETRIES) + " attempts");
            }
        }
        return false;
    }
};
