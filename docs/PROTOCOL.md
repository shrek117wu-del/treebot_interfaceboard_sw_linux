# Binary Protocol Specification

## Frame Layout

```
 Offset  Size  Field         Description
 ------  ----  -----------   -------------------------------------------
   0       1   sync[0]       Always 0xAA
   1       1   sync[1]       Always 0x55
   2       1   msg_id        Message type identifier (uint8_t)
   3       2   seq           Sequence number, little-endian (uint16_t)
   5       2   length        Payload length in bytes, LE (uint16_t)
   7       2   reserved      Reserved, set to 0x0000
   9     len   payload       Variable-length payload (max 256 bytes)
 9+len    2   crc16         CRC-16/CCITT over bytes [0 .. 9+len-1], LE
```

All multi-byte integers are **little-endian**.

**Maximum frame size**: `9 + 256 + 2 = 267 bytes`

## CRC-16/CCITT Algorithm

Polynomial `0x1021`, initial value `0xFFFF`.

```c
uint16_t crc16_ccitt(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++)
            crc = (crc & 0x8000u) ? (crc << 1) ^ 0x1021u : (crc << 1);
    }
    return crc;
}
```

Known test vector: `crc16_ccitt("123456789", 9) == 0x29B1`

## Message Type Reference

| ID   | Name             | Direction    | Description                         |
|------|------------------|--------------|-------------------------------------|
| 0x01 | HEARTBEAT        | Both         | Keepalive with timestamp and role   |
| 0x02 | ACK              | Jetson→T113i | Acknowledge a received command      |
| 0x03 | HANDSHAKE_REQ    | T113i→Jetson | Protocol version + feature bitmap   |
| 0x04 | HANDSHAKE_ACK    | Jetson→T113i | Negotiated version + features       |
| 0x10 | SENSOR_DATA      | T113i→Jetson | ADC + temperature/humidity readings |
| 0x11 | GPIO_STATUS      | T113i→Jetson | GPIO bank input/output state        |
| 0x12 | BATTERY_STATUS   | T113i→Jetson | Voltage, current, SoC, temperature  |
| 0x13 | SYSTEM_STATUS    | T113i→Jetson | Uptime, CPU load, free memory       |
| 0x20 | DO_COMMAND       | Jetson→T113i | Digital output control (GPIO bank)  |
| 0x21 | PWM_COMMAND      | Jetson→T113i | PWM channel frequency + duty cycle  |
| 0x22 | POWER_COMMAND    | Jetson→T113i | Power rail on/off/reset             |
| 0x23 | TASK_COMMAND     | Jetson→T113i | Enqueue an async task               |
| 0x24 | TASK_RESPONSE    | T113i→Jetson | Task completion status              |
| 0x30 | EVENT            | T113i→Jetson | Keyboard/touch/GPIO edge event      |

## Payload Structures

All structures use `#pragma pack(push,1)` (no padding).

### HEARTBEAT (0x01)
```c
typedef struct {
    uint32_t timestamp_ms;  // ms since boot (wraps at ~49 days)
    uint8_t  role;          // 0 = T113i, 1 = Jetson
} HeartbeatPayload;  // 5 bytes
```

### ACK (0x02)
```c
typedef struct {
    uint8_t  acked_msg_id;  // msg_id being acknowledged
    uint16_t acked_seq;     // seq number being acknowledged
    uint8_t  status;        // 0 = success, non-zero = error
} AckPayload;  // 4 bytes
```

### HANDSHAKE_REQ (0x03)
```c
typedef struct {
    uint16_t version;            // PROTOCOL_VERSION = 0x0100
    uint32_t supported_features; // FEAT_* bitmask
    uint32_t timestamp_ms;       // current ms since boot
} HandshakeReqPayload;  // 10 bytes
```

### HANDSHAKE_ACK (0x04)
```c
typedef struct {
    uint16_t version;             // Jetson protocol version
    uint32_t negotiated_features; // intersection of both sides' features
    uint8_t  role;                // 1 = Jetson
} HandshakeAckPayload;  // 7 bytes
```

### TASK_COMMAND (0x23)
```c
typedef struct {
    TaskId   task_id;   // see TaskId enum
    uint32_t arg;       // task-specific argument
    char     name[32];  // NUL-terminated task name
} TaskCommandPayload;  // 37 bytes
```

### TASK_RESPONSE (0x24)
```c
typedef struct {
    TaskId   task_id;      // mirrors TaskCommandPayload.task_id
    uint16_t acked_seq;    // seq from the original TASK_COMMAND frame
    uint8_t  result;       // 0 = fail, 1 = success
    char     message[63];  // NUL-terminated result message
} TaskResponsePayload;  // 67 bytes
```

## Feature Capability Bitmask

Used in handshake to advertise and negotiate capabilities:

| Bit | Constant     | Capability       |
|-----|--------------|------------------|
| 0   | `FEAT_GPIO`  | GPIO control     |
| 1   | `FEAT_ADC`   | ADC sensor data  |
| 2   | `FEAT_POWER` | Power management |
| 3   | `FEAT_TASK`  | Task execution   |
| 4   | `FEAT_INPUT` | Input events     |

`FEAT_ALL = 0x1F` enables all capabilities.

## Handshake Sequence

```
T113i                              Jetson
  │                                  │
  │──── HANDSHAKE_REQ (seq=1) ──────►│
  │     version=0x0100               │
  │     features=FEAT_ALL            │
  │                                  │
  │◄─── HANDSHAKE_ACK (seq=1) ──────│
  │     version=0x0100               │
  │     negotiated=FEAT_ALL & peer   │
  │     role=1                       │
  │                                  │
  │──── HEARTBEAT (seq=2) ──────────►│
  │◄─── HEARTBEAT (seq=1) ──────────│
  │         (steady-state)           │
```

## Protocol Version

Current version: `0x0100` (major=1, minor=0)

The high byte is the **major** version.  A mismatch in major version is
treated as incompatible and may result in disconnection.
