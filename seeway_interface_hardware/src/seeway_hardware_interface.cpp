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
    // Currently hardcoded to 8 ADC + 2 (temp, hum), 32 GPIO inputs, 32 GPIO cmds, 4 PWM cmds
    hw_sensor_states_.resize(10, std::numeric_limits<double>::quiet_NaN());
    hw_gpio_inputs_.resize(32, 0.0);
    hw_gpio_cmds_.resize(32, std::numeric_limits<double>::quiet_NaN());
    hw_pwm_cmds_.resize(4, std::numeric_limits<double>::quiet_NaN());

    // Create a local ROS2 node for sub/pubs/clients inside the plugin
    node_ = std::make_shared<rclcpp::Node>("seeway_hardware_interface_node");

    return hardware_interface::CallbackReturn::SUCCESS;
}

hardware_interface::CallbackReturn SeewayHardwareInterface::on_configure(const rclcpp_lifecycle::State &) {
    RCLCPP_INFO(node_->get_logger(), "Configuring SeewayHardwareInterface...");

    // Setup Subscribers Topic
    sensor_sub_ = node_->create_subscription<seeway_interface_msgs::msg::SensorData>(
        "seeway/sensor_data", 10, std::bind(&SeewayHardwareInterface::on_sensor_data, this, std::placeholders::_1));
    gpio_sub_ = node_->create_subscription<seeway_interface_msgs::msg::GpioStatus>(
        "seeway/gpio_status", 10, std::bind(&SeewayHardwareInterface::on_gpio_status, this, std::placeholders::_1));

    // Setup Service Clients
    cli_set_gpio_ = node_->create_client<seeway_interface_msgs::srv::SetGpio>("seeway/set_gpio");
    cli_set_pwm_ = node_->create_client<seeway_interface_msgs::srv::SetPwm>("seeway/set_pwm");

    return hardware_interface::CallbackReturn::SUCCESS;
}

std::vector<hardware_interface::StateInterface> SeewayHardwareInterface::export_state_interfaces() {
    std::vector<hardware_interface::StateInterface> state_interfaces;

    // Export Sensor states (ADC 0-7, Temp, Hum)
    for (size_t i = 0; i < 8; ++i) {
        state_interfaces.emplace_back(hardware_interface::StateInterface(
            info_.name, "adc_" + std::to_string(i), &hw_sensor_states_[i]));
    }
    state_interfaces.emplace_back(hardware_interface::StateInterface(info_.name, "temperature", &hw_sensor_states_[8]));
    state_interfaces.emplace_back(hardware_interface::StateInterface(info_.name, "humidity", &hw_sensor_states_[9]));

    // Export GPIO input states (first bank 32 pins)
    for (size_t i = 0; i < 32; ++i) {
        state_interfaces.emplace_back(hardware_interface::StateInterface(
            info_.name, "gpio_in_" + std::to_string(i), &hw_gpio_inputs_[i]));
    }

    return state_interfaces;
}

std::vector<hardware_interface::CommandInterface> SeewayHardwareInterface::export_command_interfaces() {
    std::vector<hardware_interface::CommandInterface> command_interfaces;

    // Export GPIO commands (first bank 32 pins)
    for (size_t i = 0; i < 32; ++i) {
        command_interfaces.emplace_back(hardware_interface::CommandInterface(
            info_.name, "gpio_out_" + std::to_string(i), &hw_gpio_cmds_[i]));
    }

    // Export PWM commands
    for (size_t i = 0; i < 4; ++i) {
        command_interfaces.emplace_back(hardware_interface::CommandInterface(
            info_.name, "pwm_out_" + std::to_string(i), &hw_pwm_cmds_[i]));
    }

    return command_interfaces;
}

hardware_interface::CallbackReturn SeewayHardwareInterface::on_activate(const rclcpp_lifecycle::State &) {
    RCLCPP_INFO(node_->get_logger(), "Activating SeewayHardwareInterface...");
    // Spin up a thread to process ROS2 callbacks for the internal node
    return hardware_interface::CallbackReturn::SUCCESS;
}

hardware_interface::CallbackReturn SeewayHardwareInterface::on_deactivate(const rclcpp_lifecycle::State &) {
    RCLCPP_INFO(node_->get_logger(), "Deactivating SeewayHardwareInterface...");
    return hardware_interface::CallbackReturn::SUCCESS;
}

hardware_interface::return_type SeewayHardwareInterface::read(const rclcpp::Time &, const rclcpp::Duration &) {
    // Spin node quickly to capture latest topic data
    rclcpp::spin_some(node_);

    std::lock_guard<std::mutex> lk(data_mutex_);
    
    // Update hw_sensor_states_ from latest_sensor_
    if (latest_sensor_) {
        for (size_t i = 0; i < std::min(hw_sensor_states_.size() - 2, latest_sensor_->adc_raw.size()); ++i) {
            hw_sensor_states_[i] = static_cast<double>(latest_sensor_->adc_raw[i]);
        }
        hw_sensor_states_[8] = latest_sensor_->temperature_c;
        hw_sensor_states_[9] = latest_sensor_->humidity_pct;
    }

    // Update hw_gpio_inputs_ from latest_gpio_
    if (latest_gpio_ && !latest_gpio_->input_states.empty()) {
        uint32_t bank0 = latest_gpio_->input_states[0];
        for (size_t i = 0; i < 32; ++i) {
            hw_gpio_inputs_[i] = (bank0 & (1U << i)) ? 1.0 : 0.0;
        }
    }

    return hardware_interface::return_type::OK;
}

hardware_interface::return_type SeewayHardwareInterface::write(const rclcpp::Time &, const rclcpp::Duration &) {
    // Determine if any commanded values have changed and send service requests
    
    // Example: collect all GPIO commands that are not NaN and build a mask
    uint32_t set_mask = 0;
    uint32_t set_states = 0;
    bool needs_gpio_update = false;

    for (size_t i = 0; i < 32; ++i) {
        if (!std::isnan(hw_gpio_cmds_[i])) {
            set_mask |= (1U << i);
            if (hw_gpio_cmds_[i] >= 0.5) {
                set_states |= (1U << i);
            }
            needs_gpio_update = true;
            // Clear command cache after reading to avoid spam
            hw_gpio_cmds_[i] = std::numeric_limits<double>::quiet_NaN();
        }
    }

    if (needs_gpio_update && cli_set_gpio_->service_is_ready()) {
        auto req = std::make_shared<seeway_interface_msgs::srv::SetGpio::Request>();
        req->bank = 0;
        req->pin_mask = set_mask;
        req->pin_states = set_states;
        // Asynchronous call (don't block control loop)
        cli_set_gpio_->async_send_request(req);
    }

    // Example PWM checks
    for (size_t i = 0; i < 4; ++i) {
        if (!std::isnan(hw_pwm_cmds_[i])) {
            if (cli_set_pwm_->service_is_ready()) {
                auto req = std::make_shared<seeway_interface_msgs::srv::SetPwm::Request>();
                req->channel = static_cast<uint8_t>(i);
                req->enable = 1;
                req->frequency_hz = 1000; // default
                req->duty_per_mil = static_cast<uint16_t>(hw_pwm_cmds_[i]);
                cli_set_pwm_->async_send_request(req);
            }
            hw_pwm_cmds_[i] = std::numeric_limits<double>::quiet_NaN();
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
