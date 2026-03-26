# System Architecture

## Overview

The T113i daemon bridges hardware peripherals (GPIO, ADC, power rails, serial
input) on the T113i SoC to the Jetson Orin NX running the ROS 2 control stack,
via TCP/UART using a custom binary protocol.

```
┌─────────────────────────────────────────────────────────────────┐
│                         T113i SoC                               │
│                                                                 │
│  ┌─────────────┐   ┌──────────────┐   ┌──────────────────────┐ │
│  │ GpioCtrl    │   │ SensorReader │   │ PowerManager         │ │
│  │ (sysfs PWM) │   │ (IIO ADC)    │   │ (PWR rails)          │ │
│  └──────┬──────┘   └──────┬───────┘   └──────────┬───────────┘ │
│         │                 │                       │             │
│         └─────────────────┼───────────────────────┘             │
│                           │ callbacks                           │
│                    ┌──────▼──────┐                              │
│                    │  main.cpp   │◄──── signal handler          │
│                    │  main loop  │      (SIGINT/SIGTERM)        │
│                    └──────┬──────┘                              │
│                           │                                     │
│         ┌─────────────────┼────────────────────┐               │
│         │                 │                    │               │
│  ┌──────▼──────┐  ┌───────▼──────┐  ┌─────────▼──────┐        │
│  │TaskExecutor │  │SerialComm    │  │ConnectionMonitor│        │
│  │(worker thr) │  │(TCP or UART) │  │(heartbeat FSM)  │        │
│  └─────────────┘  └──────┬───────┘  └─────────────────┘        │
│                          │ binary frames                        │
└──────────────────────────┼─────────────────────────────────────┘
                           │ TCP / UART
┌──────────────────────────┼─────────────────────────────────────┐
│                   Jetson Orin NX                                │
│                          │                                     │
│                   ┌──────▼──────────────┐                      │
│                   │ seeway_driver_node  │  (ROS 2 lifecycle)   │
│                   └─────────────────────┘                      │
└─────────────────────────────────────────────────────────────────┘
```

## Component Descriptions

### SerialComm (`serial_comm.h`)
Communication abstraction layer supporting both TCP and UART.  Provides
`send()` / `recv()` and configurable reconnection logic.

### ConnectionMonitor (`connection_monitor.h`)
Heartbeat-based connection state machine with states:
- `DISCONNECTED` → initial or after `reset()`
- `CONNECTING`   → transport up, awaiting first heartbeat
- `CONNECTED`    → heartbeat received within timeout window
- `TIMEOUT`      → heartbeat not received within `heartbeat_timeout_ms`

Transitions:
```
DISCONNECTED ──check_health(true)──► CONNECTING
CONNECTING   ──on_heartbeat()──────► CONNECTED
CONNECTED    ──timeout expires──────► TIMEOUT
TIMEOUT      ──on_heartbeat()──────► CONNECTED
any state    ──reset()──────────────► DISCONNECTED
```

### TaskExecutor (`task_executor.h`)
Worker-thread task queue for long-running operations (arm_home, estop, valve
control, etc.).  Features:
- **Queue depth**: up to 64 pending tasks
- **Timeout**: 30 s per task
- **Context validity**: null-pointer guard on TaskContext before execution
- **Result callback**: notifies main loop on completion

### ModuleInitializer (`module_initializer.h`)
Generic retry-with-exponential-backoff helper for any module exposing
`bool start()`.  Retries 3 times: 500 ms → 1 000 ms → 2 000 ms.

### ShutdownManager (`shutdown_manager.h`)
Ordered shutdown with named steps and optional task-drain predicate.
Default order used in `main.cpp`:
1. Stop InputHandler
2. Drain TaskExecutor (up to 5 s)
3. Flush SerialComm
4. Stop TaskExecutor
5. Stop PowerManager
6. Stop GpioController
7. Stop SerialComm
8. Shutdown Logger

### Logger (`logger.h`)
Thread-safe centralized logging with:
- Five levels: DEBUG / INFO / WARN / ERROR / FATAL
- ISO-8601 timestamps (`YYYY-MM-DD HH:MM:SS`)
- Optional file output with 10 MB automatic rotation

## Thread Model

| Thread | Responsibility |
|--------|----------------|
| **Main thread** | Configuration, module startup, main loop (500 ms), shutdown |
| **TaskExecutor worker** | Dequeues and executes `TaskCommandPayload` tasks |
| **SensorReader thread** | Periodic ADC/sensor sampling at configurable rate |
| **SerialComm recv thread** | Blocking socket/UART read, dispatches frames to handlers |

All shared state is protected by:
- `std::mutex` + `std::condition_variable` in TaskExecutor
- `std::atomic` in ConnectionMonitor and Logger
- `std::mutex` in Logger file output

## Data Flow

### Inbound (Jetson → T113i)

```
SerialComm::recv_thread()
  └─► parse frame (AA55 sync, CRC-16)
      └─► dispatch on msg_id:
          MSG_HANDSHAKE_REQ  → send HandshakeAck
          MSG_HEARTBEAT      → ConnectionMonitor::on_heartbeat_received()
          MSG_DO_COMMAND     → GpioController::set_output()
          MSG_PWM_COMMAND    → GpioController::set_pwm()
          MSG_POWER_COMMAND  → PowerManager::execute()
          MSG_TASK_COMMAND   → TaskExecutor::enqueue()
```

### Outbound (T113i → Jetson)

```
SensorReader callback  → encode SensorDataPayload  → SerialComm::send()
GpioController callback→ encode GpioStatusPayload  → SerialComm::send()
InputHandler callback  → encode EventPayload        → SerialComm::send()
TaskExecutor result_cb → encode TaskResponsePayload → SerialComm::send()
Main loop (1 s tick)   → encode HeartbeatPayload   → SerialComm::send()
```
