/**
 * @file test_config_loader.cpp
 * @brief Unit tests for load_daemon_config.
 *
 * Tests: valid config parsing, default values, range validation,
 * error handling for missing/malformed files, and all section types.
 */

#include "config_loader.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helper: write a temporary config file
// ---------------------------------------------------------------------------
static std::string write_temp_conf(const std::string& content) {
    std::string path = (fs::temp_directory_path() /
                        ("test_cfg_" + std::to_string(getpid()) + ".conf"))
                           .string();
    std::ofstream f(path);
    f << content;
    return path;
}

class ConfigLoaderTest : public ::testing::Test {
protected:
    std::string tmp_;

    void TearDown() override {
        if (!tmp_.empty()) {
            std::error_code ec;
            fs::remove(tmp_, ec);
        }
    }
};

// ---------------------------------------------------------------------------
// Missing file returns false
// ---------------------------------------------------------------------------
TEST_F(ConfigLoaderTest, MissingFileReturnsFalse) {
    DaemonConfig cfg;
    EXPECT_FALSE(load_daemon_config("/nonexistent/path/daemon.conf", cfg));
}

// ---------------------------------------------------------------------------
// Empty file returns true (all defaults)
// ---------------------------------------------------------------------------
TEST_F(ConfigLoaderTest, EmptyFileReturnsTrue) {
    tmp_ = write_temp_conf("");
    DaemonConfig cfg;
    EXPECT_TRUE(load_daemon_config(tmp_, cfg));
    EXPECT_EQ(cfg.transport, "tcp");
    EXPECT_EQ(cfg.server_ip, "127.0.0.1");
    EXPECT_EQ(cfg.server_port, 9000);
    EXPECT_EQ(cfg.log_level, 1);
}

// ---------------------------------------------------------------------------
// [communication] section
// ---------------------------------------------------------------------------
TEST_F(ConfigLoaderTest, CommunicationSection) {
    tmp_ = write_temp_conf(
        "[communication]\n"
        "transport=uart\n"
        "server_ip=192.168.1.100\n"
        "server_port=8888\n"
        "uart_device=/dev/ttyS1\n"
        "baud_rate=9600\n"
        "reconnect_ms=3000\n"
        "heartbeat_timeout_ms=10000\n");
    DaemonConfig cfg;
    EXPECT_TRUE(load_daemon_config(tmp_, cfg));
    EXPECT_EQ(cfg.transport, "uart");
    EXPECT_EQ(cfg.server_ip, "192.168.1.100");
    EXPECT_EQ(cfg.server_port, 8888);
    EXPECT_EQ(cfg.uart_device, "/dev/ttyS1");
    EXPECT_EQ(cfg.baud_rate, 9600);
    EXPECT_EQ(cfg.reconnect_ms, 3000);
    EXPECT_EQ(cfg.heartbeat_timeout_ms, 10000);
}

// ---------------------------------------------------------------------------
// [gpio] section
// ---------------------------------------------------------------------------
TEST_F(ConfigLoaderTest, GpioSectionParsed) {
    tmp_ = write_temp_conf(
        "[gpio]\n"
        "pin_0=0,0,10,true,false,valve_1\n"
        "pin_1=2,3,64,false,true,estop\n");
    DaemonConfig cfg;
    EXPECT_TRUE(load_daemon_config(tmp_, cfg));
    ASSERT_EQ(cfg.gpio_pins.size(), 2u);

    EXPECT_EQ(cfg.gpio_pins[0].bank, 0);
    EXPECT_EQ(cfg.gpio_pins[0].pin, 0);
    EXPECT_EQ(cfg.gpio_pins[0].linux_num, 10);
    EXPECT_TRUE(cfg.gpio_pins[0].is_output);
    EXPECT_FALSE(cfg.gpio_pins[0].active_low);
    EXPECT_EQ(cfg.gpio_pins[0].label, "valve_1");

    EXPECT_EQ(cfg.gpio_pins[1].bank, 2);
    EXPECT_FALSE(cfg.gpio_pins[1].is_output);
    EXPECT_TRUE(cfg.gpio_pins[1].active_low);
    EXPECT_EQ(cfg.gpio_pins[1].label, "estop");
}

