/**
 * @file main.cpp
 * @brief Seeway Interface Daemon for Allwinner T113i
 *
 * Reads configuration from daemon.conf (path given on the command line or
 * defaulting to /etc/seeway_interface/daemon.conf) and wires all subsystems
 * together.
 *
 * Improvements over the original:
 *  - Centralized Logger with timestamps and file output
 *  - Module initialization with exponential-backoff retry (ModuleInitializer)
 *  - Connection health monitoring (ConnectionMonitor)
 *  - Main loop runs at 500 ms instead of 100 ms (P0: CPU reduction)
 *  - Protocol version handshake on connect (MSG_HANDSHAKE_REQ/ACK)
 *  - TaskContext validity check and queue-depth limiting (TaskExecutor)
 *  - Sequence-number–based pending task tracking
 *  - Graceful shutdown with task drain (ShutdownManager)
 */

#include "protocol.h"
#include "serial_comm.h"
#include "gpio_controller.h"
#include "sensor_reader.h"
#include "power_manager.h"
#include "input_handler.h"
#include "task_executor.h"
#include "config_loader.h"
#include "logger.h"
#include "connection_monitor.h"
#include "module_initializer.h"
#include "shutdown_manager.h"
#include "utils.h"

#include <iostream>
#include <memory>
#include <csignal>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <chrono>
#include <thread>

std::atomic<bool> g_running{true};

void signal_handler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        Logger::info("Main", "Caught signal " + std::to_string(signum) +
                             ", shutting down...");
        g_running = false;
    }
}

