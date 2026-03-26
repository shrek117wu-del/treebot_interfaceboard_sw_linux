/**
 * @file main.cpp
 * @brief Seeway Interface Daemon for Allwinner T113i
 *
 * Reads configuration from daemon.conf (path given on the command line or
 * defaulting to /etc/seeway_interface/daemon.conf) and wires all subsystems
 * together.
 */

#include "protocol.h"
#include "serial_comm.h"
#include "gpio_controller.h"
#include "sensor_reader.h"
#include "power_manager.h"
#include "input_handler.h"
#include "task_executor.h"
#include "config_loader.h"

#include <iostream>
#include <memory>
#include <csignal>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <thread>
#include <chrono>

std::atomic<bool> g_running{true};

void signal_handler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        std::cout << "\nCaught signal " << signum << ", shutting down...\n";
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
    std::cout << "Configuration loaded from '" << config_path << "'\n";
    std::cout << "  transport : " << cfg.transport << "\n";
    if (cfg.transport == "tcp") {
        std::cout << "  tcp       : " << cfg.server_ip << ":" << cfg.server_port << "\n";
    } else {
        std::cout << "  uart      : " << cfg.uart_device << " @ " << cfg.baud_rate << "\n";
    }
    std::cout << "  gpio pins : " << cfg.gpio_pins.size() << "\n";
    std::cout << "  adc ch    : " << cfg.adc_channels.size() << "\n";
    std::cout << "  power rails: " << cfg.power_rails.size() << "\n";

    std::cout << "Starting Seeway Interface Daemon (T113i)...\n";

    // -------------------------------------------------------------------------
    // 2. Initialize subsystems
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
        channel = std::make_unique<TcpClientChannel>(cfg.server_ip, cfg.server_port);
    }
    SerialComm comm(std::move(channel));

    // Task executor
    TaskContext ctx;
    ctx.comm  = &comm;
    ctx.power = &power_manager;
    ctx.gpio  = &gpio;
    TaskExecutor task_executor(ctx);

    // -------------------------------------------------------------------------
    // 3. Configure GPIO / PWM from config
    // -------------------------------------------------------------------------
    for (const auto& p : cfg.gpio_pins) {
        gpio.add_pin({p.bank, p.pin, p.linux_num, p.is_output, p.active_low, p.label});
    }
    if (!cfg.gpio_pins.empty()) {
        // Always add default PWM channel (chip0 / channel0) if any GPIO was configured
        gpio.add_pwm_channel({0, "/sys/class/pwm/pwmchip0", 0});
    }

    // -------------------------------------------------------------------------
    // 4. Configure ADC channels from config
    // -------------------------------------------------------------------------
    for (const auto& c : cfg.adc_channels) {
        sensor_reader.add_adc_channel(
            {c.index, c.sysfs_path, c.scale, c.offset, c.unit});
    }

    // -------------------------------------------------------------------------
    // 5. Configure power rails from config
    // -------------------------------------------------------------------------
    for (const auto& r : cfg.power_rails) {
        power_manager.add_rail(
            {static_cast<PowerCmd>(r.on_cmd_id),
             static_cast<PowerCmd>(r.off_cmd_id),
             r.gpio_num, r.active_low, r.min_on_ms, r.sequence_delay_ms});
    }

    // -------------------------------------------------------------------------
    // 6. Wire command handlers: Serial -> Components
    // -------------------------------------------------------------------------

    // seq→task_id tracking for acked_seq in TaskResponsePayload
    std::mutex pending_tasks_mutex;
    // task_id → seq: allows O(1) lookup by task_id when populating acked_seq.
    // Limitation: if two concurrent commands share the same task_id the older
    // entry is overwritten; this is acceptable in practice.
    std::unordered_map<uint8_t /*task_id*/, uint16_t /*seq*/> pending_task_seqs;

    comm.register_handler(MSG_DO_COMMAND,
        [&gpio, &comm](MsgId id, uint16_t seq, const uint8_t* pay, uint16_t len) {
            if (len != sizeof(DoCommandPayload)) {
                std::cerr << "[daemon] MSG_DO_COMMAND: unexpected len " << len << "\n";
                return;
            }
            const auto* cmd = reinterpret_cast<const DoCommandPayload*>(pay);
            bool ok = gpio.apply_do_command(*cmd);
            AckPayload ack{MSG_DO_COMMAND, seq, ok ? (uint8_t)0 : (uint8_t)1};
            comm.send_payload(MSG_ACK, ack);
        });

    comm.register_handler(MSG_PWM_COMMAND,
        [&gpio, &comm](MsgId id, uint16_t seq, const uint8_t* pay, uint16_t len) {
            if (len != sizeof(PwmCommandPayload)) {
                std::cerr << "[daemon] MSG_PWM_COMMAND: unexpected len " << len << "\n";
                return;
            }
            const auto* cmd = reinterpret_cast<const PwmCommandPayload*>(pay);
            bool ok = gpio.apply_pwm_command(*cmd);
            AckPayload ack{MSG_PWM_COMMAND, seq, ok ? (uint8_t)0 : (uint8_t)1};
            comm.send_payload(MSG_ACK, ack);
        });

    comm.register_handler(MSG_POWER_COMMAND,
        [&power_manager, &comm](MsgId id, uint16_t seq, const uint8_t* pay, uint16_t len) {
            if (len != sizeof(PowerCommandPayload)) {
                std::cerr << "[daemon] MSG_POWER_COMMAND: unexpected len " << len << "\n";
                return;
            }
            const auto* cmd = reinterpret_cast<const PowerCommandPayload*>(pay);
            bool ok = power_manager.apply_power_command(*cmd);
            AckPayload ack{MSG_POWER_COMMAND, seq, ok ? (uint8_t)0 : (uint8_t)1};
            comm.send_payload(MSG_ACK, ack);
        });

    comm.register_handler(MSG_TASK_COMMAND,
        [&task_executor, &comm, &pending_tasks_mutex, &pending_task_seqs]
        (MsgId id, uint16_t seq, const uint8_t* pay, uint16_t len) {
            if (len != sizeof(TaskCommandPayload)) {
                std::cerr << "[daemon] MSG_TASK_COMMAND: unexpected len " << len << "\n";
                return;
            }
            const auto* cmd = reinterpret_cast<const TaskCommandPayload*>(pay);
            // Store seq so result callback can echo it back
            {
                std::lock_guard<std::mutex> lk(pending_tasks_mutex);
                pending_task_seqs[static_cast<uint8_t>(cmd->task_id)] = seq;
            }
            task_executor.enqueue(*cmd);
            // Immediately ACK enqueue success
            AckPayload ack{MSG_TASK_COMMAND, seq, 0};
            comm.send_payload(MSG_ACK, ack);
        });

    // -------------------------------------------------------------------------
    // 7. Wire component output: Components -> Serial
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
        [&comm, &pending_tasks_mutex, &pending_task_seqs]
        (const TaskResponsePayload& resp_in) {
            TaskResponsePayload resp = resp_in;
            // Echo back the original request seq so Jetson can match
            {
                std::lock_guard<std::mutex> lk(pending_tasks_mutex);
                auto it = pending_task_seqs.find(
                    static_cast<uint8_t>(resp.task_id));
                if (it != pending_task_seqs.end()) {
                    resp.acked_seq = it->second;
                    pending_task_seqs.erase(it);
                }
            }
            // Ensure message is NUL-terminated
            resp.message[sizeof(resp.message) - 1] = '\0';
            comm.send_payload(MSG_TASK_RESPONSE, resp);
        });

    // -------------------------------------------------------------------------
    // 8. Start all modules
    // -------------------------------------------------------------------------
    if (!gpio.start())         std::cerr << "Warn: GpioController start failed\n";
    if (!power_manager.start()) std::cerr << "Warn: PowerManager start failed\n";
    if (!input_handler.start()) std::cerr << "Warn: InputHandler start failed\n";
    if (!task_executor.start()) std::cerr << "Warn: TaskExecutor start failed\n";
    if (!sensor_reader.start()) std::cerr << "Warn: SensorReader start failed\n";
    if (!comm.start()) {
        std::cerr << "Warn: SerialComm failed to start/connect."
                     " Will retry internally.\n";
    }

    std::cout << "Daemon initialized and running.\n";

    // -------------------------------------------------------------------------
    // 9. Main loop: heartbeat + periodic status snapshot
    // -------------------------------------------------------------------------
    uint32_t ms_counter = 0;
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        ms_counter += 100;

        if (ms_counter >= 1000) {
            ms_counter = 0;
            if (comm.is_connected()) {
                HeartbeatPayload hb{0, 0}; // timestamp=0 (T113i role)
                comm.send_payload(MSG_HEARTBEAT, hb);

                auto status = gpio.snapshot();
                comm.send_payload(MSG_GPIO_STATUS, status);

                SystemStatusPayload sys{0, 10, 45.0f, 102400};
                comm.send_payload(MSG_SYSTEM_STATUS, sys);
            }
        }
    }

    // -------------------------------------------------------------------------
    // 10. Graceful shutdown
    // -------------------------------------------------------------------------
    std::cout << "Shutting down modules...\n";
    sensor_reader.stop();
    task_executor.stop();
    input_handler.stop();
    power_manager.stop();
    gpio.stop();
    comm.stop();

    std::cout << "Exiting.\n";
    return 0;
}
