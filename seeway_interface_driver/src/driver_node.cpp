#include "seeway_interface_driver/driver_node.hpp"
#include <chrono>
#include <cstring>
#include <thread>

namespace seeway_interface_driver {

DriverNode::DriverNode(const rclcpp::NodeOptions & options)
    : rclcpp_lifecycle::LifecycleNode("seeway_interface_driver", options)
{
    this->declare_parameter("transport", std::string("tcp"));
    this->declare_parameter("tcp.mode",         std::string("server"));
    this->declare_parameter("tcp.port",         9000);
    this->declare_parameter("tcp.host",         std::string("192.168.1.50"));
    this->declare_parameter("tcp.bind_address", std::string("0.0.0.0"));
    this->declare_parameter("tcp.reconnect_ms", 1000);
    this->declare_parameter("serial.device",   std::string("/dev/ttyACM0"));
    this->declare_parameter("serial.baudrate", 115200);
    this->declare_parameter("ack_timeout_ms", 300);
    this->declare_parameter("ack_retries",    0);
    this->declare_parameter("timeout.power_ms",     1000);
    this->declare_parameter("timeout.send_task_ms", 5000);
}

DriverNode::~DriverNode() {
    if (transport_) transport_->stop();
}

CallbackReturn DriverNode::on_configure(const rclcpp_lifecycle::State &) {
    RCLCPP_INFO(this->get_logger(), "Configuring...");

    // --- ACK parameters (with range validation) ---
    ack_timeout_ms_ = static_cast<int32_t>(this->get_parameter("ack_timeout_ms").as_int());
    ack_retries_    = static_cast<int32_t>(this->get_parameter("ack_retries").as_int());

    if (ack_timeout_ms_ < 100 || ack_timeout_ms_ > 10000) {
        RCLCPP_ERROR(this->get_logger(),
            "Invalid ack_timeout_ms: %d (must be 100–10000)", ack_timeout_ms_);
        return CallbackReturn::ERROR;
    }
    if (ack_retries_ < 0 || ack_retries_ > 5) {
        RCLCPP_ERROR(this->get_logger(),
            "Invalid ack_retries: %d (must be 0–5)", ack_retries_);
        return CallbackReturn::ERROR;
    }
    RCLCPP_INFO(this->get_logger(), "ACK settings: timeout=%dms  retries=%d",
                ack_timeout_ms_, ack_retries_);

    // --- Per-service timeout parameters ---
    power_timeout_ms_     = static_cast<int32_t>(
        this->get_parameter("timeout.power_ms").as_int());
    send_task_timeout_ms_ = static_cast<int32_t>(
        this->get_parameter("timeout.send_task_ms").as_int());

    if (power_timeout_ms_ < 100 || power_timeout_ms_ > 30000) {
        RCLCPP_ERROR(this->get_logger(),
            "Invalid timeout.power_ms: %d (must be 100–30000)", power_timeout_ms_);
        return CallbackReturn::ERROR;
    }
    if (send_task_timeout_ms_ < 100 || send_task_timeout_ms_ > 60000) {
        RCLCPP_ERROR(this->get_logger(),
            "Invalid timeout.send_task_ms: %d (must be 100–60000)", send_task_timeout_ms_);
        return CallbackReturn::ERROR;
    }

    // --- Transport setup (with parameter validation) ---
    const auto transport_type = this->get_parameter("transport").as_string();

    if (transport_type == "serial") {
        const auto device   = this->get_parameter("serial.device").as_string();
        const auto baudrate = static_cast<uint32_t>(this->get_parameter("serial.baudrate").as_int());

        // Validate baudrate against common standard values
        const uint32_t valid_baudrates[] = {
            9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600
        };
        bool valid = false;
        for (const auto& b : valid_baudrates) { if (b == baudrate) { valid = true; break; } }
        if (!valid) {
            RCLCPP_ERROR(this->get_logger(),
                "Invalid serial.baudrate: %u. "
                "Valid values: 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600",
                baudrate);
            return CallbackReturn::ERROR;
        }

        RCLCPP_INFO(this->get_logger(), "Transport: serial  device=%s  baudrate=%u",
                    device.c_str(), baudrate);
        transport_ = make_serial_transport(device, baudrate);
    } else if (transport_type == "tcp") {
        const auto tcp_mode    = this->get_parameter("tcp.mode").as_string();
        const auto tcp_port_i  = this->get_parameter("tcp.port").as_int();

        if (tcp_port_i <= 0 || tcp_port_i > 65535) {
            RCLCPP_ERROR(this->get_logger(),
                "Invalid tcp.port: %ld (must be 1–65535)", tcp_port_i);
            return CallbackReturn::ERROR;
        }
        const auto tcp_port = static_cast<uint16_t>(tcp_port_i);

        if (tcp_mode == "client") {
            const auto host         = this->get_parameter("tcp.host").as_string();
            const auto reconnect_ms = static_cast<uint32_t>(
                this->get_parameter("tcp.reconnect_ms").as_int());

            if (reconnect_ms < 100 || reconnect_ms > 30000) {
                RCLCPP_ERROR(this->get_logger(),
                    "Invalid tcp.reconnect_ms: %u (must be 100–30000)", reconnect_ms);
                return CallbackReturn::ERROR;
            }

            RCLCPP_INFO(this->get_logger(),
                "Transport: tcp client  host=%s  port=%u  reconnect_ms=%u",
                host.c_str(), tcp_port, reconnect_ms);
            transport_ = make_tcp_client_transport(host, tcp_port, reconnect_ms);
        } else if (tcp_mode == "server") {
            const auto bind_addr = this->get_parameter("tcp.bind_address").as_string();
            RCLCPP_INFO(this->get_logger(), "Transport: tcp server  bind=%s  port=%u",
                        bind_addr.c_str(), tcp_port);
            transport_ = make_tcp_server_transport(bind_addr, tcp_port);
        } else {
            RCLCPP_ERROR(this->get_logger(),
                "Invalid tcp.mode: '%s' (use 'client' or 'server')", tcp_mode.c_str());
            return CallbackReturn::ERROR;
        }
    } else {
        RCLCPP_ERROR(this->get_logger(),
            "Unknown transport type '%s'. Use 'tcp' or 'serial'.", transport_type.c_str());
        return CallbackReturn::ERROR;
    }

    // ---------- inbound handlers ----------
    transport_->register_handler(MSG_SENSOR_DATA,
        [this](MsgId, uint16_t, const uint8_t* pay, uint16_t len) { handle_sensor_data(pay, len); });
    transport_->register_handler(MSG_GPIO_STATUS,
        [this](MsgId, uint16_t, const uint8_t* pay, uint16_t len) { handle_gpio_status(pay, len); });
    transport_->register_handler(MSG_BATTERY_STATUS,
        [this](MsgId, uint16_t, const uint8_t* pay, uint16_t len) { handle_battery_status(pay, len); });
    transport_->register_handler(MSG_SYSTEM_STATUS,
        [this](MsgId, uint16_t, const uint8_t* pay, uint16_t len) { handle_system_status(pay, len); });
    transport_->register_handler(MSG_EVENT,
        [this](MsgId, uint16_t, const uint8_t* pay, uint16_t len) { handle_input_event(pay, len); });

    // MSG_ACK: resolve pending ACK promise for the echoed seq
    transport_->register_handler(MSG_ACK,
        [this](MsgId, uint16_t, const uint8_t* pay, uint16_t len) {
            if (len < sizeof(AckPayload)) {
                RCLCPP_WARN(this->get_logger(),
                            "MSG_ACK: unexpected len %u (expected %zu)", len, sizeof(AckPayload));
                diagnostics_.parse_errors++;
                return;
            }
            const auto* ack = reinterpret_cast<const AckPayload*>(pay);
            RCLCPP_DEBUG(this->get_logger(), "MSG_ACK: acked_msg=0x%02X  seq=%u  status=%u",
                         ack->acked_msg_id, ack->acked_seq, ack->status);
            std::lock_guard<std::mutex> lk(pending_acks_mutex_);
            auto it = pending_acks_.find(ack->acked_seq);
            if (it != pending_acks_.end()) {
                try {
                    it->second.set_value(ack->status);
                } catch (const std::future_error&) {
                    // Promise was already satisfied (e.g. timeout erased it); ignore.
                }
                pending_acks_.erase(it);
            }
        });

    // MSG_TASK_RESPONSE: resolve pending task promise by acked_seq
    transport_->register_handler(MSG_TASK_RESPONSE,
        [this](MsgId, uint16_t, const uint8_t* pay, uint16_t len) {
            if (len < sizeof(TaskResponsePayload)) {
                RCLCPP_WARN(this->get_logger(),
                            "MSG_TASK_RESPONSE: unexpected len %u (expected %zu)",
                            len, sizeof(TaskResponsePayload));
                diagnostics_.parse_errors++;
                return;
            }
            const auto* resp = reinterpret_cast<const TaskResponsePayload*>(pay);
            seeway_interface_msgs::srv::SendTask::Response ros_resp;
            ros_resp.success     = (resp->result == 0);
            ros_resp.result_code = resp->result;
            char msg_buf[sizeof(resp->message) + 1];
            std::memcpy(msg_buf, resp->message, sizeof(resp->message));
            msg_buf[sizeof(resp->message)] = '\0';
            ros_resp.message = msg_buf;
            RCLCPP_DEBUG(this->get_logger(),
                         "MSG_TASK_RESPONSE: task_id=%u  acked_seq=%u  result=%u",
                         resp->task_id, resp->acked_seq, resp->result);
            std::lock_guard<std::mutex> lk(pending_tasks_mutex_);
            auto it = pending_tasks_.find(resp->acked_seq);
            if (it != pending_tasks_.end()) {
                try {
                    it->second.set_value(ros_resp);
                } catch (const std::future_error&) {
                    // Already satisfied (timeout); ignore.
                }
                pending_tasks_.erase(it);
            }
        });

    // ---------- publishers ----------
    sensor_pub_  = this->create_publisher<seeway_interface_msgs::msg::SensorData>("seeway/sensor_data", 10);
    gpio_pub_    = this->create_publisher<seeway_interface_msgs::msg::GpioStatus>("seeway/gpio_status", 10);
    battery_pub_ = this->create_publisher<seeway_interface_msgs::msg::BatteryStatus>("seeway/battery_status", 10);
    system_pub_  = this->create_publisher<seeway_interface_msgs::msg::SystemStatus>("seeway/system_status", 10);
    event_pub_   = this->create_publisher<seeway_interface_msgs::msg::InputEvent>("seeway/input_event", 10);

    // ---------- services ----------
    srv_set_gpio_ = this->create_service<seeway_interface_msgs::srv::SetGpio>(
        "seeway/set_gpio", std::bind(&DriverNode::on_set_gpio, this, std::placeholders::_1, std::placeholders::_2));
    srv_set_pwm_  = this->create_service<seeway_interface_msgs::srv::SetPwm>(
        "seeway/set_pwm",  std::bind(&DriverNode::on_set_pwm, this, std::placeholders::_1, std::placeholders::_2));
    srv_power_    = this->create_service<seeway_interface_msgs::srv::PowerControl>(
        "seeway/power_control", std::bind(&DriverNode::on_power_control, this, std::placeholders::_1, std::placeholders::_2));
    srv_task_     = this->create_service<seeway_interface_msgs::srv::SendTask>(
        "seeway/send_task", std::bind(&DriverNode::on_send_task, this, std::placeholders::_1, std::placeholders::_2));

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
        sensor_pub_->on_deactivate(); gpio_pub_->on_deactivate();
        battery_pub_->on_deactivate(); system_pub_->on_deactivate();
        event_pub_->on_deactivate();
        return CallbackReturn::FAILURE;
    }

