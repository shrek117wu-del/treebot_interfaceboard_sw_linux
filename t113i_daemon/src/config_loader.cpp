/**
 * @file config_loader.cpp
 * @brief Minimal INI parser for daemon.conf.
 *
 * Supported grammar (subset of standard INI):
 *   [section]
 *   key=value
 *   # comment
 *
 * Only the sections and keys that daemon.conf actually uses are parsed.
 */

#include "config_loader.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return {};
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// Split 'line' on the first '=' and return {key, value}.
// Returns false if there is no '='.
static bool split_kv(const std::string& line,
                     std::string& key, std::string& value)
{
    auto pos = line.find('=');
    if (pos == std::string::npos) return false;
    key   = trim(line.substr(0, pos));
    value = trim(line.substr(pos + 1));
    return !key.empty();
}

// Split CSV fields into a vector of trimmed strings.
static std::vector<std::string> split_csv(const std::string& s) {
    std::vector<std::string> fields;
    std::istringstream ss(s);
    std::string field;
    while (std::getline(ss, field, ',')) {
        fields.push_back(trim(field));
    }
    return fields;
}

static bool parse_bool(const std::string& s) {
    std::string lo = s;
    std::transform(lo.begin(), lo.end(), lo.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    return lo == "true" || lo == "1" || lo == "yes";
}

// ---------------------------------------------------------------------------
// Section parsers
// ---------------------------------------------------------------------------

static bool parse_gpio_pin(const std::string& value, GpioPinConfig& out) {
    auto f = split_csv(value);
    if (f.size() < 6) {
        std::cerr << "[config] gpio pin: expected 6 fields, got "
                  << f.size() << " in '" << value << "'\n";
        return false;
    }
    out.bank       = std::stoi(f[0]);
    out.pin        = std::stoi(f[1]);
    out.linux_num  = std::stoi(f[2]);
    out.is_output  = parse_bool(f[3]);
    out.active_low = parse_bool(f[4]);
    out.label      = f[5];
    return true;
}

static bool parse_adc_channel(const std::string& value, AdcChannelConfig& out) {
    auto f = split_csv(value);
    if (f.size() < 5) {
        std::cerr << "[config] adc channel: expected 5 fields, got "
                  << f.size() << " in '" << value << "'\n";
        return false;
    }
    out.index      = std::stoi(f[0]);
    out.sysfs_path = f[1];
    out.scale      = std::stof(f[2]);
    out.offset     = std::stof(f[3]);
    out.unit       = f[4];
    return true;
}

static bool parse_power_rail(const std::string& value, PowerRailConfig& out) {
    auto f = split_csv(value);
    if (f.size() < 6) {
        std::cerr << "[config] power rail: expected 6 fields, got "
                  << f.size() << " in '" << value << "'\n";
        return false;
    }
    // Fields may be hex (0x..) or decimal
    out.on_cmd_id         = static_cast<uint8_t>(std::stoul(f[0], nullptr, 0));
    out.off_cmd_id        = static_cast<uint8_t>(std::stoul(f[1], nullptr, 0));
    out.gpio_num          = std::stoi(f[2]);
    out.active_low        = parse_bool(f[3]);
    out.min_on_ms         = static_cast<uint32_t>(std::stoul(f[4]));
    out.sequence_delay_ms = static_cast<uint32_t>(std::stoul(f[5]));
    return true;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool load_daemon_config(const std::string& path, DaemonConfig& out) {
    std::ifstream f(path);
    if (!f.is_open()) {
        std::cerr << "[config] Cannot open configuration file: " << path << "\n";
        return false;
    }

    std::string section;
    std::string line;
    int line_no = 0;
    bool ok = true;

    while (std::getline(f, line)) {
        ++line_no;
        std::string l = trim(line);
        if (l.empty() || l[0] == '#' || l[0] == ';') continue;

        if (l[0] == '[') {
            // Section header
            auto close = l.find(']');
            if (close == std::string::npos) {
                std::cerr << "[config] Line " << line_no
                          << ": malformed section header\n";
                ok = false;
                continue;
            }
            section = trim(l.substr(1, close - 1));
            continue;
        }

        std::string key, value;
        if (!split_kv(l, key, value)) {
            std::cerr << "[config] Line " << line_no << ": not a key=value pair\n";
            ok = false;
            continue;
        }

        if (section == "communication") {
            if      (key == "transport")    out.transport   = value;
            else if (key == "server_ip")    out.server_ip   = value;
            else if (key == "server_port")  out.server_port = std::stoi(value);
            else if (key == "uart_device")  out.uart_device = value;
            else if (key == "baud_rate")    out.baud_rate   = std::stoi(value);
        } else if (section == "gpio") {
            // Keys are pin_0, pin_1, … – any key in [gpio] is treated as a pin
            GpioPinConfig pin;
            if (!parse_gpio_pin(value, pin)) { ok = false; continue; }
            out.gpio_pins.push_back(pin);
        } else if (section == "adc") {
            AdcChannelConfig ch;
            if (!parse_adc_channel(value, ch)) { ok = false; continue; }
            out.adc_channels.push_back(ch);
        } else if (section == "power") {
            PowerRailConfig rail;
            if (!parse_power_rail(value, rail)) { ok = false; continue; }
            out.power_rails.push_back(rail);
        } else if (!section.empty()) {
            std::cerr << "[config] Line " << line_no
                      << ": unknown section '" << section
                      << "', key ignored: " << key << "\n";
        }
    }

    return ok;
}
