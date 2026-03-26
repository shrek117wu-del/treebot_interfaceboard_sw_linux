#pragma once
/**
 * @file utils.h
 * @brief Small utility functions shared across the T113i daemon.
 */

#include <cstdint>
#include <cstdio>
#include <string>

// Format an integer as a hex string with the given number of uppercase digits.
// E.g. to_hex(0x1A2B, 4) → "1A2B"
template<typename T>
static inline std::string to_hex(T value, int digits = sizeof(T) * 2) {
    char buf[32];
    // Build the format string dynamically
    char fmt[16];
    std::snprintf(fmt, sizeof(fmt), "%%0%dX", digits);
    std::snprintf(buf, sizeof(buf), fmt,
                  static_cast<unsigned long long>(value));
    return std::string(buf);
}