    deactivating_.store(false, std::memory_order_release);
    failed_checks_ = 0;

    // Check connection health every second; log when connection is lost/recovered.
    connection_monitor_timer_ = this->create_wall_timer(
        std::chrono::seconds(1),
        std::bind(&DriverNode::monitor_connection, this));

    // Emit a diagnostics summary every 30 seconds.
    diagnostics_timer_ = this->create_wall_timer(
        std::chrono::seconds(30),
        std::bind(&DriverNode::publish_diagnostics, this));

    RCLCPP_INFO(this->get_logger(), "Transport started successfully");
    return CallbackReturn::SUCCESS;
}

CallbackReturn DriverNode::on_deactivate(const rclcpp_lifecycle::State &) {
    RCLCPP_INFO(this->get_logger(), "Deactivating...");

    // Signal timers and service callbacks to exit early.
    deactivating_.store(true, std::memory_order_release);

    // Stop periodic timers immediately.
    if (connection_monitor_timer_) { connection_monitor_timer_->cancel(); }
    if (diagnostics_timer_)        { diagnostics_timer_->cancel(); }

    // Give any in-flight service callbacks a short grace period to complete.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    transport_->stop();

    // Unblock all threads waiting on an ACK – deliver an error status.
    {
        std::lock_guard<std::mutex> lk(pending_acks_mutex_);
        for (auto& [seq, prom] : pending_acks_) {
            try {
                prom.set_value(0xFF);   // non-zero = error; avoids exception propagation
            } catch (const std::future_error&) {
                // Promise already satisfied; safe to ignore.
            }
        }
        pending_acks_.clear();
    }

    // Unblock all threads waiting on a task response – deliver a failure response.
    {
        std::lock_guard<std::mutex> lk(pending_tasks_mutex_);
        for (auto& [seq, prom] : pending_tasks_) {
            seeway_interface_msgs::srv::SendTask::Response err_res;
            err_res.success     = false;
            err_res.result_code = 0;
            err_res.message     = "Node deactivating";
            try {
                prom.set_value(err_res);
            } catch (const std::future_error&) {
                // Already satisfied; safe to ignore.
            }
        }
        pending_tasks_.clear();
    }

    sensor_pub_->on_deactivate(); gpio_pub_->on_deactivate();
    battery_pub_->on_deactivate(); system_pub_->on_deactivate();
    event_pub_->on_deactivate();
    return CallbackReturn::SUCCESS;
}

