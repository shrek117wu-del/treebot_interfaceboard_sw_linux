#include "seeway_interface_hardware/seeway_hardware_interface.hpp"

#include <chrono>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

#include "hardware_interface/types/hardware_interface_type_values.hpp"
#include "rclcpp/rclcpp.hpp"

namespace seeway_interface_hardware {

hardware_interface::CallbackReturn SeewayHardwareInterface::on_init(const hardware_interface::HardwareInfo & info) {
    if (hardware_interface::SystemInterface::on_init(info) != hardware_interface::CallbackReturn::SUCCESS) {
        return hardware_interface::CallbackReturn::ERROR;
    }

    // Allocate memory for states/commands
    // 8 ADC channels + temperature + humidity = 10 sensor states
    // 32 digital input pins, 32 digital output commands, 4 PWM commands
    hw_sensor_states_.resize(10, std::numeric_limits<double>::quiet_NaN());
    hw_gpio_inputs_.resize(32, 0.0);
    hw_gpio_cmds_.resize(32, std::numeric_limits<double>::quiet_NaN());
    hw_pwm_cmds_.resize(4, std::numeric_limits<double>::quiet_NaN());
    hw_gpio_cmds_prev_.resize(32, std::numeric_limits<double>::quiet_NaN());
    hw_pwm_cmds_prev_.resize(4, std::numeric_limits<double>::quiet_NaN());

    // Create a local ROS2 node for sub/pubs/clients inside the plugin
    node_ = std::make_shared<rclcpp::Node>("seeway_hardware_interface_node");

    return hardware_interface::CallbackReturn::SUCCESS;
}

hardware_interface::CallbackReturn SeewayHardwareInterface::on_configure(const rclcpp_lifecycle::State &) {
    RCLCPP_INFO(node_->get_logger(), "Configuring SeewayHardwareInterface...");

    // Setup Subscribers
    sensor_sub_ = node_->create_subscription<seeway_interface_msgs::msg::SensorData>(
        "seeway/sensor_data", 10,
        std::bind(&SeewayHardwareInterface::on_sensor_data, this, std::placeholders::_1));
    gpio_sub_ = node_->create_subscription<seeway_interface_msgs::msg::GpioStatus>(
        "seeway/gpio_status", 10,
        std::bind(&SeewayHardwareInterface::on_gpio_status, this, std::placeholders::_1));

    // Setup Service Clients
    cli_set_gpio_ = node_->create_client<seeway_interface_msgs::srv::SetGpio>("seeway/set_gpio");
    cli_set_pwm_ = node_->create_client<seeway_interface_msgs::srv::SetPwm>("seeway/set_pwm");

    return hardware_interface::CallbackReturn::SUCCESS;
}

std::vector<hardware_interface::StateInterface> SeewayHardwareInterface::export_state_interfaces() {
    std::vector<hardware_interface::StateInterface> state_interfaces;

    // Sensor states: <hardware_name>/adc_0 .. adc_7, temperature, humidity
    for (size_t i = 0; i < 8; ++i) {
        state_interfaces.emplace_back(hardware_interface::StateInterface(
            info_.name, "adc_" + std::to_string(i), &hw_sensor_states_[i]));
    }
    state_interfaces.emplace_back(hardware_interface::StateInterface(
        info_.name, "temperature", &hw_sensor_states_[8]));
    state_interfaces.emplace_back(hardware_interface::StateInterface(
        info_.name, "humidity", &hw_sensor_states_[9]));

    // Digital input states: <hardware_name>/gpio_in_0 .. gpio_in_31
    for (size_t i = 0; i < 32; ++i) {
        state_interfaces.emplace_back(hardware_interface::StateInterface(
            info_.name, "gpio_in_" + std::to_string(i), &hw_gpio_inputs_[i]));
    }

    return state_interfaces;
}

std::vector<hardware_interface::CommandInterface> SeewayHardwareInterface::export_command_interfaces() {
    std::vector<hardware_interface::CommandInterface> command_interfaces;

    // Digital output commands: <hardware_name>/gpio_out_0 .. gpio_out_31
    for (size_t i = 0; i < 32; ++i) {
        command_interfaces.emplace_back(hardware_interface::CommandInterface(
            info_.name, "gpio_out_" + std::to_string(i), &hw_gpio_cmds_[i]));
    }

    // PWM commands: <hardware_name>/pwm_out_0 .. pwm_out_3  (duty cycle in per-mille, 0-1000)
    for (size_t i = 0; i < 4; ++i) {
        command_interfaces.emplace_back(hardware_interface::CommandInterface(
            info_.name, "pwm_out_" + std::to_string(i), &hw_pwm_cmds_[i]));
    }

    return command_interfaces;
}

hardware_interface::CallbackReturn SeewayHardwareInterface::on_activate(const rclcpp_lifecycle::State &) {
    RCLCPP_INFO(node_->get_logger(), "Activating SeewayHardwareInterface...");

    // Start a dedicated executor thread so ROS callbacks are processed
    // independently from the real-time control loop thread.
    executor_ = std::make_unique<rclcpp::executors::SingleThreadedExecutor>();
    executor_->add_node(node_);
    executor_thread_ = std::thread([this]() { executor_->spin(); });

    return hardware_interface::CallbackReturn::SUCCESS;
}

hardware_interface::CallbackReturn SeewayHardwareInterface::on_deactivate(const rclcpp_lifecycle::State &) {
    RCLCPP_INFO(node_->get_logger(), "Deactivating SeewayHardwareInterface...");

    // Stop the executor and join the thread cleanly before returning.
    if (executor_) {
        executor_->cancel();
        if (executor_thread_.joinable()) {
            executor_thread_.join();
        }
        executor_->remove_node(node_);
        executor_.reset();
    }

    return hardware_interface::CallbackReturn::SUCCESS;
}

hardware_interface::return_type SeewayHardwareInterface::read(const rclcpp::Time &, const rclcpp::Duration &) {
    // Do NOT call rclcpp::spin_some() here. The control loop thread must not
    // block on ROS callbacks. The dedicated executor thread (started in
    // on_activate) keeps the data caches up-to-date; we only copy them.
    std::lock_guard<std::mutex> lk(data_mutex_);

    // Copy latest sensor data into exported state arrays
    if (latest_sensor_ && hw_sensor_states_.size() >= 2) {
        const size_t n_adc = std::min(hw_sensor_states_.size() - 2, latest_sensor_->adc_raw.size());
        for (size_t i = 0; i < n_adc; ++i) {
            hw_sensor_states_[i] = static_cast<double>(latest_sensor_->adc_raw[i]);
        }
        hw_sensor_states_[8] = latest_sensor_->temperature_c;
        hw_sensor_states_[9] = latest_sensor_->humidity_pct;
    }

    // Copy latest GPIO input states
    if (latest_gpio_ && !latest_gpio_->input_states.empty()) {
        const uint32_t bank0 = latest_gpio_->input_states[0];
        for (size_t i = 0; i < 32; ++i) {
            hw_gpio_inputs_[i] = (bank0 & (1U << i)) ? 1.0 : 0.0;
        }
    }

    return hardware_interface::return_type::OK;
}

hardware_interface::return_type SeewayHardwareInterface::write(const rclcpp::Time &, const rclcpp::Duration &) {
    // --- Digital outputs (GPIO) ---
    // Only send a service request when at least one pin value has changed
    // relative to what was last successfully sent (edge-triggered).
    uint32_t set_mask = 0;
    uint32_t set_states = 0;
    bool needs_gpio_update = false;

    for (size_t i = 0; i < 32; ++i) {
        if (!std::isnan(hw_gpio_cmds_[i])) {
            // Include this pin if its commanded value differs from the last sent value
            if (std::isnan(hw_gpio_cmds_prev_[i]) || hw_gpio_cmds_[i] != hw_gpio_cmds_prev_[i]) {
                set_mask |= (1U << i);
                if (hw_gpio_cmds_[i] >= 0.5) {
                    set_states |= (1U << i);
                }
                needs_gpio_update = true;
            }
        }
    }

    if (needs_gpio_update) {
        if (cli_set_gpio_->service_is_ready()) {
            auto req = std::make_shared<seeway_interface_msgs::srv::SetGpio::Request>();
            req->bank = 0;
            req->pin_mask = set_mask;
            req->pin_states = set_states;
            cli_set_gpio_->async_send_request(req);
            // Update sent-cache only for pins included in this request
            for (size_t i = 0; i < 32; ++i) {
                if (set_mask & (1U << i)) {
                    hw_gpio_cmds_prev_[i] = hw_gpio_cmds_[i];
                }
            }
            gpio_service_warned_ = false;
        } else {
            // Service not yet available: keep commands pending for next cycle.
            // Warn once to avoid flooding the log.
            if (!gpio_service_warned_) {
                RCLCPP_WARN(node_->get_logger(),
                    "seeway/set_gpio service not available; GPIO commands pending retry");
                gpio_service_warned_ = true;
            }
        }
    }

    // --- PWM outputs ---
    // Same edge-triggered, one-shot-warning pattern.
    for (size_t i = 0; i < 4; ++i) {
        if (!std::isnan(hw_pwm_cmds_[i])) {
            if (std::isnan(hw_pwm_cmds_prev_[i]) || hw_pwm_cmds_[i] != hw_pwm_cmds_prev_[i]) {
                if (cli_set_pwm_->service_is_ready()) {
                    auto req = std::make_shared<seeway_interface_msgs::srv::SetPwm::Request>();
                    req->channel = static_cast<uint8_t>(i);
                    req->enable = 1;
                    req->frequency_hz = 1000;
                    req->duty_per_mil = static_cast<uint16_t>(hw_pwm_cmds_[i]);
                    cli_set_pwm_->async_send_request(req);
                    hw_pwm_cmds_prev_[i] = hw_pwm_cmds_[i];
                    pwm_service_warned_ = false;
                } else {
                    if (!pwm_service_warned_) {
                        RCLCPP_WARN(node_->get_logger(),
                            "seeway/set_pwm service not available; PWM commands pending retry");
                        pwm_service_warned_ = true;
                    }
                }
            }
        }
    }

    return hardware_interface::return_type::OK;
}

void SeewayHardwareInterface::on_sensor_data(const seeway_interface_msgs::msg::SensorData::SharedPtr msg) {
    std::lock_guard<std::mutex> lk(data_mutex_);
    latest_sensor_ = msg;
}

void SeewayHardwareInterface::on_gpio_status(const seeway_interface_msgs::msg::GpioStatus::SharedPtr msg) {
    std::lock_guard<std::mutex> lk(data_mutex_);
    latest_gpio_ = msg;
}

} // namespace seeway_interface_hardware

#include "pluginlib/class_list_macros.hpp"
PLUGINLIB_EXPORT_CLASS(
    seeway_interface_hardware::SeewayHardwareInterface,
    hardware_interface::SystemInterface)
