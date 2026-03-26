# Architecture Overview

This document describes the system design, module interactions, data flow,
and thread model for the **Seeway Interface Daemon** running on the
Allwinner T113i interface board.

---

## System Context

```
┌─────────────────────────────┐         ┌──────────────────────────────┐
│      T113i Interface Board  │         │      Jetson Orin NX          │
│                             │  TCP /  │                              │
│  ┌──────────────────────┐   │  UART   │  ┌──────────────────────┐   │
│  │  seeway_interface_   │◄──┼─────────┼─►│  seeway_interface_   │   │
│  │     daemon           │   │         │  │     driver (ROS2)    │   │
│  └──────┬───────────────┘   │         │  └──────────────────────┘   │
│         │                   │         │                              │
│   ┌─────▼──────────────┐    │         │  ROS2 Topics / Services:    │
│   │  GPIO / PWM sysfs  │    │         │  /seeway/sensor_data        │
│   │  ADC  iio sysfs    │    │         │  /seeway/gpio_status        │
│   │  UART / SPI / I2C  │    │         │  /seeway/task_command       │
│   └────────────────────┘    │         │  /seeway/power_control      │
└─────────────────────────────┘         └──────────────────────────────┘
```

---

## Module Architecture

```
main.cpp
│
├── ConfigLoader          – Parses daemon.conf (INI format)
├── Logger                – Centralized timestamped logging
├── ModuleInitializer     – Retry-with-backoff for module start()
│
├── SerialComm            – Frame codec + channel wrapper
│   ├── UartChannel       – UART/USB-CDC transport
│   └── TcpClientChannel  – TCP client (connects to Jetson)
│
├── GpioController        – Digital I/O + PWM via sysfs
├── SensorReader          – ADC sampling + callback dispatch
├── PowerManager          – Power rail switching with min-on-time
├── InputHandler          – Button/event input monitoring
├── TaskExecutor          – Async task queue (worker thread)
│   └── TaskContext       – Shared pointers to subsystems
│
├── ConnectionMonitor     – Heartbeat-based health checking
└── ShutdownManager       – Ordered graceful shutdown
```

---

## Data Flow

### Inbound (Jetson → T113i)

```
TCP/UART bytes
    │
    ▼
FrameCodec::feed()
    │  (CRC check, frame reassembly)
    ▼
SerialComm handler dispatch
    │
    ├── MSG_HEARTBEAT      → ConnectionMonitor::on_heartbeat_received()
    ├── MSG_HANDSHAKE_REQ  → send MSG_HANDSHAKE_ACK
    ├── MSG_DO_COMMAND     → GpioController::apply_do_command()
    ├── MSG_PWM_COMMAND    → GpioController::apply_pwm_command()
    ├── MSG_POWER_COMMAND  → PowerManager::apply_power_command()
    └── MSG_TASK_COMMAND   → TaskExecutor::enqueue()
```

### Outbound (T113i → Jetson)

```
SensorReader (10 Hz)         → MSG_SENSOR_DATA
GpioController (input edge)  → MSG_EVENT
InputHandler (button/touch)  → MSG_EVENT
TaskExecutor result callback → MSG_TASK_RESPONSE
Main loop (1 Hz)             → MSG_HEARTBEAT, MSG_GPIO_STATUS, MSG_SYSTEM_STATUS
```

---

## Thread Model

| Thread | Owner | Purpose |
|--------|-------|---------|
| Main   | main.cpp | Main loop (500 ms), shutdown |
| IO     | TcpClientChannel / UartChannel | TX/RX, reconnect |
| Worker | TaskExecutor | Execute task queue |
| Poll   | GpioController | Poll input GPIO edges |
| Sensor | SensorReader | ADC sampling at 10 Hz |

All cross-thread communication uses:
- `std::mutex` + `std::lock_guard` for shared data
- `std::atomic<>` for state flags
- `std::condition_variable` for worker wake-up

---

## Protocol Handshake Sequence

```
T113i                              Jetson
  │                                   │
  │──── TCP connect ─────────────────►│
  │                                   │
  │──── MSG_HANDSHAKE_REQ ───────────►│
  │     {version=0x0100,              │
  │      features=FEAT_ALL}           │
  │                                   │
  │◄─── MSG_HANDSHAKE_ACK ───────────│
  │     {version=0x0100,              │
  │      negotiated_features,         │
  │      role=1}                      │
  │                                   │
  │◄──► MSG_HEARTBEAT (1 Hz) ────────►│
  │◄──► MSG_SENSOR_DATA (10 Hz) ─────►│
  │                                   │
```

---

## Configuration → Module Wiring

`DaemonConfig` (parsed from `daemon.conf`) drives:
- `GpioController`: `GpioPinConfig` list → `GpioPin` objects
- `SensorReader`: `AdcChannelConfig` list → ADC channels
- `PowerManager`: `PowerRailConfig` list → `PowerRail` objects
- `SerialComm`: transport selection (TCP / UART) + connection params
- `Logger`: log file path + level
- `ConnectionMonitor`: `heartbeat_timeout_ms`
- `TcpClientChannel`: `reconnect_ms`

---

## Error Handling Strategy

| Scenario | Strategy |
|----------|----------|
| Module start() fails | Retry up to 3× with exponential backoff (ModuleInitializer) |
| TCP connection lost | TcpClientChannel reconnects automatically with `reconnect_ms` interval |
| Heartbeat timeout | ConnectionMonitor detects; main loop triggers reconnect |
| Task queue full | Reject with status=2 ACK; log WARN |
| Invalid TaskContext | Report error result; log ERROR |
| Config parse error | Print to stderr; return false → daemon exits |
| Shutdown signal | ShutdownManager drains tasks, flushes messages, stops modules in order |
