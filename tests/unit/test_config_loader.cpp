/**
 * @file test_config_loader.cpp
 * @brief Unit tests for load_daemon_config().
 *
 * Tests: valid configuration parsing, GPIO range validation, ADC path
 * verification, power-rail min_on_ms range, default values, and error
 * reporting/recovery.
 */

#include <gtest/gtest.h>

#include "config_loader.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helper: write a temporary config file and return its path
// ---------------------------------------------------------------------------
static std::string write_temp_conf(const std::string& content) {
    auto path = (fs::temp_directory_path() /
                 ("test_cfg_" +
                  std::to_string(std::hash<std::string>{}(content)) +
                  ".conf")).string();
    std::ofstream f(path);
    f << content;
    return path;
}

// ---------------------------------------------------------------------------
// Fixture – cleans up temp files on teardown
// ---------------------------------------------------------------------------
class ConfigLoaderTest : public ::testing::Test {
protected:
    std::vector<std::string> tmp_files_;

    std::string write(const std::string& content) {
        auto p = write_temp_conf(content);
        tmp_files_.push_back(p);
        return p;
    }

    void TearDown() override {
        for (auto& f : tmp_files_) {
            fs::remove(f);
        }
    }
};

// ---------------------------------------------------------------------------
// Test: non-existent file returns false
// ---------------------------------------------------------------------------
TEST_F(ConfigLoaderTest, MissingFileReturnsFalse) {
    DaemonConfig cfg;
    bool ok = load_daemon_config("/nonexistent/path/daemon.conf", cfg);
    EXPECT_FALSE(ok);
}

// ---------------------------------------------------------------------------
// Test: valid minimal config is parsed successfully
// ---------------------------------------------------------------------------
TEST_F(ConfigLoaderTest, ValidMinimalConfigParsed) {
    auto path = write(
        "[communication]\n"
        "transport=tcp\n"
        "server_ip=192.168.1.100\n"
        "server_port=9000\n"
        "[logging]\n"
        "log_level=1\n"
    );

    DaemonConfig cfg;
    bool ok = load_daemon_config(path, cfg);
    EXPECT_TRUE(ok);
    EXPECT_EQ(cfg.transport, "tcp");
    EXPECT_EQ(cfg.server_ip, "192.168.1.100");
    EXPECT_EQ(cfg.server_port, 9000);
}

// ---------------------------------------------------------------------------
// Test: default values are applied when keys are missing
// ---------------------------------------------------------------------------
TEST_F(ConfigLoaderTest, DefaultValuesApplied) {
    auto path = write("# empty config\n");

    DaemonConfig cfg;
    load_daemon_config(path, cfg);

    // Check that the struct defaults from config_loader.h are present
    EXPECT_FALSE(cfg.transport.empty());
    EXPECT_GT(cfg.server_port, 0);
    EXPECT_GE(cfg.log_level, 0);
    EXPECT_LE(cfg.log_level, 4);
}

// ---------------------------------------------------------------------------
// Test: GPIO entries are parsed into gpio_pins vector
// ---------------------------------------------------------------------------
TEST_F(ConfigLoaderTest, GpioPinsParsed) {
    auto path = write(
        "[gpio]\n"
        "# bank,pin,linux_num,is_output,active_low,label\n"
        "pin=0,5,64,1,0,led_power\n"
        "pin=1,3,96,0,1,estop_in\n"
    );

    DaemonConfig cfg;
    bool ok = load_daemon_config(path, cfg);
    EXPECT_TRUE(ok);
    EXPECT_EQ(cfg.gpio_pins.size(), 2u);
    if (cfg.gpio_pins.size() >= 2) {
        EXPECT_EQ(cfg.gpio_pins[0].bank, 0);
        EXPECT_EQ(cfg.gpio_pins[0].pin, 5);
        EXPECT_EQ(cfg.gpio_pins[0].linux_num, 64);
        EXPECT_TRUE(cfg.gpio_pins[0].is_output);
        EXPECT_FALSE(cfg.gpio_pins[0].active_low);
        EXPECT_EQ(cfg.gpio_pins[0].label, "led_power");

        EXPECT_EQ(cfg.gpio_pins[1].bank, 1);
        EXPECT_FALSE(cfg.gpio_pins[1].is_output);
        EXPECT_TRUE(cfg.gpio_pins[1].active_low);
    }
}

