#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "hardware_interface/handle.hpp"
#include "hardware_interface/hardware_info.hpp"
#include "hardware_interface/system_interface.hpp"
#include "hardware_interface/types/hardware_interface_return_values.hpp"
#include "rclcpp/executors/single_threaded_executor.hpp"
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

    // Dedicated executor thread (spun in on_activate, stopped in on_deactivate)
    rclcpp::executors::SingleThreadedExecutor::UniquePtr executor_;
    std::thread executor_thread_;

    // Subscribers
    rclcpp::Subscription<seeway_interface_msgs::msg::SensorData>::SharedPtr sensor_sub_;
    rclcpp::Subscription<seeway_interface_msgs::msg::GpioStatus>::SharedPtr gpio_sub_;

    // Service Clients
    rclcpp::Client<seeway_interface_msgs::srv::SetGpio>::SharedPtr cli_set_gpio_;
    rclcpp::Client<seeway_interface_msgs::srv::SetPwm>::SharedPtr cli_set_pwm_;

    void on_sensor_data(const seeway_interface_msgs::msg::SensorData::SharedPtr msg);
    void on_gpio_status(const seeway_interface_msgs::msg::GpioStatus::SharedPtr msg);

    // State Variables (read from driver, updated by executor thread via callbacks)
    std::vector<double> hw_sensor_states_; // ADC channels + temp + humidity
    std::vector<double> hw_gpio_inputs_;   // 32 digital input pins

    // Command Variables (written by controllers, sent to driver)
    std::vector<double> hw_gpio_cmds_;      // digital output commands
    std::vector<double> hw_pwm_cmds_;       // PWM channel commands

    // Last-sent caches for edge-triggered service calls
    std::vector<double> hw_gpio_cmds_prev_;
    std::vector<double> hw_pwm_cmds_prev_;

    // One-shot service-unavailable warnings (reset when service becomes available).
    // Written only from write() on the control loop thread; atomic for safety.
    std::atomic<bool> gpio_service_warned_{false};
    std::atomic<bool> pwm_service_warned_{false};

    // Cache latest status from topics (protected by data_mutex_)
    seeway_interface_msgs::msg::SensorData::SharedPtr latest_sensor_;
    seeway_interface_msgs::msg::GpioStatus::SharedPtr latest_gpio_;
    std::mutex data_mutex_;
};

} // namespace seeway_interface_hardware
