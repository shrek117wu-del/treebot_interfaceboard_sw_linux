#pragma once
/**
 * @file protocol.h
 * @brief Binary frame protocol shared between T113i daemon and Jetson ROS2 driver.
 *
 * Frame layout:
 *   [AA][55][msg_id:1][seq:2][len:2][reserved:2][payload:len][crc16:2]
 *
 * All multi-byte integers are little-endian.
 * All payload structs are packed (no padding).
 */

#include <stdint.h>
#include <stddef.h>

// ---------------------------------------------------------------------------
// Frame constants
// ---------------------------------------------------------------------------
static const uint8_t  PROTO_HEADER_0    = 0xAA;
static const uint8_t  PROTO_HEADER_1    = 0x55;
static const uint16_t PROTO_HEADER_TOT  = 9;   // AA55 + id(1) + seq(2) + len(2) + reserved(2)
static const uint16_t PROTO_CRC_SZ      = 2;
static const uint16_t PROTO_MAX_PAYLOAD = 256;
static const uint16_t PROTO_MAX_FRAME   = PROTO_HEADER_TOT + PROTO_MAX_PAYLOAD + PROTO_CRC_SZ;

#define PROTOCOL_VERSION 0x0100  // v1.0

// ---------------------------------------------------------------------------
// Message IDs
// ---------------------------------------------------------------------------
typedef uint8_t MsgId;

static const MsgId MSG_HEARTBEAT       = 0x01;
static const MsgId MSG_ACK             = 0x02;
static const MsgId MSG_HANDSHAKE_REQ   = 0x03;  // Protocol version negotiation
static const MsgId MSG_HANDSHAKE_ACK   = 0x04;
static const MsgId MSG_SENSOR_DATA     = 0x10;
static const MsgId MSG_GPIO_STATUS     = 0x11;
static const MsgId MSG_BATTERY_STATUS  = 0x12;
static const MsgId MSG_SYSTEM_STATUS   = 0x13;
static const MsgId MSG_DO_COMMAND      = 0x20;
static const MsgId MSG_PWM_COMMAND     = 0x21;
static const MsgId MSG_POWER_COMMAND   = 0x22;
static const MsgId MSG_TASK_COMMAND    = 0x23;
static const MsgId MSG_TASK_RESPONSE   = 0x24;
static const MsgId MSG_EVENT           = 0x30;

// ---------------------------------------------------------------------------
// Feature capability bitmasks (used in handshake)
// ---------------------------------------------------------------------------
static const uint32_t FEAT_GPIO    = (1u << 0);
static const uint32_t FEAT_ADC     = (1u << 1);
static const uint32_t FEAT_POWER   = (1u << 2);
static const uint32_t FEAT_TASK    = (1u << 3);
static const uint32_t FEAT_INPUT   = (1u << 4);
static const uint32_t FEAT_ALL     = 0x1Fu;

// ---------------------------------------------------------------------------
// Payload structures (packed, little-endian)
// ---------------------------------------------------------------------------
#pragma pack(push, 1)

typedef struct {
    uint32_t timestamp_ms;
    uint8_t  role;   // 0 = T113i, 1 = Jetson
} HeartbeatPayload;

typedef struct {
    uint8_t  acked_msg_id;
    uint16_t acked_seq;
    uint8_t  status;  // 0 = success, non-zero = error code
} AckPayload;

// Handshake request – sent by T113i immediately after connection
typedef struct {
    uint16_t version;            // PROTOCOL_VERSION
    uint32_t supported_features; // FEAT_* bitmask
    uint32_t timestamp_ms;
} HandshakeReqPayload;

// Handshake acknowledgement – sent by Jetson in response
typedef struct {
    uint16_t version;             // Jetson's protocol version
    uint32_t negotiated_features; // intersection of both sides' features
    uint8_t  role;                // 1 = Jetson
} HandshakeAckPayload;

#define SENSOR_ADC_CHANNELS  8
typedef struct {
    uint32_t timestamp_ms;
    uint16_t adc_raw[SENSOR_ADC_CHANNELS];
    float    temperature_c;
    float    humidity_pct;
    uint8_t  channel_count;
} SensorDataPayload;

#define GPIO_BANK_COUNT  4
typedef struct {
    uint32_t timestamp_ms;
    uint32_t input_states[GPIO_BANK_COUNT];
    uint32_t output_states[GPIO_BANK_COUNT];
} GpioStatusPayload;

typedef struct {
    uint32_t timestamp_ms;
    float    voltage_v;
    float    current_a;
    float    soc_pct;
    float    temperature_c;
    uint8_t  status_flags;
    uint16_t cycle_count;
} BatteryStatusPayload;

typedef struct {
    uint32_t uptime_s;
    uint8_t  cpu_load_pct;
    float    cpu_temp_c;
    uint32_t free_mem_kb;
} SystemStatusPayload;

typedef enum : uint8_t {
    EVT_KEY_PRESS    = 0x01,
    EVT_KEY_RELEASE  = 0x02,
    EVT_TOUCH_DOWN   = 0x10,
    EVT_TOUCH_UP     = 0x11,
    EVT_GPIO_RISING  = 0x20,
    EVT_GPIO_FALLING = 0x21,
} EventType;

typedef struct {
    uint32_t  timestamp_ms;
    EventType type;
    uint16_t  code;
    int32_t   value;
} EventPayload;

typedef struct {
    uint8_t  bank;
    uint32_t pin_mask;
    uint32_t pin_states;
} DoCommandPayload;

typedef struct {
    uint8_t  channel;
    uint16_t frequency_hz;
    uint16_t duty_per_mil;
    uint8_t  enable;
} PwmCommandPayload;

typedef enum : uint8_t {
    PWR_NX_ON         = 0x01,
    PWR_NX_OFF        = 0x02,
    PWR_NX_RESET      = 0x03,
    PWR_ARM_ON        = 0x11,
    PWR_ARM_OFF       = 0x12,
    PWR_CHASSIS_ON    = 0x21,
    PWR_CHASSIS_OFF   = 0x22,
    PWR_ALL_OFF       = 0xFF,
} PowerCmd;

typedef struct {
    PowerCmd command;
    uint16_t delay_ms;
} PowerCommandPayload;

typedef enum : uint8_t {
    TASK_ARM_HOME      = 0x01,
    TASK_ESTOP         = 0x02,
    TASK_SHUTDOWN_ALL  = 0x03,
    TASK_ENABLE_VALVE  = 0x10,
    TASK_DISABLE_VALVE = 0x11,
    TASK_CUSTOM        = 0xF0,
} TaskId;

typedef struct {
    TaskId   task_id;
    uint32_t arg;
    char     name[32];
} TaskCommandPayload;

typedef struct {
    TaskId   task_id;
    uint16_t acked_seq;   // echoes the frame seq from the original TaskCommandPayload
    uint8_t  result;
    char     message[63]; // NUL-terminated result message
} TaskResponsePayload;

#pragma pack(pop)

// ---------------------------------------------------------------------------
// CRC-16/CCITT (polynomial 0x1021, init 0xFFFF)
// ---------------------------------------------------------------------------
static inline uint16_t crc16_ccitt(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++)
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
    }
    return crc;
}
