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

#include <atomic>
#include <condition_variable>
#include <future>
#include <mutex>
#include <unordered_map>

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

    // -----------------------------------------------------------------------
    // Sequence counter – shared across all outbound frames so that ACKs can
    // be matched back to their requests regardless of concurrency.
    // -----------------------------------------------------------------------
    std::atomic<uint16_t> next_seq_{0};

    // ACK parameters (declared in constructor via declare_parameter)
    int32_t ack_timeout_ms_{300};
    int32_t ack_retries_{0};

    // -----------------------------------------------------------------------
    // Pending ACK map  (MSG_ACK responses for SetGpio / SetPwm)
    // Key = request seq; value = promise<ack_status_byte>.
    // -----------------------------------------------------------------------
    std::mutex pending_acks_mutex_;
    std::unordered_map<uint16_t, std::promise<uint8_t>> pending_acks_;

    // -----------------------------------------------------------------------
    // Pending task-response map  (MSG_TASK_RESPONSE for SendTask)
    // Key = request seq (echoed back in TaskResponsePayload::acked_seq).
    // -----------------------------------------------------------------------
    std::mutex pending_tasks_mutex_;
    std::unordered_map<uint16_t,
        std::promise<seeway_interface_msgs::srv::SendTask::Response>> pending_tasks_;

    // -----------------------------------------------------------------------
    // Helper: send a payload and wait up to ack_timeout_ms_ for an ACK.
    // Returns true when the ACK status == 0 (success).
    // -----------------------------------------------------------------------
    template<typename T>
    bool send_and_wait_ack(MsgId id, const T& payload,
                           std::string& out_message);
};

}  // namespace seeway_interface_driver