CallbackReturn DriverNode::on_cleanup(const rclcpp_lifecycle::State &) {
    RCLCPP_INFO(this->get_logger(), "Cleaning up...");
    transport_.reset();
    connection_monitor_timer_.reset();
    diagnostics_timer_.reset();
    sensor_pub_.reset(); gpio_pub_.reset(); battery_pub_.reset();
    system_pub_.reset(); event_pub_.reset();
    srv_set_gpio_.reset(); srv_set_pwm_.reset();
    srv_power_.reset(); srv_task_.reset();
    return CallbackReturn::SUCCESS;
}

CallbackReturn DriverNode::on_shutdown(const rclcpp_lifecycle::State &) {
    RCLCPP_INFO(this->get_logger(), "Shutting down...");
    if (transport_) transport_->stop();
    return CallbackReturn::SUCCESS;
}

// ===========================================================================
// Connection monitoring
// ===========================================================================

void DriverNode::monitor_connection() {
    if (deactivating_.load(std::memory_order_acquire)) return;

    if (!transport_->is_connected()) {
        failed_checks_++;
        RCLCPP_WARN(this->get_logger(),
            "T113i not connected (consecutive checks: %d/%d)",
            failed_checks_, MAX_FAILED_CHECKS);

        if (failed_checks_ >= MAX_FAILED_CHECKS) {
            RCLCPP_ERROR(this->get_logger(),
                "Lost connection to T113i – clearing pending requests to prevent hangs");

            // Release any callers blocked waiting for ACK.
            {
                std::lock_guard<std::mutex> lk(pending_acks_mutex_);
                for (auto& [seq, prom] : pending_acks_) {
                    try { prom.set_value(0xFF); } catch (const std::future_error&) {}
                }
                pending_acks_.clear();
            }

            // Release any callers blocked waiting for a task response.
            {
                std::lock_guard<std::mutex> lk(pending_tasks_mutex_);
                for (auto& [seq, prom] : pending_tasks_) {
                    seeway_interface_msgs::srv::SendTask::Response err_res;
                    err_res.success = false;
                    err_res.message = "Connection lost";
                    try { prom.set_value(err_res); } catch (const std::future_error&) {}
                }
                pending_tasks_.clear();
            }

            failed_checks_ = 0;
        }
    } else {
        if (failed_checks_ > 0) {
            RCLCPP_INFO(this->get_logger(), "Connection to T113i restored");
        }
        failed_checks_ = 0;
    }
}