// ---------------------------------------------------------------------------
// [adc] section
// ---------------------------------------------------------------------------
TEST_F(ConfigLoaderTest, AdcSectionParsed) {
    tmp_ = write_temp_conf(
        "[adc]\n"
        "ch_0=0,/sys/bus/iio/devices/iio:device0/in_voltage0_raw,0.001,0.0,V\n");
    DaemonConfig cfg;
    EXPECT_TRUE(load_daemon_config(tmp_, cfg));
    ASSERT_EQ(cfg.adc_channels.size(), 1u);
    EXPECT_EQ(cfg.adc_channels[0].index, 0);
    EXPECT_EQ(cfg.adc_channels[0].sysfs_path,
              "/sys/bus/iio/devices/iio:device0/in_voltage0_raw");
    EXPECT_FLOAT_EQ(cfg.adc_channels[0].scale, 0.001f);
    EXPECT_FLOAT_EQ(cfg.adc_channels[0].offset, 0.0f);
    EXPECT_EQ(cfg.adc_channels[0].unit, "V");
}

// ---------------------------------------------------------------------------
// [power] section
// ---------------------------------------------------------------------------
TEST_F(ConfigLoaderTest, PowerSectionParsed) {
    tmp_ = write_temp_conf(
        "[power]\n"
        "nx_rail=0x01,0x02,20,true,5000,1000\n");
    DaemonConfig cfg;
    EXPECT_TRUE(load_daemon_config(tmp_, cfg));
    ASSERT_EQ(cfg.power_rails.size(), 1u);
    EXPECT_EQ(cfg.power_rails[0].on_cmd_id, 0x01u);
    EXPECT_EQ(cfg.power_rails[0].off_cmd_id, 0x02u);
    EXPECT_EQ(cfg.power_rails[0].gpio_num, 20);
    EXPECT_TRUE(cfg.power_rails[0].active_low);
    EXPECT_EQ(cfg.power_rails[0].min_on_ms, 5000u);
    EXPECT_EQ(cfg.power_rails[0].sequence_delay_ms, 1000u);
}

// ---------------------------------------------------------------------------
// [logging] section
// ---------------------------------------------------------------------------
TEST_F(ConfigLoaderTest, LoggingSectionParsed) {
    tmp_ = write_temp_conf(
        "[logging]\n"
        "log_file=/var/log/seeway.log\n"
        "log_level=2\n");
    DaemonConfig cfg;
    EXPECT_TRUE(load_daemon_config(tmp_, cfg));
    EXPECT_EQ(cfg.log_file, "/var/log/seeway.log");
    EXPECT_EQ(cfg.log_level, 2);
}

// ---------------------------------------------------------------------------
// Comments and whitespace
// ---------------------------------------------------------------------------
TEST_F(ConfigLoaderTest, CommentsIgnored) {
    tmp_ = write_temp_conf(
        "# top-level comment\n"
        "[communication]\n"
        "# inline comment\n"
        "; semicolon comment\n"
        "transport=tcp\n");
    DaemonConfig cfg;
    EXPECT_TRUE(load_daemon_config(tmp_, cfg));
    EXPECT_EQ(cfg.transport, "tcp");
}

TEST_F(ConfigLoaderTest, LeadingTrailingWhitespaceStripped) {
    tmp_ = write_temp_conf(
        "[communication]\n"
        "  transport  =  tcp  \n");
    DaemonConfig cfg;
    EXPECT_TRUE(load_daemon_config(tmp_, cfg));
    EXPECT_EQ(cfg.transport, "tcp");
}

// ---------------------------------------------------------------------------
// Range validation – GPIO linux_num
// ---------------------------------------------------------------------------
TEST_F(ConfigLoaderTest, GpioLinuxNumOutOfRangeReturnsFalse) {
    tmp_ = write_temp_conf(
        "[gpio]\n"
        "pin_0=0,0,9999,true,false,bad\n"); // linux_num > 511
    DaemonConfig cfg;
    EXPECT_FALSE(load_daemon_config(tmp_, cfg));
}