int main(int argc, char** argv) {
    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);

    // -------------------------------------------------------------------------
    // 1. Load configuration
    // -------------------------------------------------------------------------
    const std::string config_path =
        (argc >= 2) ? argv[1] : "/etc/seeway_interface/daemon.conf";

    DaemonConfig cfg;
    if (!load_daemon_config(config_path, cfg)) {
        std::cerr << "Error: Failed to load configuration from '"
                  << config_path << "'\n";
        return 1;
    }

    // -------------------------------------------------------------------------
    // 2. Initialize logger (must come right after config load)
    // -------------------------------------------------------------------------
    Logger::init(cfg.log_file,
                 static_cast<Logger::Level>(cfg.log_level));

    Logger::info("Main", "Configuration loaded from '" + config_path + "'");
    Logger::info("Main", "  transport  : " + cfg.transport);
    if (cfg.transport == "tcp") {
        Logger::info("Main", "  tcp        : " + cfg.server_ip + ":" +
                              std::to_string(cfg.server_port));
    } else {
        Logger::info("Main", "  uart       : " + cfg.uart_device + " @ " +
                              std::to_string(cfg.baud_rate));
    }
    Logger::info("Main", "  gpio pins  : " + std::to_string(cfg.gpio_pins.size()));
    Logger::info("Main", "  adc ch     : " + std::to_string(cfg.adc_channels.size()));
    Logger::info("Main", "  power rails: " + std::to_string(cfg.power_rails.size()));

    Logger::info("Main", "Starting Seeway Interface Daemon (T113i)...");

    // -------------------------------------------------------------------------
    // 3. Initialize subsystems
    // -------------------------------------------------------------------------
    GpioController gpio;
    SensorReader   sensor_reader(10); // 10 Hz
    PowerManager   power_manager;
    InputHandler   input_handler;

    // Communication channel selected by config
    std::unique_ptr<IChannel> channel;
    if (cfg.transport == "uart") {
        channel = std::make_unique<UartChannel>(cfg.uart_device, cfg.baud_rate);
    } else {
        channel = std::make_unique<TcpClientChannel>(
            cfg.server_ip, cfg.server_port, cfg.reconnect_ms);
    }
    SerialComm comm(std::move(channel));

    // Task executor
    TaskContext ctx;
    ctx.comm  = &comm;
    ctx.power = &power_manager;
    ctx.gpio  = &gpio;
    TaskExecutor task_executor(ctx);

    // Connection monitor
    ConnectionMonitor conn_monitor(cfg.heartbeat_timeout_ms);

    // -------------------------------------------------------------------------
    // 4. Configure GPIO / PWM from config
    // -------------------------------------------------------------------------
    for (const auto& p : cfg.gpio_pins) {
        gpio.add_pin({p.bank, p.pin, p.linux_num, p.is_output, p.active_low, p.label});
    }
    if (!cfg.gpio_pins.empty()) {
        gpio.add_pwm_channel({0, "/sys/class/pwm/pwmchip0", 0});
    }

    // -------------------------------------------------------------------------
    // 5. Configure ADC channels from config
    // -------------------------------------------------------------------------
    for (const auto& c : cfg.adc_channels) {
        sensor_reader.add_adc_channel(
            {c.index, c.sysfs_path, c.scale, c.offset, c.unit});
    }

    // -------------------------------------------------------------------------
    // 6. Configure power rails from config
    // -------------------------------------------------------------------------
    for (const auto& r : cfg.power_rails) {
        power_manager.add_rail(
            {static_cast<PowerCmd>(r.on_cmd_id),
             static_cast<PowerCmd>(r.off_cmd_id),
             r.gpio_num, r.active_low, r.min_on_ms, r.sequence_delay_ms});
    }

    // -------------------------------------------------------------------------
    // 7. Wire command handlers: Serial -> Components
    // -------------------------------------------------------------------------

    // seq-based pending task tracking (replaces task_id keyed map)
    // seq → {task_id, issue_time}
    struct PendingTask {
        uint8_t  task_id;
        std::chrono::steady_clock::time_point issued_at;
    };
    std::mutex pending_tasks_mutex;
    std::unordered_map<uint16_t /*seq*/, PendingTask> pending_tasks;

    // Heartbeat from Jetson
    comm.register_handler(MSG_HEARTBEAT,
        [&conn_monitor](MsgId, uint16_t, const uint8_t*, uint16_t) {
            conn_monitor.on_heartbeat_received();
        });

    // Handshake request from Jetson
    comm.register_handler(MSG_HANDSHAKE_REQ,
        [&comm](MsgId, uint16_t /*seq*/, const uint8_t* pay, uint16_t len) {
            if (len < sizeof(HandshakeReqPayload)) {
                Logger::warn("Main", "MSG_HANDSHAKE_REQ: short payload");
                return;
            }
            const auto* req = reinterpret_cast<const HandshakeReqPayload*>(pay);
            if (req->version != PROTOCOL_VERSION) {
                Logger::warn("Main",
                    "Peer protocol version mismatch: peer=0x" +
                    to_hex(req->version, 4) +
                    " local=0x" +
                    to_hex(static_cast<uint16_t>(PROTOCOL_VERSION), 4));
            }
            HandshakeAckPayload ack{};
            ack.version             = PROTOCOL_VERSION;
            ack.negotiated_features = req->supported_features & FEAT_ALL;
            ack.role                = 0; // T113i
            comm.send_payload(MSG_HANDSHAKE_ACK, ack);
            Logger::info("Main", "Handshake completed with peer");
        });

    comm.register_handler(MSG_DO_COMMAND,
        [&gpio, &comm](MsgId, uint16_t seq, const uint8_t* pay, uint16_t len) {
            if (len != sizeof(DoCommandPayload)) {
                Logger::warn("Main", "MSG_DO_COMMAND: unexpected len " +
                             std::to_string(len));
                return;
            }
            const auto* cmd = reinterpret_cast<const DoCommandPayload*>(pay);
            bool ok = gpio.apply_do_command(*cmd);
            AckPayload ack{MSG_DO_COMMAND, seq, ok ? uint8_t(0) : uint8_t(1)};
            comm.send_payload(MSG_ACK, ack);
        });

    comm.register_handler(MSG_PWM_COMMAND,
        [&gpio, &comm](MsgId, uint16_t seq, const uint8_t* pay, uint16_t len) {
            if (len != sizeof(PwmCommandPayload)) {
                Logger::warn("Main", "MSG_PWM_COMMAND: unexpected len " +
                             std::to_string(len));
                return;
            }
            const auto* cmd = reinterpret_cast<const PwmCommandPayload*>(pay);
            bool ok = gpio.apply_pwm_command(*cmd);
            AckPayload ack{MSG_PWM_COMMAND, seq, ok ? uint8_t(0) : uint8_t(1)};
            comm.send_payload(MSG_ACK, ack);
        });

    comm.register_handler(MSG_POWER_COMMAND,
        [&power_manager, &comm](MsgId, uint16_t seq, const uint8_t* pay, uint16_t len) {
            if (len != sizeof(PowerCommandPayload)) {
                Logger::warn("Main", "MSG_POWER_COMMAND: unexpected len " +
                             std::to_string(len));
                return;
            }
            const auto* cmd = reinterpret_cast<const PowerCommandPayload*>(pay);
            bool ok = power_manager.apply_power_command(*cmd);
            AckPayload ack{MSG_POWER_COMMAND, seq, ok ? uint8_t(0) : uint8_t(1)};
            comm.send_payload(MSG_ACK, ack);
        });

    comm.register_handler(MSG_TASK_COMMAND,
        [&task_executor, &comm, &pending_tasks_mutex, &pending_tasks]
        (MsgId, uint16_t seq, const uint8_t* pay, uint16_t len) {
            if (len != sizeof(TaskCommandPayload)) {
                Logger::warn("Main", "MSG_TASK_COMMAND: unexpected len " +
                             std::to_string(len));
                return;
            }
            const auto* cmd = reinterpret_cast<const TaskCommandPayload*>(pay);

            // Check queue capacity before accepting
            if (task_executor.queue_size() >= TaskExecutor::MAX_QUEUE_DEPTH) {
                Logger::warn("Main", "Task queue full, rejecting task " +
                             std::to_string(cmd->task_id));
                AckPayload ack{MSG_TASK_COMMAND, seq, 2}; // 2 = queue full
                comm.send_payload(MSG_ACK, ack);
                return;
            }

            // Track pending task by sequence number
            {
                std::lock_guard<std::mutex> lk(pending_tasks_mutex);
                pending_tasks[seq] = {static_cast<uint8_t>(cmd->task_id),
                                      std::chrono::steady_clock::now()};
            }

            task_executor.enqueue(*cmd);
            AckPayload ack{MSG_TASK_COMMAND, seq, 0};
            comm.send_payload(MSG_ACK, ack);
        });

    // -------------------------------------------------------------------------
    // 8. Wire component output: Components -> Serial
    // -------------------------------------------------------------------------
    sensor_reader.set_callback([&comm](const SensorDataPayload& data) {
        comm.send_payload(MSG_SENSOR_DATA, data);
    });

    gpio.set_event_callback([&comm](const EventPayload& evt) {
        comm.send_payload(MSG_EVENT, evt);
    });

    input_handler.set_callback(
        [&task_executor, &comm](const EventPayload& evt) {
            comm.send_payload(MSG_EVENT, evt);
            if (evt.type == EVT_GPIO_FALLING && evt.code == 64) {
                TaskCommandPayload tcmd{};
                tcmd.task_id = TASK_ESTOP;
                task_executor.enqueue(tcmd);
            }
        });

    task_executor.set_result_callback(
        [&comm, &pending_tasks_mutex, &pending_tasks]
        (const TaskResponsePayload& resp_in) {
            TaskResponsePayload resp = resp_in;

            // Look up the original request sequence number
            {
                std::lock_guard<std::mutex> lk(pending_tasks_mutex);
                const auto now = std::chrono::steady_clock::now();
                // Clean up timed-out entries first
                for (auto it = pending_tasks.begin(); it != pending_tasks.end(); ) {
                    auto age_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - it->second.issued_at).count();
                    if (age_ms > TaskExecutor::TASK_TIMEOUT_MS) {
                        Logger::warn("Main",
                            "Task seq=" + std::to_string(it->first) +
                            " timed out after " + std::to_string(age_ms) + " ms");
                        it = pending_tasks.erase(it);
                    } else {
                        ++it;
                    }
                }
                // Find matching entry by task_id
                for (auto it = pending_tasks.begin(); it != pending_tasks.end(); ++it) {
                    if (it->second.task_id == static_cast<uint8_t>(resp.task_id)) {
                        resp.acked_seq = it->first;
                        pending_tasks.erase(it);
                        break;
                    }
                }
            }

            resp.message[sizeof(resp.message) - 1] = '\0';
            comm.send_payload(MSG_TASK_RESPONSE, resp);
        });

    // -------------------------------------------------------------------------
    // 9. Start all modules (with retry on failure)
    // -------------------------------------------------------------------------
    bool all_ok = true;
    all_ok &= ModuleInitializer::start_with_retry(gpio,          "GpioController");
    all_ok &= ModuleInitializer::start_with_retry(power_manager, "PowerManager");
    all_ok &= ModuleInitializer::start_with_retry(input_handler, "InputHandler");
    all_ok &= ModuleInitializer::start_with_retry(task_executor, "TaskExecutor");
    all_ok &= ModuleInitializer::start_with_retry(sensor_reader, "SensorReader");

    // comm.start() is expected to retry internally (TCP reconnect thread)
    if (!comm.start()) {
        Logger::warn("Main",
            "SerialComm failed initial connect; will retry internally");
    }

    if (!all_ok) {
        Logger::error("Main",
            "One or more critical modules failed to start; daemon may be degraded");
    }

    Logger::info("Main", "Daemon initialized and running");

    // Send initial handshake when connected
    bool handshake_sent = false;

    // -------------------------------------------------------------------------
    // 10. Main loop: heartbeat + periodic status + connection monitoring
    //     Runs at 500 ms to reduce CPU consumption (was 100 ms).
    // -------------------------------------------------------------------------
    static constexpr int LOOP_STEP_MS   = 500;
    static constexpr int HEARTBEAT_MS   = 1000;
    uint32_t ms_counter     = 0;
    uint32_t reconnect_wait = 0;

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(LOOP_STEP_MS));
        ms_counter += LOOP_STEP_MS;

        bool connected = comm.is_connected();

        // Send handshake once after connection is established
        if (connected && !handshake_sent) {
            HandshakeReqPayload hs{};
            hs.version             = PROTOCOL_VERSION;
            hs.supported_features  = FEAT_ALL;
            hs.timestamp_ms        = 0;
            comm.send_payload(MSG_HANDSHAKE_REQ, hs);
            handshake_sent = true;
            conn_monitor.reset();
            Logger::info("Main", "Sent handshake to peer");
        } else if (!connected) {
            handshake_sent = false;
        }

        // Connection health check
        bool healthy = conn_monitor.check_health(connected);
        if (!healthy && connected) {
            // Heartbeat timeout: force reconnect
            reconnect_wait += LOOP_STEP_MS;
            if (reconnect_wait >= static_cast<uint32_t>(cfg.reconnect_ms)) {
                conn_monitor.on_reconnect_attempt();
                comm.reconnect();
                reconnect_wait = 0;
            }
        } else if (healthy) {
            reconnect_wait = 0;
        }

        // Periodic heartbeat + status snapshot (once per HEARTBEAT_MS)
        if (ms_counter >= HEARTBEAT_MS) {
            ms_counter = 0;
            if (connected) {
                HeartbeatPayload hb{0, 0};
                comm.send_payload(MSG_HEARTBEAT, hb);

                auto status = gpio.snapshot();
                comm.send_payload(MSG_GPIO_STATUS, status);

                SystemStatusPayload sys{0, 10, 45.0f, 102400};
                comm.send_payload(MSG_SYSTEM_STATUS, sys);
            }
        }
    }

    // -------------------------------------------------------------------------
    // 11. Graceful shutdown (ordered, with task drain)
    // -------------------------------------------------------------------------
    ShutdownManager shutdown;
    shutdown.add_step("Stop InputHandler",  [&]{ input_handler.stop(); });
    shutdown.add_step("Stop SensorReader",  [&]{ sensor_reader.stop(); });
    shutdown.add_step("Flush SerialComm",   [&]{ comm.flush_pending(3000); });
    shutdown.add_step("Stop TaskExecutor",  [&]{ task_executor.stop(); });
    shutdown.add_step("Stop PowerManager",  [&]{ power_manager.stop(); });
    shutdown.add_step("Stop GpioController",[&]{ gpio.stop(); });
    shutdown.add_step("Stop SerialComm",    [&]{ comm.stop(); });
    shutdown.add_step("Flush Logger",       [&]{ Logger::shutdown(); });

    shutdown.drain_then_execute(
        "TaskExecutor queue",
        [&]{ return task_executor.is_queue_empty(); },
        5000);

    return 0;
}