// ===========================================================================
// Diagnostics
// ===========================================================================

void DriverNode::publish_diagnostics() {
    RCLCPP_INFO(this->get_logger(),
        "[DIAGNOSTICS] Sent=%lu  Received=%lu  ACK_Timeouts=%lu  "
        "Send_Failures=%lu  Parse_Errors=%lu",
        diagnostics_.frames_sent.load(),
        diagnostics_.frames_received.load(),
        diagnostics_.ack_timeouts.load(),
        diagnostics_.send_failures.load(),
        diagnostics_.parse_errors.load());
}

// ===========================================================================
// Inbound frame handlers
// ===========================================================================

void DriverNode::handle_sensor_data(const uint8_t* pay, uint16_t len) {
    if (len != sizeof(SensorDataPayload)) {
        RCLCPP_WARN(this->get_logger(), "MSG_SENSOR_DATA: unexpected len %u (expected %zu)",
                    len, sizeof(SensorDataPayload));
        diagnostics_.parse_errors++;
        return;
    }
    const auto* data = reinterpret_cast<const SensorDataPayload*>(pay);

    // Validate channel_count to avoid out-of-bounds array access.
    if (data->channel_count > SENSOR_ADC_CHANNELS) {
        RCLCPP_ERROR(this->get_logger(),
            "MSG_SENSOR_DATA: invalid channel_count %u (max %d)",
            data->channel_count, SENSOR_ADC_CHANNELS);
        diagnostics_.parse_errors++;
        return;
    }

    seeway_interface_msgs::msg::SensorData msg;
    msg.header.stamp  = this->now();
    msg.timestamp_ms  = data->timestamp_ms;
    msg.temperature_c = data->temperature_c;
    msg.humidity_pct  = data->humidity_pct;
    msg.channel_count = data->channel_count;
    // Copy only the valid channels (indices 0 to channel_count-1) from the payload array.
    msg.adc_raw.assign(data->adc_raw, data->adc_raw + data->channel_count);
    sensor_pub_->publish(msg);
    diagnostics_.frames_received++;
}

