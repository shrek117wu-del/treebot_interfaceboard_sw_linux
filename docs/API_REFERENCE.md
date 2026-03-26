# API Reference

This document provides a high-level summary of the public API for the
Seeway Interface Daemon.  For complete, auto-generated documentation with
class diagrams and method signatures, generate Doxygen output:

```bash
cd t113i_daemon
doxygen Doxyfile
# Open: docs/doxygen/html/index.html
```

---

## Core Modules

### Logger
```cpp
#include "logger.h"

// Initialize (call once at startup)
Logger::init("/var/log/seeway_daemon.log", Logger::INFO);

// Log messages
Logger::debug("Tag", "message");
Logger::info("Tag", "message");
Logger::warn("Tag", "message");
Logger::error("Tag", "message");
Logger::fatal("Tag", "message");

// Shutdown (flush and close file)
Logger::shutdown();
```

**Log Levels:** `DEBUG=0`, `INFO=1`, `WARN=2`, `ERROR=3`, `FATAL=4`

**Thread-safe:** Yes (internal mutex)

---

### ConfigLoader
```cpp
#include "config_loader.h"

DaemonConfig cfg;
bool ok = load_daemon_config("/etc/seeway_interface/daemon.conf", cfg);
// cfg.transport, cfg.server_ip, cfg.gpio_pins, etc.
```

See [CONFIGURATION.md](CONFIGURATION.md) for all fields.

---

### SerialComm
```cpp
#include "serial_comm.h"

// Create with a channel
auto ch = std::make_unique<TcpClientChannel>("192.168.1.50", 9000, 2000);
SerialComm comm(std::move(ch));

// Register inbound message handler
comm.register_handler(MSG_HEARTBEAT,
    [](MsgId id, uint16_t seq, const uint8_t* pay, uint16_t len) {
        // handle heartbeat
    });

// Send a typed payload
HeartbeatPayload hb{};
comm.send_payload(MSG_HEARTBEAT, hb);

comm.start();
// ...
comm.stop();
```

---

### ConnectionMonitor
```cpp
#include "connection_monitor.h"

ConnectionMonitor mon(5000); // 5000 ms heartbeat timeout

// Call when a heartbeat is received
mon.on_heartbeat_received();

// Call periodically from main loop
bool healthy = mon.check_health(comm.is_connected());

// Query state
mon.state();           // DISCONNECTED / CONNECTING / CONNECTED / TIMEOUT
mon.state_str();       // "CONNECTED"
mon.reconnect_attempts();
mon.ms_since_last_heartbeat();

// Reset on reconnect
mon.reset();
```

---

### ModuleInitializer
```cpp
#include "module_initializer.h"

// Retry up to MAX_RETRIES=3 times with exponential backoff
bool ok = ModuleInitializer::start_with_retry(gpio, "GpioController");
```

---

### TaskExecutor
```cpp
#include "task_executor.h"

TaskContext ctx{&comm, &power_manager, &gpio};
TaskExecutor exec(ctx);

exec.set_result_callback([](const TaskResponsePayload& r) { /* ... */ });
exec.start();

TaskCommandPayload cmd{};
cmd.task_id = TASK_ESTOP;
exec.enqueue(cmd);

exec.stop();
```

**Queue depth:** `MAX_QUEUE_DEPTH = 64`

**Timeout:** `TASK_TIMEOUT_MS = 30000`

---

### ShutdownManager
```cpp
#include "shutdown_manager.h"

ShutdownManager sm;
sm.add_step("Stop InputHandler",  [&]{ input.stop(); });
sm.add_step("Stop TaskExecutor",  [&]{ task.stop(); });
// ...

// Drain task queue (max 5 s), then execute shutdown steps
sm.drain_then_execute(
    "TaskExecutor queue",
    [&]{ return task.is_queue_empty(); },
    5000);
```

---

## Protocol Types

See [PROTOCOL.md](PROTOCOL.md) for the complete binary specification.

### Key constants
```cpp
#define PROTOCOL_VERSION 0x0100

// Message IDs
MSG_HEARTBEAT       = 0x01
MSG_HANDSHAKE_REQ   = 0x03
MSG_HANDSHAKE_ACK   = 0x04
MSG_TASK_COMMAND    = 0x23
MSG_TASK_RESPONSE   = 0x24
// ...

// Feature bits
FEAT_GPIO   = (1 << 0)
FEAT_ADC    = (1 << 1)
FEAT_POWER  = (1 << 2)
FEAT_TASK   = (1 << 3)
FEAT_INPUT  = (1 << 4)
FEAT_ALL    = 0x1F
```

### CRC function
```cpp
uint16_t crc = crc16_ccitt(data_ptr, data_len);
```

---

## Utility Functions

```cpp
#include "utils.h"

// Format integer as hex string (uppercase, zero-padded)
std::string hex = to_hex(uint16_t(0x0100), 4);  // → "0100"
std::string hex = to_hex(uint8_t(0xAB));          // → "AB"
```
