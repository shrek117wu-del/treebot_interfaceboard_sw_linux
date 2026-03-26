#pragma once
/**
 * @file gpio_controller.h
 * @brief GPIO and PWM controller for T113i Linux sysfs interface.
 *
 * Manages digital output/input pins via /sys/class/gpio and PWM channels
 * via /sys/class/pwm.  Thread-safe for concurrent command and snapshot calls.
 */

#include "protocol.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
// Pin descriptor
// ---------------------------------------------------------------------------
struct GpioPin {
    int         bank{0};
    int         pin{0};
    int         linux_num{0};   // /sys/class/gpio/gpioN
    bool        is_output{true};
    bool        active_low{false};
    std::string label;

    // Runtime state
    bool        current_state{false};
    bool        exported{false};
};

// ---------------------------------------------------------------------------
// PWM channel descriptor
// ---------------------------------------------------------------------------
struct PwmChannel {
    int         chip{0};
    std::string chip_path;   // e.g. /sys/class/pwm/pwmchip0
    int         channel{0};

    bool        exported{false};
    bool        enabled{false};
    uint32_t    period_ns{0};
    uint32_t    duty_ns{0};
};

// ---------------------------------------------------------------------------
// GpioController
// ---------------------------------------------------------------------------
class GpioController {
public:
    GpioController()  = default;
    ~GpioController() = default;

    void add_pin(const GpioPin& pin);
    void add_pwm_channel(const PwmChannel& ch);

    bool start();
    void stop();

    // Apply a digital output command from the wire protocol.
    bool apply_do_command(const DoCommandPayload& cmd);

    // Apply a PWM command from the wire protocol.
    bool apply_pwm_command(const PwmCommandPayload& cmd);

    // Snapshot current GPIO state for periodic status reporting.
    GpioStatusPayload snapshot() const;

    // Register callback for GPIO input-edge events (rising/falling).
    using EventCb = std::function<void(const EventPayload&)>;
    void set_event_callback(EventCb cb);

private:
    mutable std::mutex       mutex_;
    std::vector<GpioPin>     pins_;
    std::vector<PwmChannel>  pwm_channels_;
    EventCb                  event_cb_;

    std::atomic<bool>        running_{false};
    std::thread              poll_thread_;

    bool export_pin(GpioPin& pin);
    bool export_pwm(PwmChannel& ch);
    bool set_pin_state(GpioPin& pin, bool state);
    bool read_pin_state(const GpioPin& pin) const;
    void poll_inputs();

    static bool sysfs_write(const std::string& path, const std::string& value);
    static bool sysfs_read(const std::string& path, std::string& out);
};