void DriverNode::handle_gpio_status(const uint8_t* pay, uint16_t len) {
    if (len != sizeof(GpioStatusPayload)) {
        RCLCPP_WARN(this->get_logger(), "MSG_GPIO_STATUS: unexpected len %u (expected %zu)",
                    len, sizeof(GpioStatusPayload));
        diagnostics_.parse_errors++;
        return;
    }
    const auto* data = reinterpret_cast<const GpioStatusPayload*>(pay);
    seeway_interface_msgs::msg::GpioStatus msg;
    msg.header.stamp  = this->now();
    msg.timestamp_ms  = data->timestamp_ms;
    msg.input_states.assign(std::begin(data->input_states), std::end(data->input_states));
    msg.output_states.assign(std::begin(data->output_states), std::end(data->output_states));
    gpio_pub_->publish(msg);
    diagnostics_.frames_received++;
}

void DriverNode::handle_battery_status(const uint8_t* pay, uint16_t len) {
    if (len != sizeof(BatteryStatusPayload)) {
        RCLCPP_WARN(this->get_logger(), "MSG_BATTERY_STATUS: unexpected len %u (expected %zu)",
                    len, sizeof(BatteryStatusPayload));
        diagnostics_.parse_errors++;
        return;
    }
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
    diagnostics_.frames_received++;
}

void DriverNode::handle_system_status(const uint8_t* pay, uint16_t len) {
    if (len != sizeof(SystemStatusPayload)) {
        RCLCPP_WARN(this->get_logger(), "MSG_SYSTEM_STATUS: unexpected len %u (expected %zu)",
                    len, sizeof(SystemStatusPayload));
        diagnostics_.parse_errors++;
        return;
    }
    const auto* data = reinterpret_cast<const SystemStatusPayload*>(pay);
    seeway_interface_msgs::msg::SystemStatus msg;
    msg.header.stamp = this->now();
    msg.uptime_s     = data->uptime_s;
    msg.cpu_load_pct = data->cpu_load_pct;
    msg.cpu_temp_c   = data->cpu_temp_c;
    msg.free_mem_kb  = data->free_mem_kb;
    system_pub_->publish(msg);
    diagnostics_.frames_received++;
}

