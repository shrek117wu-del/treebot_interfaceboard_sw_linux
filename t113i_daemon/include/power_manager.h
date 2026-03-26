#pragma once
/**
 * @file power_manager.h
 * @brief Power rail management for T113i (NX / arm / chassis power control).
 *
 * Each rail is mapped to a GPIO pin; the manager enforces minimum-on-time
 * and sequence delays between rail transitions.
 */

#include "protocol.h"

#include <chrono>
#include <mutex>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Power rail descriptor
// ---------------------------------------------------------------------------
struct PowerRail {
    PowerCmd on_cmd_id;
    PowerCmd off_cmd_id;
    int      gpio_num;
    bool     active_low{false};
    uint32_t min_on_ms{5000};
    uint32_t sequence_delay_ms{1000};

    // Runtime state
    bool     is_on{false};
    std::chrono::steady_clock::time_point turn_on_time{std::chrono::steady_clock::now()};
};

// ---------------------------------------------------------------------------
// PowerManager
// ---------------------------------------------------------------------------
class PowerManager {
public:
    PowerManager()  = default;
    ~PowerManager() = default;

    void add_rail(const PowerRail& rail);

    bool start();
    void stop();

    // Apply a power command received from the wire protocol.
    // Returns false if the command cannot be executed (e.g. min_on_ms not elapsed).
    bool apply_power_command(const PowerCommandPayload& cmd);

private:
    mutable std::mutex    mutex_;
    std::vector<PowerRail> rails_;

    bool set_rail(PowerRail& rail, bool on);
    bool gpio_write(int gpio_num, bool active_low, bool logical_on);
    static bool sysfs_write(const std::string& path, const std::string& value);
};
