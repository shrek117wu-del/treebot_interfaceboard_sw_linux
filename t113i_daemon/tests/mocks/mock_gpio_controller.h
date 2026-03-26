#pragma once
/**
 * @file mock_gpio_controller.h
 * @brief Minimal stub for GpioController used in unit and integration tests.
 *
 * Overrides sysfs I/O to avoid touching the real filesystem.
 * Tracks command calls for assertion in tests.
 */

#include "protocol.h"

#include <gmock/gmock.h>

#include <atomic>
#include <mutex>
#include <vector>

// ---------------------------------------------------------------------------
// MockGpioController – records apply_do_command / apply_pwm_command calls
// ---------------------------------------------------------------------------
class MockGpioController {
public:
    MockGpioController() = default;

    bool start() { return true; }
    void stop()  {}

    bool apply_do_command(const DoCommandPayload& cmd) {
        std::lock_guard<std::mutex> lk(mutex_);
        do_commands_.push_back(cmd);
        return do_return_value_.load();
    }

    bool apply_pwm_command(const PwmCommandPayload& cmd) {
        std::lock_guard<std::mutex> lk(mutex_);
        pwm_commands_.push_back(cmd);
        return true;
    }

    GpioStatusPayload snapshot() const {
        GpioStatusPayload s{};
        return s;
    }

    // -----------------------------------------------------------------------
    // Test helpers
    // -----------------------------------------------------------------------

    // Set the return value for apply_do_command
    void set_do_return(bool value) { do_return_value_ = value; }

    // Number of times apply_do_command was called
    int do_command_count() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return static_cast<int>(do_commands_.size());
    }

    // Last DoCommandPayload received
    DoCommandPayload last_do_command() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return do_commands_.empty() ? DoCommandPayload{} : do_commands_.back();
    }

    void clear() {
        std::lock_guard<std::mutex> lk(mutex_);
        do_commands_.clear();
        pwm_commands_.clear();
    }

private:
    mutable std::mutex           mutex_;
    std::vector<DoCommandPayload>  do_commands_;
    std::vector<PwmCommandPayload> pwm_commands_;
    std::atomic<bool>            do_return_value_{true};
};
