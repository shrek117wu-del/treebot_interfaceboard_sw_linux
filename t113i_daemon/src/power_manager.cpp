/**
 * @file power_manager.cpp
 * @brief PowerManager implementation – GPIO-backed power rail control.
 */

#include "power_manager.h"
#include "logger.h"
#include "utils.h"

#include <chrono>
#include <fstream>
#include <thread>

// ---------------------------------------------------------------------------
// Sysfs helper
// ---------------------------------------------------------------------------

bool PowerManager::sysfs_write(const std::string& path,
                                const std::string& value)
{
    std::ofstream f(path);
    if (!f.is_open()) return false;
    f << value;
    return f.good();
}

bool PowerManager::gpio_write(int gpio_num, bool active_low, bool logical_on) {
    bool physical = active_low ? !logical_on : logical_on;
    std::string path = "/sys/class/gpio/gpio" +
                       std::to_string(gpio_num) + "/value";
    return sysfs_write(path, physical ? "1" : "0");
}

// ---------------------------------------------------------------------------
// PowerManager public API
// ---------------------------------------------------------------------------

void PowerManager::add_rail(const PowerRail& rail) {
    std::lock_guard<std::mutex> lk(mutex_);
    rails_.push_back(rail);
}

bool PowerManager::start() {
    Logger::info("PowerManager",
        "Initialized with " + std::to_string(rails_.size()) + " rail(s)");
    return true;
}

void PowerManager::stop() {
    Logger::info("PowerManager", "Stopped");
}

bool PowerManager::set_rail(PowerRail& rail, bool on) {
    using namespace std::chrono;

    if (on && rail.is_on) {
        Logger::debug("PowerManager", "Rail already on, skipping");
        return true;
    }
    if (!on && !rail.is_on) {
        Logger::debug("PowerManager", "Rail already off, skipping");
        return true;
    }

    // Enforce minimum on-time
    if (!on && rail.is_on && rail.min_on_ms > 0) {
        auto elapsed = duration_cast<milliseconds>(
            steady_clock::now() - rail.turn_on_time).count();
        if (static_cast<uint32_t>(elapsed) < rail.min_on_ms) {
            Logger::warn("PowerManager",
                "Min-on-time not elapsed (" + std::to_string(elapsed) +
                " ms / " + std::to_string(rail.min_on_ms) + " ms)");
            return false;
        }
    }

    // Sequence delay before turning on
    if (on && rail.sequence_delay_ms > 0) {
        std::this_thread::sleep_for(milliseconds(rail.sequence_delay_ms));
    }

    bool ok = gpio_write(rail.gpio_num, rail.active_low, on);
    if (ok) {
        rail.is_on = on;
        if (on) rail.turn_on_time = steady_clock::now();
    } else {
        Logger::error("PowerManager",
            "Failed to set GPIO " + std::to_string(rail.gpio_num));
    }
    return ok;
}

bool PowerManager::apply_power_command(const PowerCommandPayload& cmd) {
    std::lock_guard<std::mutex> lk(mutex_);

    // Handle PWR_ALL_OFF as a special broadcast
    if (cmd.command == PWR_ALL_OFF) {
        bool all_ok = true;
        for (auto& rail : rails_) {
            if (!set_rail(rail, false)) all_ok = false;
        }
        return all_ok;
    }

    for (auto& rail : rails_) {
        if (rail.on_cmd_id == cmd.command) {
            return set_rail(rail, true);
        }
        if (rail.off_cmd_id == cmd.command) {
            return set_rail(rail, false);
        }
    }

    Logger::warn("PowerManager",
        "Unknown power command: 0x" + to_hex(cmd.command, 2));
    return false;
}
