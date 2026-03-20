#pragma once

#include "rclcpp/rclcpp.hpp"
#include "rclcpp_lifecycle/lifecycle_node.hpp"
#include "seeway_interface_driver/transport.hpp"

// ROS2 Messages
#include "seeway_interface_msgs/msg/sensor_data.hpp"
#include "seeway_interface_msgs/msg/gpio_status.hpp"
#include "seeway_interface_msgs/msg/battery_status.hpp"
#include "seeway_interface_msgs/msg/system_status.hpp"
#include "seeway_interface_msgs/msg/input_event.hpp"

// ROS2 Services
#include "seeway_interface_msgs/srv/set_gpio.hpp"
#include "seeway_interface_msgs/srv/set_pwm.hpp"
#include "seeway_interface_msgs/srv/power_control.hpp"
#include "seeway_interface_msgs/srv/send_task.hpp"

namespace seeway_interface_driver {

using CallbackReturn = rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn;

class DriverNode : public rclcpp_lifecycle::LifecycleNode {
public:
    explicit DriverNode(const rclcpp::NodeOptions & options);
    ~DriverNode();

protected:
    CallbackReturn on_configure(const rclcpp_lifecycle::State &) override;
    CallbackReturn on_activate(const rclcpp_lifecycle::State &) override;
    CallbackReturn on_deactivate(const rclcpp_lifecycle::State &) override;
    CallbackReturn on_cleanup(const rclcpp_lifecycle::State &) override;
    CallbackReturn on_shutdown(const rclcpp_lifecycle::State &) override;

private:
    // Active transport instance (created in on_configure, destroyed in on_cleanup)
    std::unique_ptr<ITransport> transport_;

    // Publishers
    rclcpp_lifecycle::LifecyclePublisher<seeway_interface_msgs::msg::SensorData>::SharedPtr sensor_pub_;
    rclcpp_lifecycle::LifecyclePublisher<seeway_interface_msgs::msg::GpioStatus>::SharedPtr gpio_pub_;
    rclcpp_lifecycle::LifecyclePublisher<seeway_interface_msgs::msg::BatteryStatus>::SharedPtr battery_pub_;
    rclcpp_lifecycle::LifecyclePublisher<seeway_interface_msgs::msg::SystemStatus>::SharedPtr system_pub_;
    rclcpp_lifecycle::LifecyclePublisher<seeway_interface_msgs::msg::InputEvent>::SharedPtr event_pub_;

    // Services
    rclcpp::Service<seeway_interface_msgs::srv::SetGpio>::SharedPtr srv_set_gpio_;
    rclcpp::Service<seeway_interface_msgs::srv::SetPwm>::SharedPtr srv_set_pwm_;
    rclcpp::Service<seeway_interface_msgs::srv::PowerControl>::SharedPtr srv_power_;
    rclcpp::Service<seeway_interface_msgs::srv::SendTask>::SharedPtr srv_task_;

    // Callbacks for inbound frames from T113i
    void handle_sensor_data(const uint8_t* pay, uint16_t len);
    void handle_gpio_status(const uint8_t* pay, uint16_t len);
    void handle_battery_status(const uint8_t* pay, uint16_t len);
    void handle_system_status(const uint8_t* pay, uint16_t len);
    void handle_input_event(const uint8_t* pay, uint16_t len);

    // ROS2 service callbacks (outbound commands to T113i)
    void on_set_gpio(const std::shared_ptr<seeway_interface_msgs::srv::SetGpio::Request> req,
                     std::shared_ptr<seeway_interface_msgs::srv::SetGpio::Response> res);
    void on_set_pwm(const std::shared_ptr<seeway_interface_msgs::srv::SetPwm::Request> req,
                    std::shared_ptr<seeway_interface_msgs::srv::SetPwm::Response> res);
    void on_power_control(const std::shared_ptr<seeway_interface_msgs::srv::PowerControl::Request> req,
                          std::shared_ptr<seeway_interface_msgs::srv::PowerControl::Response> res);
    void on_send_task(const std::shared_ptr<seeway_interface_msgs::srv::SendTask::Request> req,
                      std::shared_ptr<seeway_interface_msgs::srv::SendTask::Response> res);

    std::mutex task_resp_mutex_;
    std::condition_variable task_resp_cv_;
    seeway_interface_msgs::srv::SendTask::Response pending_task_resp_;
    bool task_resp_ready_ = false;
};

}  // namespace seeway_interface_driver
