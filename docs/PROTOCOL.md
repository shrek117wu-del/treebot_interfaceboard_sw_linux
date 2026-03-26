# Binary Protocol Specification

This document defines the binary frame format used between the T113i daemon
and the Jetson Orin NX ROS2 driver.

---

## Frame Format

```
Offset  Size  Field       Description
──────  ────  ─────────   ───────────────────────────────────────────────
0       1     header[0]   0xAA (magic byte 0)
1       1     header[1]   0x55 (magic byte 1)
2       1     msg_id      Message type identifier (see below)
3       2     seq         Sequence number (little-endian, wraps at 65535)
5       2     length      Payload length in bytes (little-endian, 0–256)
7       2     reserved    Must be 0x0000
9       N     payload     Message-specific payload (0–256 bytes)
9+N     2     crc16       CRC-16/CCITT over bytes [0..9+N-1] (little-endian)
```

**Total frame size**: 9 (header) + N (payload) + 2 (CRC) = 11–267 bytes

All multi-byte fields are **little-endian**.

---

## CRC Algorithm

**CRC-16/CCITT**
- Polynomial: `0x1021`
- Initial value: `0xFFFF`
- Input/output NOT reflected (MSB-first)

Reference implementation (C):
```c
uint16_t crc16_ccitt(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++)
            crc = (crc & 0x8000u) ?
                  (uint16_t)((crc << 1) ^ 0x1021u) :
                  (uint16_t)(crc << 1);
    }
    return crc;
}
```

Known test vector: `"123456789"` → `0x29B1`

---

## Protocol Version

```
#define PROTOCOL_VERSION 0x0100   // v1.0
```

Carried in `HandshakeReqPayload::version` and `HandshakeAckPayload::version`.
Mismatch is logged as a warning; communication continues.

---

## Message Types

| ID   | Name              | Direction      | Description |
|------|-------------------|----------------|-------------|
| 0x01 | HEARTBEAT         | Both           | Keepalive |
| 0x02 | ACK               | Both           | Generic acknowledgement |
| 0x03 | HANDSHAKE_REQ     | T113i → Jetson | Protocol version + features |
| 0x04 | HANDSHAKE_ACK     | Jetson → T113i | Negotiated version + features |
| 0x10 | SENSOR_DATA       | T113i → Jetson | ADC + temperature readings |
| 0x11 | GPIO_STATUS       | T113i → Jetson | Digital I/O snapshot |
| 0x12 | BATTERY_STATUS    | T113i → Jetson | Battery voltage, SoC |
| 0x13 | SYSTEM_STATUS     | T113i → Jetson | CPU, temperature, memory |
| 0x20 | DO_COMMAND        | Jetson → T113i | Digital output control |
| 0x21 | PWM_COMMAND       | Jetson → T113i | PWM channel control |
| 0x22 | POWER_COMMAND     | Jetson → T113i | Power rail on/off |
| 0x23 | TASK_COMMAND      | Jetson → T113i | Enqueue long-running task |
| 0x24 | TASK_RESPONSE     | T113i → Jetson | Task completion result |
| 0x30 | EVENT             | T113i → Jetson | Input event (key, GPIO edge) |

---

## Payload Definitions

### HEARTBEAT (0x01) – 5 bytes
```c
struct HeartbeatPayload {
    uint32_t timestamp_ms;  // system uptime or wall clock ms
    uint8_t  role;          // 0=T113i, 1=Jetson
};
```

### ACK (0x02) – 4 bytes
```c
struct AckPayload {
    uint8_t  acked_msg_id;  // message type being acknowledged
    uint16_t acked_seq;     // sequence number being acknowledged
    uint8_t  status;        // 0=success, 1=error, 2=queue_full
};
```

### HANDSHAKE_REQ (0x03) – 10 bytes
```c
struct HandshakeReqPayload {
    uint16_t version;             // PROTOCOL_VERSION
    uint32_t supported_features;  // FEAT_* bitmask (see below)
    uint32_t timestamp_ms;
};
```

### HANDSHAKE_ACK (0x04) – 7 bytes
```c
struct HandshakeAckPayload {
    uint16_t version;              // peer's PROTOCOL_VERSION
    uint32_t negotiated_features;  // intersection of both sides
    uint8_t  role;                 // 1=Jetson
};
```

### SENSOR_DATA (0x10)
```c
struct SensorDataPayload {
    uint32_t timestamp_ms;
    uint16_t adc_raw[8];       // raw ADC counts per channel
    float    temperature_c;
    float    humidity_pct;
    uint8_t  channel_count;
};
```

### TASK_COMMAND (0x23) – 37 bytes
```c
struct TaskCommandPayload {
    uint8_t  task_id;   // TaskId enum (see Task IDs below)
    uint32_t arg;       // task-specific argument
    char     name[32];  // NUL-terminated descriptive name
};
```

### TASK_RESPONSE (0x24) – 67 bytes
```c
struct TaskResponsePayload {
    uint8_t  task_id;
    uint16_t acked_seq;   // seq from the original TASK_COMMAND
    uint8_t  result;      // 0=success, non-zero=error
    char     message[63]; // NUL-terminated result message
};
```

---

## Feature Capability Bitmask

Used in handshake to negotiate which capabilities are active:

| Bit | Constant   | Meaning |
|-----|-----------|---------|
| 0   | FEAT_GPIO  | Digital I/O control |
| 1   | FEAT_ADC   | ADC sensor readings |
| 2   | FEAT_POWER | Power rail switching |
| 3   | FEAT_TASK  | Long-running task execution |
| 4   | FEAT_INPUT | Button / touch events |

`FEAT_ALL = 0x1F` (all features)

---

## Task IDs

| ID   | Name            | Description |
|------|-----------------|-------------|
| 0x01 | TASK_ARM_HOME   | Send arm to home position |
| 0x02 | TASK_ESTOP      | Emergency stop all outputs |
| 0x03 | TASK_SHUTDOWN_ALL | Shutdown all power rails |
| 0x10 | TASK_ENABLE_VALVE | Open specified valve |
| 0x11 | TASK_DISABLE_VALVE | Close specified valve |
| 0xF0 | TASK_CUSTOM     | Application-defined task |

---

## Error Codes (ACK.status)

| Value | Meaning |
|-------|---------|
| 0     | Success |
| 1     | Generic error |
| 2     | Queue full (task rejected) |
| 3     | Invalid payload length |

---

## Example Frame: Heartbeat

Request from T113i (timestamp=1000, role=0):

```
AA 55 01 00 01 05 00 00 00 E8 03 00 00 00 XX XX
├─┘ ├─┘ ├─┘ ├──────┘ ├──────┘ ├──────┘ ├─┘  ├─── payload (timestamp=0x3E8=1000, role=0)
│   │   │   seq=256  len=5   reserved   CRC16
│   │   msg_id=HEARTBEAT
│   header[1]=0x55
header[0]=0xAA
```
