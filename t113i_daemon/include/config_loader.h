#pragma once
/**
 * @file config_loader.h
 * @brief Simple INI-style configuration loader for the T113i daemon.
 *
 * Supports the format used by daemon.conf:
 *   [section]
 *   key=value   (leading/trailing whitespace stripped)
 *   # comment lines are ignored
 */

#include <cstdint>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// GPIO pin descriptor parsed from [gpio] section
// Format: bank,pin,linux_num,is_output,active_low,label
// ---------------------------------------------------------------------------
struct GpioPinConfig {
    int         bank{0};
    int         pin{0};
    int         linux_num{0};
    bool        is_output{true};
    bool        active_low{false};
    std::string label;
};

// ---------------------------------------------------------------------------
// ADC channel descriptor parsed from [adc] section
// Format: index,sysfs_path,scale,offset,unit
// ---------------------------------------------------------------------------
struct AdcChannelConfig {
    int         index{0};
    std::string sysfs_path;
    float       scale{1.0f};
    float       offset{0.0f};
    std::string unit;
};

// ---------------------------------------------------------------------------
// Power rail descriptor parsed from [power] section
// Format: on_cmd_id(hex),off_cmd_id(hex),gpio_num,active_low,min_on_ms,sequence_delay_ms
// ---------------------------------------------------------------------------
struct PowerRailConfig {
    uint8_t  on_cmd_id{0};
    uint8_t  off_cmd_id{0};
    int      gpio_num{0};
    bool     active_low{false};
    uint32_t min_on_ms{5000};
    uint32_t sequence_delay_ms{1000};
};

// ---------------------------------------------------------------------------
// Top-level configuration
// ---------------------------------------------------------------------------
struct DaemonConfig {
    // [communication]
    std::string transport{"tcp"};   // "tcp" or "uart"
    std::string server_ip{"127.0.0.1"};
    int         server_port{9000};
    std::string uart_device{"/dev/ttyS3"};
    int         baud_rate{115200};

    // [gpio]
    std::vector<GpioPinConfig> gpio_pins;

    // [adc]
    std::vector<AdcChannelConfig> adc_channels;

    // [power]
    std::vector<PowerRailConfig> power_rails;
};

// ---------------------------------------------------------------------------
// Load configuration from file path.
// Returns true on success.  On failure prints an error to stderr and
// returns false – callers should treat this as a fatal error and exit(1).
// ---------------------------------------------------------------------------
bool load_daemon_config(const std::string& path, DaemonConfig& out_config);