void DriverNode::handle_input_event(const uint8_t* pay, uint16_t len) {
    if (len != sizeof(EventPayload)) {
        RCLCPP_WARN(this->get_logger(), "MSG_EVENT: unexpected len %u (expected %zu)",
                    len, sizeof(EventPayload));
        diagnostics_.parse_errors++;
        return;
    }
    const auto* data = reinterpret_cast<const EventPayload*>(pay);
    seeway_interface_msgs::msg::InputEvent msg;
    msg.header.stamp = this->now();
    msg.timestamp_ms = data->timestamp_ms;
    msg.type         = data->type;
    msg.code         = data->code;
    msg.value        = data->value;
    event_pub_->publish(msg);
    diagnostics_.frames_received++;
}

// ===========================================================================
// send_and_wait_ack – helper for SetGpio / SetPwm / PowerControl
// ===========================================================================

template<typename T>
bool DriverNode::send_and_wait_ack(MsgId id, const T& payload, std::string& out_message,
                                   int timeout_ms)
{
    const int effective_timeout = (timeout_ms > 0) ? timeout_ms : ack_timeout_ms_;

    // Use uint32_t counter; cast to uint16_t for the wire protocol and map key.
    uint32_t seq32 = next_seq_.fetch_add(1, std::memory_order_relaxed);
    uint16_t seq   = static_cast<uint16_t>(seq32);

    std::future<uint8_t> fut;
    {
        std::lock_guard<std::mutex> lk(pending_acks_mutex_);
        fut = pending_acks_[seq].get_future();
    }

    if (!transport_->send_payload(id, seq, payload)) {
        std::lock_guard<std::mutex> lk(pending_acks_mutex_);
        pending_acks_.erase(seq);
        out_message = "Failed to send";
        diagnostics_.send_failures++;
        return false;
    }
    diagnostics_.frames_sent++;

    const auto timeout_dur = std::chrono::milliseconds(effective_timeout);
    int retries = ack_retries_;
    do {
        if (fut.wait_for(timeout_dur) == std::future_status::ready) {
            uint8_t result;
            try {
                result = fut.get();
            } catch (const std::exception& e) {
                out_message = std::string("ACK exception: ") + e.what();
                RCLCPP_ERROR(this->get_logger(), "send_and_wait_ack: %s", out_message.c_str());
                return false;
            }
            if (result == 0) { out_message = "OK"; return true; }
            out_message = "T113i error code " + std::to_string(result);
            RCLCPP_WARN(this->get_logger(), "ACK error: seq=%u  result_code=%u", seq, result);
            return false;
        }

        diagnostics_.ack_timeouts++;
        RCLCPP_WARN(this->get_logger(), "ACK timeout (seq=%u, %dms), retries_left=%d",
                    seq, effective_timeout, retries);

        if (retries > 0) {
            uint32_t new_seq32 = next_seq_.fetch_add(1, std::memory_order_relaxed);
            uint16_t new_seq   = static_cast<uint16_t>(new_seq32);
            {
                std::lock_guard<std::mutex> lk(pending_acks_mutex_);
                pending_acks_.erase(seq);
                fut = pending_acks_[new_seq].get_future();
            }
            seq = new_seq;
            if (!transport_->send_payload(id, seq, payload)) {
                out_message = "Retry send failed";
                diagnostics_.send_failures++;
                return false;
            }
            diagnostics_.frames_sent++;
        }
    } while (retries-- > 0);

    {
        std::lock_guard<std::mutex> lk(pending_acks_mutex_);
        pending_acks_.erase(seq);
    }
    out_message = "ACK timeout";
    return false;
}

// ===========================================================================
// Service callbacks
// ===========================================================================

void DriverNode::on_set_gpio(
    const std::shared_ptr<seeway_interface_msgs::srv::SetGpio::Request> req,
    std::shared_ptr<seeway_interface_msgs::srv::SetGpio::Response> res)
{
    if (!transport_->is_connected()) {
        RCLCPP_WARN(this->get_logger(), "SetGpio: not connected to T113i");
        res->success = false; res->message = "Not connected to T113i"; return;
    }
    DoCommandPayload cmd{};
    cmd.bank = req->bank; cmd.pin_mask = req->pin_mask; cmd.pin_states = req->pin_states;
    res->success = send_and_wait_ack(MSG_DO_COMMAND, cmd, res->message);
}

