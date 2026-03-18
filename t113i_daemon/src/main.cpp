/**
 * @file main.cpp
 * @brief Seeway Interface Daemon for Allwinner T113i
 *
 * This daemon integrates communication (SerialComm over TCP or UART),
 * I/O (GpioController), sensors (SensorReader), power management
 * (PowerManager), local input handling (InputHandler), and a task 
 * execution engine (TaskExecutor).
 */

#include "protocol.h"
#include "serial_comm.h"
#include "gpio_controller.h"
#include "sensor_reader.h"
#include "power_manager.h"
#include "input_handler.h"
#include "task_executor.h"

#include <iostream>
#include <memory>
#include <csignal>
#include <atomic>
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
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::cout << "Starting Seeway Interface Daemon (T113i)...\n";

    // 1. Initialize Components
    GpioController gpio;
    SensorReader sensor_reader(10); // 10 Hz
    PowerManager power_manager;
    InputHandler input_handler;

    // TODO: Read config from file in a real deployment
    // Here we hardcode some mock configuration for the T113i environment

    // Mock Config: Communication over TCP (Client connecting to Jetson at 192.168.1.100:9000)
    // Replace with UartChannel("/dev/ttyS3", 115200) for UART
    auto tcp_channel = std::make_unique<TcpClientChannel>("127.0.0.1", 9000); 
    SerialComm comm(std::move(tcp_channel));

    // 2. Setup Task Executor (needs pointers to other components)
    TaskContext ctx;
    ctx.comm = &comm;
    ctx.power = &power_manager;
    ctx.gpio = &gpio;
    TaskExecutor task_executor(ctx);

    // 3. Configure GPIO/PWM
    gpio.add_pin({0, 0, 10, true, false, "valve_1"});
    gpio.add_pin({0, 1, 11, true, false, "valve_2"});
    gpio.add_pin({2, 0, 64, false, false, "estop_btn"});
    
    gpio.add_pwm_channel({0, "/sys/class/pwm/pwmchip0", 0});

    // 4. Configure Sensors
    sensor_reader.add_adc_channel({0, "/sys/bus/iio/devices/iio:device0/in_voltage0_raw", 0.001f, 0.0f, "V"});
    
    // 5. Configure Power Manager
    // e.g. Jetson NX on GPIO 20, active low, 5000ms min ON time
    power_manager.add_rail({PWR_NX_ON, PWR_NX_OFF, 20, true, 5000, 1000});

    // 6. Hook up callbacks

    // Serial -> Component commands
    comm.register_handler(MSG_DO_COMMAND, [&gpio, &comm](MsgId id, uint16_t seq, const uint8_t* pay, uint16_t len) {
        if (len == sizeof(DoCommandPayload)) {
            const auto* cmd = reinterpret_cast<const DoCommandPayload*>(pay);
            bool ok = gpio.apply_do_command(*cmd);
            AckPayload ack{MSG_DO_COMMAND, seq, ok ? (uint8_t)0 : (uint8_t)1};
            comm.send_payload(MSG_ACK, ack);
        }
    });

    comm.register_handler(MSG_PWM_COMMAND, [&gpio, &comm](MsgId id, uint16_t seq, const uint8_t* pay, uint16_t len) {
        if (len == sizeof(PwmCommandPayload)) {
            const auto* cmd = reinterpret_cast<const PwmCommandPayload*>(pay);
            bool ok = gpio.apply_pwm_command(*cmd);
            AckPayload ack{MSG_PWM_COMMAND, seq, ok ? (uint8_t)0 : (uint8_t)1};
            comm.send_payload(MSG_ACK, ack);
        }
    });

    comm.register_handler(MSG_POWER_COMMAND, [&power_manager, &comm](MsgId id, uint16_t seq, const uint8_t* pay, uint16_t len) {
        if (len == sizeof(PowerCommandPayload)) {
            const auto* cmd = reinterpret_cast<const PowerCommandPayload*>(pay);
            bool ok = power_manager.apply_power_command(*cmd);
            AckPayload ack{MSG_POWER_COMMAND, seq, ok ? (uint8_t)0 : (uint8_t)1};
            comm.send_payload(MSG_ACK, ack);
        }
    });

    comm.register_handler(MSG_TASK_COMMAND, [&task_executor, &comm](MsgId id, uint16_t seq, const uint8_t* pay, uint16_t len) {
        if (len == sizeof(TaskCommandPayload)) {
            const auto* cmd = reinterpret_cast<const TaskCommandPayload*>(pay);
            task_executor.enqueue(*cmd);
            AckPayload ack{MSG_TASK_COMMAND, seq, 0};
            comm.send_payload(MSG_ACK, ack);
        }
    });

    // Components -> Serial updates
    sensor_reader.set_callback([&comm](const SensorDataPayload& data) {
        comm.send_payload(MSG_SENSOR_DATA, data);
    });

    gpio.set_event_callback([&comm](const EventPayload& evt) {
        comm.send_payload(MSG_EVENT, evt);
    });

    input_handler.set_callback([&task_executor, &comm](const EventPayload& evt) {
        // Forward event to Jetson
        comm.send_payload(MSG_EVENT, evt);
        
        // Local logic: E.g., trigger E-STOP task on specific button press
        if (evt.type == EVT_GPIO_FALLING && evt.code == 64) {
            TaskCommandPayload tcmd{};
            tcmd.task_id = TASK_ESTOP;
            task_executor.enqueue(tcmd);
        }
    });

    task_executor.set_result_callback([&comm](const TaskResponsePayload& resp) {
        comm.send_payload(MSG_TASK_RESPONSE, resp);
    });

    // 7. Start Modules
    if (!gpio.start()) std::cerr << "Warn: GpioController start failed\n";
    if (!power_manager.start()) std::cerr << "Warn: PowerManager start failed\n";
    if (!input_handler.start()) std::cerr << "Warn: InputHandler start failed\n";
    if (!task_executor.start()) std::cerr << "Warn: TaskExecutor start failed\n";
    if (!sensor_reader.start()) std::cerr << "Warn: SensorReader start failed\n";
    if (!comm.start()) {
        std::cerr << "Error: SerialComm failed to start/connect. We will continue anyway and attempt reconnects internally.\n";
    }

    std::cout << "Daemon initialized and running.\n";

    // 8. Main Loop: Send Heartbeats, read GPIO snapshot
    uint32_t ms_counter = 0;
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        ms_counter += 100;

        if (ms_counter >= 1000) {
            ms_counter = 0;
            if (comm.is_connected()) {
                HeartbeatPayload hb{0, 0}; // timestamp, role=T113i
                comm.send_payload(MSG_HEARTBEAT, hb);

                auto status = gpio.snapshot();
                comm.send_payload(MSG_GPIO_STATUS, status);
                
                // Mock system status
                SystemStatusPayload sys{0, 10, 45.0f, 102400};
                comm.send_payload(MSG_SYSTEM_STATUS, sys);
            } else {
                // In a real system, you might trigger a reconnect logic here if using TCP
                // UartChannel typically stays "open" even if the other side is quiet
            }
        }
    }

    std::cout << "Shutting down modules...\n";
    sensor_reader.stop();
    task_executor.stop();
    input_handler.stop();
    power_manager.stop();
    gpio.stop();
    comm.stop();

    std::cout << "Exiting softly.\n";
    return 0;
}
