/**
 * @file gpio_controller.cpp
 * @brief GpioController implementation using Linux sysfs GPIO and PWM.
 */

#include "gpio_controller.h"
#include "logger.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <poll.h>
#include <thread>

// ---------------------------------------------------------------------------
// Sysfs helpers
// ---------------------------------------------------------------------------

bool GpioController::sysfs_write(const std::string& path,
                                  const std::string& value)
{
    std::ofstream f(path);
    if (!f.is_open()) return false;
    f << value;
    return f.good();
}

bool GpioController::sysfs_read(const std::string& path, std::string& out) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    std::getline(f, out);
    return true;
}

// ---------------------------------------------------------------------------
// GPIO export / unexport
// ---------------------------------------------------------------------------

bool GpioController::export_pin(GpioPin& pin) {
    std::string val_path = "/sys/class/gpio/gpio" +
                           std::to_string(pin.linux_num) + "/value";

    // Export if not already exported
    std::ifstream test(val_path);
    if (!test.is_open()) {
        if (!sysfs_write("/sys/class/gpio/export",
                          std::to_string(pin.linux_num))) {
            Logger::warn("GpioCtrl",
                "Failed to export GPIO " + std::to_string(pin.linux_num));
            return false;
        }
        // Allow kernel time to create sysfs files
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    std::string dir_path = "/sys/class/gpio/gpio" +
                           std::to_string(pin.linux_num) + "/direction";
    std::string dir_val = pin.is_output ? "out" : "in";
    if (!sysfs_write(dir_path, dir_val)) {
        Logger::warn("GpioCtrl",
            "Failed to set direction for GPIO " + std::to_string(pin.linux_num));
        return false;
    }

    if (!pin.is_output) {
        // Set edge detection for input pins
        std::string edge_path = "/sys/class/gpio/gpio" +
                                std::to_string(pin.linux_num) + "/edge";
        sysfs_write(edge_path, "both");
    }

    pin.exported = true;
    return true;
}

bool GpioController::export_pwm(PwmChannel& ch) {
    std::string export_path = ch.chip_path + "/export";
    if (!sysfs_write(export_path, std::to_string(ch.channel))) {
        Logger::warn("GpioCtrl",
            "Failed to export PWM " + std::to_string(ch.channel));
        return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ch.exported = true;
    return true;
}

// ---------------------------------------------------------------------------
// GpioController public API
// ---------------------------------------------------------------------------

void GpioController::add_pin(const GpioPin& pin) {
    std::lock_guard<std::mutex> lk(mutex_);
    pins_.push_back(pin);
}

void GpioController::add_pwm_channel(const PwmChannel& ch) {
    std::lock_guard<std::mutex> lk(mutex_);
    pwm_channels_.push_back(ch);
}

void GpioController::set_event_callback(EventCb cb) {
    std::lock_guard<std::mutex> lk(mutex_);
    event_cb_ = std::move(cb);
}

bool GpioController::start() {
    std::lock_guard<std::mutex> lk(mutex_);
    for (auto& pin : pins_) {
        if (!export_pin(pin)) {
            Logger::warn("GpioCtrl",
                "Could not fully initialize GPIO " + pin.label +
                " (num=" + std::to_string(pin.linux_num) + ")");
        }
    }
    for (auto& ch : pwm_channels_) {
        export_pwm(ch);
    }

    running_ = true;
    poll_thread_ = std::thread(&GpioController::poll_inputs, this);
    Logger::info("GpioCtrl", "Started with " + std::to_string(pins_.size()) +
                              " pin(s)");
    return true;
}

void GpioController::stop() {
    running_ = false;
    if (poll_thread_.joinable()) poll_thread_.join();
}

bool GpioController::set_pin_state(GpioPin& pin, bool state) {
    std::string path = "/sys/class/gpio/gpio" +
                       std::to_string(pin.linux_num) + "/value";
    bool physical = pin.active_low ? !state : state;
    bool ok = sysfs_write(path, physical ? "1" : "0");
    if (ok) pin.current_state = state;
    return ok;
}

bool GpioController::read_pin_state(const GpioPin& pin) const {
    std::string path = "/sys/class/gpio/gpio" +
                       std::to_string(pin.linux_num) + "/value";
    std::string val;
    if (!sysfs_read(path, val)) return false;
    bool physical = (val == "1");
    return pin.active_low ? !physical : physical;
}

bool GpioController::apply_do_command(const DoCommandPayload& cmd) {
    std::lock_guard<std::mutex> lk(mutex_);
    bool all_ok = true;
    for (auto& pin : pins_) {
        if (pin.bank != cmd.bank) continue;
        if (!(cmd.pin_mask & (1u << pin.pin))) continue;
        bool desired = (cmd.pin_states >> pin.pin) & 1u;
        if (!set_pin_state(pin, desired)) {
            Logger::warn("GpioCtrl",
                "Failed to set GPIO " + pin.label + " to " +
                std::to_string(desired));
            all_ok = false;
        }
    }
    return all_ok;
}

bool GpioController::apply_pwm_command(const PwmCommandPayload& cmd) {
    std::lock_guard<std::mutex> lk(mutex_);
    for (auto& ch : pwm_channels_) {
        if (ch.channel != cmd.channel) continue;
        if (!ch.exported) return false;

        std::string base = ch.chip_path + "/pwm" + std::to_string(ch.channel);

        // Set period (in ns)
        uint32_t period_ns = (cmd.frequency_hz > 0)
            ? 1000000000u / cmd.frequency_hz : 1000000u;
        sysfs_write(base + "/period", std::to_string(period_ns));

        uint32_t duty_ns = period_ns * cmd.duty_per_mil / 1000;
        sysfs_write(base + "/duty_cycle", std::to_string(duty_ns));

        sysfs_write(base + "/enable", cmd.enable ? "1" : "0");

        ch.enabled   = cmd.enable != 0;
        ch.period_ns = period_ns;
        ch.duty_ns   = duty_ns;
        return true;
    }
    return false;
}

GpioStatusPayload GpioController::snapshot() const {
    std::lock_guard<std::mutex> lk(mutex_);
    GpioStatusPayload st{};
    st.timestamp_ms = 0; // caller fills if needed
    for (const auto& pin : pins_) {
        if (pin.bank >= GPIO_BANK_COUNT) continue;
        bool state = pin.is_output ? pin.current_state : read_pin_state(pin);
        if (pin.is_output)
            st.output_states[pin.bank] |= (state ? 1u : 0u) << pin.pin;
        else
            st.input_states[pin.bank]  |= (state ? 1u : 0u) << pin.pin;
    }
    return st;
}

void GpioController::poll_inputs() {
    while (running_) {
        {
            std::lock_guard<std::mutex> lk(mutex_);
            if (event_cb_) {
                for (auto& pin : pins_) {
                    if (pin.is_output) continue;
                    bool cur = read_pin_state(pin);
                    if (cur != pin.current_state) {
                        EventPayload evt{};
                        evt.type  = cur ? EVT_GPIO_RISING : EVT_GPIO_FALLING;
                        evt.code  = static_cast<uint16_t>(pin.linux_num);
                        evt.value = cur ? 1 : 0;
                        event_cb_(evt);
                        pin.current_state = cur;
                    }
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}
