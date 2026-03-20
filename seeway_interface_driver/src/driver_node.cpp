#include "seeway_interface_driver/driver_node.hpp"
#include <chrono>
#include <cstring>

namespace seeway_interface_driver {

DriverNode::DriverNode(const rclcpp::NodeOptions & options)
    : rclcpp_lifecycle::LifecycleNode("seeway_interface_driver", options)
{
    // Transport selector
    this->declare_parameter("transport", std::string("tcp"));

    // TCP parameters
    this->declare_parameter("tcp.mode",         std::string("server"));
    this->declare_parameter("tcp.port",         9000);
    this->declare_parameter("tcp.host",         std::string("192.168.1.50"));
    this->declare_parameter("tcp.bind_address", std::string("0.0.0.0"));
    this->declare_parameter("tcp.reconnect_ms", 1000);

    // Serial / USB parameters
    this->declare_parameter("serial.device",   std::string("/dev/ttyACM0"));
    this->declare_parameter("serial.baudrate", 115200);
}

DriverNode::~DriverNode() {
    if (transport_) {
        transport_->stop();
    }
}

CallbackReturn DriverNode::on_configure(const rclcpp_lifecycle::State &) {
    RCLCPP_INFO(this->get_logger(), "Configuring...");

    const auto transport_type =
        this->get_parameter("transport").as_string();

    if (transport_type == "serial") {
        const auto device   = this->get_parameter("serial.device").as_string();
        const auto baudrate = static_cast<uint32_t>(
            this->get_parameter("serial.baudrate").as_int());
        RCLCPP_INFO(this->get_logger(),
                    "Transport: serial  device=%s  baudrate=%u",
                    device.c_str(), baudrate);
        transport_ = make_serial_transport(device, baudrate);

    } else if (transport_type == "tcp") {
        const auto tcp_mode = this->get_parameter("tcp.mode").as_string();
        const auto tcp_port = static_cast<uint16_t>(
            this->get_parameter("tcp.port").as_int());

        if (tcp_mode == "client") {
            const auto host         = this->get_parameter("tcp.host").as_string();
            const auto reconnect_ms = static_cast<uint32_t>(
                this->get_parameter("tcp.reconnect_ms").as_int());
            RCLCPP_INFO(this->get_logger(),
                        "Transport: tcp client  host=%s  port=%u  reconnect_ms=%u",
                        host.c_str(), tcp_port, reconnect_ms);
            transport_ = make_tcp_client_transport(host, tcp_port, reconnect_ms);

        } else {
            // Default: tcp server
            const auto bind_addr =
                this->get_parameter("tcp.bind_address").as_string();
            RCLCPP_INFO(this->get_logger(),
                        "Transport: tcp server  bind=%s  port=%u",
                        bind_addr.c_str(), tcp_port);
            transport_ = make_tcp_server_transport(bind_addr, tcp_port);
        }

    } else {
        RCLCPP_ERROR(this->get_logger(),
                     "Unknown transport type '%s'. Use 'tcp' or 'serial'.",
                     transport_type.c_str());
        return CallbackReturn::ERROR;
    }

    // Register inbound frame handlers
    transport_->register_handler(
        MSG_SENSOR_DATA,
        [this](MsgId, uint16_t, const uint8_t* pay, uint16_t len) {
            handle_sensor_data(pay, len);
        });
    transport_->register_handler(
        MSG_GPIO_STATUS,
        [this](MsgId, uint16_t, const uint8_t* pay, uint16_t len) {
            handle_gpio_status(pay, len);
        });
    transport_->register_handler(
        MSG_BATTERY_STATUS,
        [this](MsgId, uint16_t, const uint8_t* pay, uint16_t len) {
            handle_battery_status(pay, len);
        });
    transport_->register_handler(
        MSG_SYSTEM_STATUS,
        [this](MsgId, uint16_t, const uint8_t* pay, uint16_t len) {
            handle_system_status(pay, len);
        });
    transport_->register_handler(
        MSG_EVENT,
        [this](MsgId, uint16_t, const uint8_t* pay, uint16_t len) {
            handle_input_event(pay, len);
        });
    transport_->register_handler(
        MSG_TASK_RESPONSE,
        [this](MsgId, uint16_t, const uint8_t* pay, uint16_t len) {
            if (len == sizeof(TaskResponsePayload)) {
                const auto* resp =
                    reinterpret_cast<const TaskResponsePayload*>(pay);
                std::lock_guard<std::mutex> lk(task_resp_mutex_);
                pending_task_resp_.success    = (resp->result == 0);
                pending_task_resp_.result_code = resp->result;
                pending_task_resp_.message    = std::string(resp->message);
                task_resp_ready_ = true;
                task_resp_cv_.notify_all();
            }
        });

    // Publishers
    sensor_pub_  = this->create_publisher<seeway_interface_msgs::msg::SensorData>(
        "seeway/sensor_data", 10);
    gpio_pub_    = this->create_publisher<seeway_interface_msgs::msg::GpioStatus>(
        "seeway/gpio_status", 10);
    battery_pub_ = this->create_publisher<seeway_interface_msgs::msg::BatteryStatus>(
        "seeway/battery_status", 10);
    system_pub_  = this->create_publisher<seeway_interface_msgs::msg::SystemStatus>(
        "seeway/system_status", 10);
    event_pub_   = this->create_publisher<seeway_interface_msgs::msg::InputEvent>(
        "seeway/input_event", 10);

    // Services
    srv_set_gpio_ = this->create_service<seeway_interface_msgs::srv::SetGpio>(
        "seeway/set_gpio",
        std::bind(&DriverNode::on_set_gpio, this,
                  std::placeholders::_1, std::placeholders::_2));
    srv_set_pwm_  = this->create_service<seeway_interface_msgs::srv::SetPwm>(
        "seeway/set_pwm",
        std::bind(&DriverNode::on_set_pwm, this,
                  std::placeholders::_1, std::placeholders::_2));
    srv_power_    = this->create_service<seeway_interface_msgs::srv::PowerControl>(
        "seeway/power_control",
        std::bind(&DriverNode::on_power_control, this,
                  std::placeholders::_1, std::placeholders::_2));
    srv_task_     = this->create_service<seeway_interface_msgs::srv::SendTask>(
        "seeway/send_task",
        std::bind(&DriverNode::on_send_task, this,
                  std::placeholders::_1, std::placeholders::_2));

    RCLCPP_INFO(this->get_logger(), "Configured successfully");
    return CallbackReturn::SUCCESS;
}

CallbackReturn DriverNode::on_activate(const rclcpp_lifecycle::State &) {
    RCLCPP_INFO(this->get_logger(), "Activating...");
    sensor_pub_->on_activate();
    gpio_pub_->on_activate();
    battery_pub_->on_activate();
    system_pub_->on_activate();
    event_pub_->on_activate();

    if (!transport_->start()) {
        RCLCPP_ERROR(this->get_logger(), "Failed to start transport");
        sensor_pub_->on_deactivate();
        gpio_pub_->on_deactivate();
        battery_pub_->on_deactivate();
        system_pub_->on_deactivate();
        event_pub_->on_deactivate();
        return CallbackReturn::FAILURE;
    }
    RCLCPP_INFO(this->get_logger(), "Transport started successfully");
    return CallbackReturn::SUCCESS;
}

CallbackReturn DriverNode::on_deactivate(const rclcpp_lifecycle::State &) {
    RCLCPP_INFO(this->get_logger(), "Deactivating...");
    transport_->stop();
    sensor_pub_->on_deactivate();
    gpio_pub_->on_deactivate();
    battery_pub_->on_deactivate();
    system_pub_->on_deactivate();
    event_pub_->on_deactivate();
    return CallbackReturn::SUCCESS;
}

CallbackReturn DriverNode::on_cleanup(const rclcpp_lifecycle::State &) {
    RCLCPP_INFO(this->get_logger(), "Cleaning up...");
    transport_.reset();
    sensor_pub_.reset();
    gpio_pub_.reset();
    battery_pub_.reset();
    system_pub_.reset();
    event_pub_.reset();
    srv_set_gpio_.reset();
    srv_set_pwm_.reset();
    srv_power_.reset();
    srv_task_.reset();
    return CallbackReturn::SUCCESS;
}

CallbackReturn DriverNode::on_shutdown(const rclcpp_lifecycle::State &) {
    RCLCPP_INFO(this->get_logger(), "Shutting down...");
    if (transport_) {
        transport_->stop();
    }
    return CallbackReturn::SUCCESS;
}

// ===========================================================================
// Handlers for data from T113i
// ===========================================================================

void DriverNode::handle_sensor_data(const uint8_t* pay, uint16_t len) {
    if (len != sizeof(SensorDataPayload)) return;
    const auto* data = reinterpret_cast<const SensorDataPayload*>(pay);

    seeway_interface_msgs::msg::SensorData msg;
    msg.header.stamp  = this->now();
    msg.timestamp_ms  = data->timestamp_ms;
    msg.temperature_c = data->temperature_c;
    msg.humidity_pct  = data->humidity_pct;
    msg.channel_count = data->channel_count;
    msg.adc_raw.assign(std::begin(data->adc_raw), std::end(data->adc_raw));

    sensor_pub_->publish(msg);
}

void DriverNode::handle_gpio_status(const uint8_t* pay, uint16_t len) {
    if (len != sizeof(GpioStatusPayload)) return;
    const auto* data = reinterpret_cast<const GpioStatusPayload*>(pay);

    seeway_interface_msgs::msg::GpioStatus msg;
    msg.header.stamp  = this->now();
    msg.timestamp_ms  = data->timestamp_ms;
    msg.input_states.assign(std::begin(data->input_states),
                             std::end(data->input_states));
    msg.output_states.assign(std::begin(data->output_states),
                              std::end(data->output_states));

    gpio_pub_->publish(msg);
}

void DriverNode::handle_battery_status(const uint8_t* pay, uint16_t len) {
    if (len != sizeof(BatteryStatusPayload)) return;
    const auto* data = reinterpret_cast<const BatteryStatusPayload*>(pay);

    seeway_interface_msgs::msg::BatteryStatus msg;
    msg.header.stamp  = this->now();
    msg.timestamp_ms  = data->timestamp_ms;
    msg.voltage_v     = data->voltage_v;
    msg.current_a     = data->current_a;
    msg.soc_pct       = data->soc_pct;
    msg.temperature_c = data->temperature_c;
    msg.status_flags  = data->status_flags;
    msg.cycle_count   = data->cycle_count;

    battery_pub_->publish(msg);
}

void DriverNode::handle_system_status(const uint8_t* pay, uint16_t len) {
    if (len != sizeof(SystemStatusPayload)) return;
    const auto* data = reinterpret_cast<const SystemStatusPayload*>(pay);

    seeway_interface_msgs::msg::SystemStatus msg;
    msg.header.stamp  = this->now();
    msg.uptime_s      = data->uptime_s;
    msg.cpu_load_pct  = data->cpu_load_pct;
    msg.cpu_temp_c    = data->cpu_temp_c;
    msg.free_mem_kb   = data->free_mem_kb;

    system_pub_->publish(msg);
}

void DriverNode::handle_input_event(const uint8_t* pay, uint16_t len) {
    if (len != sizeof(EventPayload)) return;
    const auto* data = reinterpret_cast<const EventPayload*>(pay);

    seeway_interface_msgs::msg::InputEvent msg;
    msg.header.stamp = this->now();
    msg.timestamp_ms = data->timestamp_ms;
    msg.type         = data->type;
    msg.code         = data->code;
    msg.value        = data->value;

    event_pub_->publish(msg);
}

// ===========================================================================
// ROS2 Service Callbacks (outbound commands to T113i)
// ===========================================================================

void DriverNode::on_set_gpio(
    const std::shared_ptr<seeway_interface_msgs::srv::SetGpio::Request> req,
    std::shared_ptr<seeway_interface_msgs::srv::SetGpio::Response> res)
{
    if (!transport_->is_connected()) {
        res->success = false;
        res->message = "Not connected to T113i";
        return;
    }
    DoCommandPayload cmd{};
    cmd.bank       = req->bank;
    cmd.pin_mask   = req->pin_mask;
    cmd.pin_states = req->pin_states;

    res->success = transport_->send_payload(MSG_DO_COMMAND, cmd);
    res->message = res->success ? "Sent successfully" : "Failed to send";
}

void DriverNode::on_set_pwm(
    const std::shared_ptr<seeway_interface_msgs::srv::SetPwm::Request> req,
    std::shared_ptr<seeway_interface_msgs::srv::SetPwm::Response> res)
{
    if (!transport_->is_connected()) {
        res->success = false;
        res->message = "Not connected";
        return;
    }
    PwmCommandPayload cmd{};
    cmd.channel      = req->channel;
    cmd.frequency_hz = req->frequency_hz;
    cmd.duty_per_mil = req->duty_per_mil;
    cmd.enable       = req->enable;

    res->success = transport_->send_payload(MSG_PWM_COMMAND, cmd);
    res->message = res->success ? "Sent successfully" : "Failed to send";
}

void DriverNode::on_power_control(
    const std::shared_ptr<seeway_interface_msgs::srv::PowerControl::Request> req,
    std::shared_ptr<seeway_interface_msgs::srv::PowerControl::Response> res)
{
    if (!transport_->is_connected()) {
        res->success = false;
        res->message = "Not connected";
        return;
    }
    PowerCommandPayload cmd{};
    cmd.command  = static_cast<PowerCmd>(req->command);
    cmd.delay_ms = req->delay_ms;

    res->success = transport_->send_payload(MSG_POWER_COMMAND, cmd);
    res->message = res->success ? "Sent" : "Failed";
}

void DriverNode::on_send_task(
    const std::shared_ptr<seeway_interface_msgs::srv::SendTask::Request> req,
    std::shared_ptr<seeway_interface_msgs::srv::SendTask::Response> res)
{
    if (!transport_->is_connected()) {
        res->success = false;
        res->message = "Not connected";
        return;
    }
    TaskCommandPayload cmd{};
    cmd.task_id = static_cast<TaskId>(req->task_id);
    cmd.arg     = req->arg;
    strncpy(cmd.name, req->name.c_str(), sizeof(cmd.name) - 1);

    {
        std::lock_guard<std::mutex> lk(task_resp_mutex_);
        task_resp_ready_ = false;
    }

    if (!transport_->send_payload(MSG_TASK_COMMAND, cmd)) {
        res->success = false;
        res->message = "Failed to send";
        return;
    }

    // Wait up to 5 s for TaskResponse from T113i
    std::unique_lock<std::mutex> lk(task_resp_mutex_);
    if (task_resp_cv_.wait_for(lk, std::chrono::seconds(5),
                               [this] { return task_resp_ready_; })) {
        res->success    = pending_task_resp_.success;
        res->result_code = pending_task_resp_.result_code;
        res->message    = pending_task_resp_.message;
    } else {
        res->success = false;
        res->message = "Timeout waiting for TaskResponse from T113i";
    }
}

}  // namespace seeway_interface_driver

#include "rclcpp_components/register_node_macro.hpp"
RCLCPP_COMPONENTS_REGISTER_NODE(seeway_interface_driver::DriverNode)
