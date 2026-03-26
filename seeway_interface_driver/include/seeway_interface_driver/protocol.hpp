#pragma once
#include <stdint.h>
#include <stddef.h>

// ---------------------------------------------------------------------------
// Frame constants
// ---------------------------------------------------------------------------
static const uint8_t  PROTO_HEADER_0   = 0xAA;
static const uint8_t  PROTO_HEADER_1   = 0x55;
static const uint16_t PROTO_HEADER_SZ  = 2;
static const uint16_t PROTO_HEADER_TOT = 9;  // header+id+seq+len+reserved
static const uint16_t PROTO_CRC_SZ     = 2;
static const uint16_t PROTO_MAX_PAYLOAD = 256;
static const uint16_t PROTO_MAX_FRAME   = PROTO_HEADER_TOT + PROTO_MAX_PAYLOAD + PROTO_CRC_SZ;

// ---------------------------------------------------------------------------
// Message IDs
// ---------------------------------------------------------------------------
typedef enum : uint8_t {
    MSG_HEARTBEAT       = 0x01,
    MSG_ACK             = 0x02,
    MSG_SENSOR_DATA     = 0x10,
    MSG_GPIO_STATUS     = 0x11,
    MSG_BATTERY_STATUS  = 0x12,
    MSG_SYSTEM_STATUS   = 0x13,
    MSG_EVENT           = 0x30,
    MSG_DO_COMMAND      = 0x20,
    MSG_PWM_COMMAND     = 0x21,
    MSG_POWER_COMMAND   = 0x22,
    MSG_TASK_COMMAND    = 0x23,
    MSG_TASK_RESPONSE   = 0x24,
} MsgId;

// ---------------------------------------------------------------------------
// Payload structures (packed, little-endian)
// ---------------------------------------------------------------------------
#pragma pack(push, 1)

typedef struct {
    uint32_t timestamp_ms;
    uint8_t  role;
} HeartbeatPayload;

typedef struct {
    uint8_t  acked_msg_id;
    uint16_t acked_seq;
    uint8_t  status;
} AckPayload;

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
    TASK_ARM_HOME       = 0x01,
    TASK_ESTOP          = 0x02,
    TASK_SHUTDOWN_ALL   = 0x03,
    TASK_ENABLE_VALVE   = 0x10,
    TASK_DISABLE_VALVE  = 0x11,
    TASK_CUSTOM         = 0xF0,
} TaskId;

typedef struct {
    TaskId   task_id;
    uint32_t arg;
    char     name[32];
} TaskCommandPayload;

typedef struct {
    TaskId   task_id;
    uint16_t acked_seq; // Echoes the frame seq from the original TaskCommandPayload
    uint8_t  result;
    char     message[63]; // NUL-terminated; kept struct size unchanged (was 64+1+1, now 63+2+1+1)
} TaskResponsePayload;

#pragma pack(pop)

static inline uint16_t crc16_ccitt(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++)
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
    }
    return crc;
}