// ---------------------------------------------------------------------------
// Range validation – GPIO bank
// ---------------------------------------------------------------------------
TEST_F(ConfigLoaderTest, GpioBankOutOfRangeReturnsFalse) {
    tmp_ = write_temp_conf(
        "[gpio]\n"
        "pin_0=99,0,10,true,false,bad\n"); // bank >= GPIO_BANK_COUNT_CFG
    DaemonConfig cfg;
    EXPECT_FALSE(load_daemon_config(tmp_, cfg));
}

// ---------------------------------------------------------------------------
// Range validation – ADC index
// ---------------------------------------------------------------------------
TEST_F(ConfigLoaderTest, AdcIndexOutOfRangeReturnsFalse) {
    tmp_ = write_temp_conf(
        "[adc]\n"
        "ch_0=99,/some/path,1.0,0.0,V\n"); // index > 7
    DaemonConfig cfg;
    EXPECT_FALSE(load_daemon_config(tmp_, cfg));
}

// ---------------------------------------------------------------------------
// Range validation – power min_on_ms
// ---------------------------------------------------------------------------
TEST_F(ConfigLoaderTest, PowerMinOnMsTooLargeReturnsFalse) {
    tmp_ = write_temp_conf(
        "[power]\n"
        "rail=0x01,0x02,10,false,700000,1000\n"); // > 600000
    DaemonConfig cfg;
    EXPECT_FALSE(load_daemon_config(tmp_, cfg));
}

// ---------------------------------------------------------------------------
// Too few fields in GPIO pin → false
// ---------------------------------------------------------------------------
TEST_F(ConfigLoaderTest, GpioTooFewFieldsReturnsFalse) {
    tmp_ = write_temp_conf(
        "[gpio]\n"
        "pin_0=0,0,10\n"); // only 3 fields
    DaemonConfig cfg;
    EXPECT_FALSE(load_daemon_config(tmp_, cfg));
}

// ---------------------------------------------------------------------------
// Non-integer in communication numeric field
// ---------------------------------------------------------------------------
TEST_F(ConfigLoaderTest, NonIntegerPortReturnsFalse) {
    tmp_ = write_temp_conf(
        "[communication]\n"
        "server_port=not_a_number\n");
    DaemonConfig cfg;
    EXPECT_FALSE(load_daemon_config(tmp_, cfg));
}

// ---------------------------------------------------------------------------
// Unknown section is warned but doesn't crash
// ---------------------------------------------------------------------------
TEST_F(ConfigLoaderTest, UnknownSectionIsToleratedWithWarning) {
    tmp_ = write_temp_conf(
        "[unknown_section]\n"
        "key=value\n");
    DaemonConfig cfg;
    // load_daemon_config warns but still succeeds on subsequent valid content
    // (the return value depends on whether any errors occurred)
    // The unknown section key just gets a warning; no hard failure
    EXPECT_TRUE(load_daemon_config(tmp_, cfg));
}

// ---------------------------------------------------------------------------
// Multiple pins, ADC channels, and power rails
// ---------------------------------------------------------------------------
TEST_F(ConfigLoaderTest, MultipleEntriesPerSection) {
    tmp_ = write_temp_conf(
        "[gpio]\n"
        "pin_0=0,0,10,true,false,v1\n"
        "pin_1=0,1,11,true,false,v2\n"
        "pin_2=2,0,64,false,false,btn\n"
        "[adc]\n"
        "ch_0=0,/sys/path0,0.001,0.0,V\n"
        "ch_1=1,/sys/path1,0.001,0.0,V\n"
        "[power]\n"
        "r0=0x01,0x02,20,true,5000,1000\n"
        "r1=0x11,0x12,21,false,3000,500\n");
    DaemonConfig cfg;
    EXPECT_TRUE(load_daemon_config(tmp_, cfg));
    EXPECT_EQ(cfg.gpio_pins.size(), 3u);
    EXPECT_EQ(cfg.adc_channels.size(), 2u);
    EXPECT_EQ(cfg.power_rails.size(), 2u);
}