void DriverNode::on_set_pwm(
    const std::shared_ptr<seeway_interface_msgs::srv::SetPwm::Request> req,
    std::shared_ptr<seeway_interface_msgs::srv::SetPwm::Response> res)
{
    if (!transport_->is_connected()) {
        RCLCPP_WARN(this->get_logger(), "SetPwm: not connected to T113i");
        res->success = false; res->message = "Not connected"; return;
    }
    PwmCommandPayload cmd{};
    cmd.channel = req->channel; cmd.frequency_hz = req->frequency_hz;
    cmd.duty_per_mil = req->duty_per_mil; cmd.enable = req->enable;
    res->success = send_and_wait_ack(MSG_PWM_COMMAND, cmd, res->message);
}

void DriverNode::on_power_control(
    const std::shared_ptr<seeway_interface_msgs::srv::PowerControl::Request> req,
    std::shared_ptr<seeway_interface_msgs::srv::PowerControl::Response> res)
{
    if (!transport_->is_connected()) {
        RCLCPP_WARN(this->get_logger(), "PowerControl: not connected to T113i");
        res->success = false; res->message = "Not connected"; return;
    }
    PowerCommandPayload cmd{};
    cmd.command  = static_cast<PowerCmd>(req->command);
    cmd.delay_ms = req->delay_ms;
    // Use ACK-based send so callers know whether the command was received.
    res->success = send_and_wait_ack(MSG_POWER_COMMAND, cmd, res->message, power_timeout_ms_);
}

void DriverNode::on_send_task(
    const std::shared_ptr<seeway_interface_msgs::srv::SendTask::Request> req,
    std::shared_ptr<seeway_interface_msgs::srv::SendTask::Response> res)
{
    if (!transport_->is_connected()) {
        RCLCPP_WARN(this->get_logger(), "SendTask: not connected to T113i");
        res->success = false; res->message = "Not connected"; return;
    }
    TaskCommandPayload cmd{};
    cmd.task_id = static_cast<TaskId>(req->task_id);
    cmd.arg     = req->arg;

    // Safe string copy with explicit truncation warning.
    const size_t max_name = sizeof(cmd.name) - 1;
    if (req->name.length() > max_name) {
        RCLCPP_WARN(this->get_logger(),
            "SendTask: name truncated from %zu to %zu bytes",
            req->name.length(), max_name);
    }
    const size_t copy_len = std::min(req->name.length(), max_name);
    std::memcpy(cmd.name, req->name.c_str(), copy_len);
    cmd.name[copy_len] = '\0';

    uint16_t seq = static_cast<uint16_t>(next_seq_.fetch_add(1, std::memory_order_relaxed));

    std::future<seeway_interface_msgs::srv::SendTask::Response> fut;
    {
        std::lock_guard<std::mutex> lk(pending_tasks_mutex_);
        fut = pending_tasks_[seq].get_future();
    }

    if (!transport_->send_payload(MSG_TASK_COMMAND, seq, cmd)) {
        std::lock_guard<std::mutex> lk(pending_tasks_mutex_);
        pending_tasks_.erase(seq);
        res->success = false; res->message = "Failed to send"; return;
    }
    diagnostics_.frames_sent++;

    // Use the configurable per-service timeout instead of a hard-coded value.
    if (fut.wait_for(std::chrono::milliseconds(send_task_timeout_ms_))
            == std::future_status::ready) {
        *res = fut.get();
    } else {
        RCLCPP_WARN(this->get_logger(),
                    "SendTask: timeout waiting for TaskResponse (seq=%u)", seq);
        std::lock_guard<std::mutex> lk(pending_tasks_mutex_);
        pending_tasks_.erase(seq);
        res->success = false;
        res->message = "Timeout waiting for TaskResponse from T113i";
        diagnostics_.ack_timeouts++;
    }
}

}  // namespace seeway_interface_driver

#include "rclcpp_components/register_node_macro.hpp"
RCLCPP_COMPONENTS_REGISTER_NODE(seeway_interface_driver::DriverNode)

