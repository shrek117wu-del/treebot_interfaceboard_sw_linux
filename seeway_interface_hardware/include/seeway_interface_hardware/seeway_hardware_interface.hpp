#pragma once

#include <memory>
#include <string>
#include <vector>

#include "hardware_interface/handle.hpp"
#include "hardware_interface/hardware_info.hpp"
#include "hardware_interface/system_interface.hpp"
#include "hardware_interface/types/hardware_interface_return_values.hpp"
#include "rclcpp/rclcpp.hpp"

// ROS2 Messages & Services
#include "seeway_interface_msgs/msg/sensor_data.hpp"
#include "seeway_interface_msgs/msg/gpio_status.hpp"
#include "seeway_interface_msgs/srv/set_gpio.hpp"
#include "seeway_interface_msgs/srv/set_pwm.hpp"

namespace seeway_interface_hardware {

class SeewayHardwareInterface : public hardware_interface::SystemInterface {
public:
    RCLCPP_SHARED_PTR_DEFINITIONS(SeewayHardwareInterface)

    hardware_interface::CallbackReturn on_init(const hardware_interface::HardwareInfo & info) override;
    hardware_interface::CallbackReturn on_configure(const rclcpp_lifecycle::State & previous_state) override;
    std::vector<hardware_interface::StateInterface> export_state_interfaces() override;
    std::vector<hardware_interface::CommandInterface> export_command_interfaces() override;
    hardware_interface::CallbackReturn on_activate(const rclcpp_lifecycle::State & previous_state) override;
    hardware_interface::CallbackReturn on_deactivate(const rclcpp_lifecycle::State & previous_state) override;
    hardware_interface::return_type read(const rclcpp::Time & time, const rclcpp::Duration & period) override;
    hardware_interface::return_type write(const rclcpp::Time & time, const rclcpp::Duration & period) override;

private:
    rclcpp::Node::SharedPtr node_;

    // Subscribers
    rclcpp::Subscription<seeway_interface_msgs::msg::SensorData>::SharedPtr sensor_sub_;
    rclcpp::Subscription<seeway_interface_msgs::msg::GpioStatus>::SharedPtr gpio_sub_;

    // Service Clients
    rclcpp::Client<seeway_interface_msgs::srv::SetGpio>::SharedPtr cli_set_gpio_;
    rclcpp::Client<seeway_interface_msgs::srv::SetPwm>::SharedPtr cli_set_pwm_;

    void on_sensor_data(const seeway_interface_msgs::msg::SensorData::SharedPtr msg);
    void on_gpio_status(const seeway_interface_msgs::msg::GpioStatus::SharedPtr msg);

    // State Variables (read from driver)
    std::vector<double> hw_sensor_states_; // ADC channels + temp + humidity
    std::vector<double> hw_gpio_inputs_;   // e.g. 4 banks * 32 bits = 128 elements

    // Command Variables (written by controllers, sent to driver)
    std::vector<double> hw_gpio_cmds_;     // e.g. digital outputs
    std::vector<double> hw_pwm_cmds_;      // e.g. PWM channels

    // Cache latest status from topics
    seeway_interface_msgs::msg::SensorData::SharedPtr latest_sensor_;
    seeway_interface_msgs::msg::GpioStatus::SharedPtr latest_gpio_;
    std::mutex data_mutex_;
};

} // namespace seeway_interface_hardware