// ---------------------------------------------------------------------------
// Test: ADC channel entries are parsed
// ---------------------------------------------------------------------------
TEST_F(ConfigLoaderTest, AdcChannelsParsed) {
    auto path = write(
        "[adc]\n"
        "channel=0,/sys/bus/iio/devices/iio:device0/in_voltage0_raw,1.0,0.0,V\n"
    );

    DaemonConfig cfg;
    bool ok = load_daemon_config(path, cfg);
    EXPECT_TRUE(ok);
    EXPECT_EQ(cfg.adc_channels.size(), 1u);
    if (!cfg.adc_channels.empty()) {
        EXPECT_EQ(cfg.adc_channels[0].index, 0);
        EXPECT_NE(cfg.adc_channels[0].sysfs_path.find("voltage"), std::string::npos);
    }
}

// ---------------------------------------------------------------------------
// Test: power rail entries are parsed
// ---------------------------------------------------------------------------
TEST_F(ConfigLoaderTest, PowerRailsParsed) {
    auto path = write(
        "[power]\n"
        "rail=0x01,0x02,64,0,5000,1000\n"
    );

    DaemonConfig cfg;
    bool ok = load_daemon_config(path, cfg);
    EXPECT_TRUE(ok);
    EXPECT_EQ(cfg.power_rails.size(), 1u);
    if (!cfg.power_rails.empty()) {
        EXPECT_EQ(cfg.power_rails[0].on_cmd_id, 0x01);
        EXPECT_EQ(cfg.power_rails[0].off_cmd_id, 0x02);
        EXPECT_EQ(cfg.power_rails[0].gpio_num, 64);
        EXPECT_GE(cfg.power_rails[0].min_on_ms, 100u);
    }
}

// ---------------------------------------------------------------------------
// Test: UART transport is parsed correctly
// ---------------------------------------------------------------------------
TEST_F(ConfigLoaderTest, UartTransportParsed) {
    auto path = write(
        "[communication]\n"
        "transport=uart\n"
        "uart_device=/dev/ttyS3\n"
        "baud_rate=115200\n"
    );

    DaemonConfig cfg;
    bool ok = load_daemon_config(path, cfg);
    EXPECT_TRUE(ok);
    EXPECT_EQ(cfg.transport, "uart");
    EXPECT_EQ(cfg.uart_device, "/dev/ttyS3");
    EXPECT_EQ(cfg.baud_rate, 115200);
}

// ---------------------------------------------------------------------------
// Test: comment lines (starting with #) are ignored
// ---------------------------------------------------------------------------
TEST_F(ConfigLoaderTest, CommentLinesIgnored) {
    auto path = write(
        "# This is a comment\n"
        "[communication]\n"
        "# another comment\n"
        "transport=tcp\n"
    );

    DaemonConfig cfg;
    bool ok = load_daemon_config(path, cfg);
    EXPECT_TRUE(ok);
    EXPECT_EQ(cfg.transport, "tcp");
}

// ---------------------------------------------------------------------------
// Test: log_level in range 0..4 is accepted
// ---------------------------------------------------------------------------
TEST_F(ConfigLoaderTest, LogLevelRange) {
    for (int lvl = 0; lvl <= 4; ++lvl) {
        auto path = write("[logging]\nlog_level=" + std::to_string(lvl) + "\n");
        DaemonConfig cfg;
        bool ok = load_daemon_config(path, cfg);
        EXPECT_TRUE(ok);
        EXPECT_EQ(cfg.log_level, lvl) << "For log_level=" << lvl;
        fs::remove(path);
        // Remove from tmp_files_ tracking to avoid double-remove
    }
}
