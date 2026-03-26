/**
 * @file test_utils.cpp
 * @brief Unit tests for utility functions in utils.h.
 *
 * Tests: to_hex formatting (zero padding, uppercase, various types),
 * edge cases (zero value, max uint8, max uint16, max uint32).
 */

#include "utils.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

// ---------------------------------------------------------------------------
// to_hex – basic behaviour
// ---------------------------------------------------------------------------
TEST(ToHexTest, ZeroIsAllZeros) {
    EXPECT_EQ(to_hex(uint8_t(0), 2), "00");
    EXPECT_EQ(to_hex(uint16_t(0), 4), "0000");
    EXPECT_EQ(to_hex(uint32_t(0), 8), "00000000");
}

TEST(ToHexTest, MaxUint8) {
    EXPECT_EQ(to_hex(uint8_t(0xFF), 2), "FF");
}

TEST(ToHexTest, MaxUint16) {
    EXPECT_EQ(to_hex(uint16_t(0xFFFF), 4), "FFFF");
}

TEST(ToHexTest, MaxUint32) {
    EXPECT_EQ(to_hex(uint32_t(0xFFFFFFFFu), 8), "FFFFFFFF");
}

TEST(ToHexTest, KnownValue_0x1A2B) {
    EXPECT_EQ(to_hex(uint16_t(0x1A2B), 4), "1A2B");
}

TEST(ToHexTest, UppercaseDigits) {
    // All hex digits A-F should be uppercase
    EXPECT_EQ(to_hex(uint32_t(0xABCDEF01u), 8), "ABCDEF01");
}

TEST(ToHexTest, ZeroPaddingRespected_1digit) {
    EXPECT_EQ(to_hex(uint8_t(0x0A), 2), "0A");
}

TEST(ToHexTest, ZeroPaddingRespected_leadingZeros) {
    EXPECT_EQ(to_hex(uint16_t(0x000F), 4), "000F");
}

// ---------------------------------------------------------------------------
// to_hex – custom digit counts
// ---------------------------------------------------------------------------
TEST(ToHexTest, TwoDigitFormatUint16) {
    EXPECT_EQ(to_hex(uint16_t(0x001F), 2), "1F");
}

TEST(ToHexTest, SixDigitFormat) {
    EXPECT_EQ(to_hex(uint32_t(0x00ABCD), 6), "00ABCD");
}

// ---------------------------------------------------------------------------
// to_hex – default digit count (sizeof(T)*2)
// ---------------------------------------------------------------------------
TEST(ToHexTest, DefaultDigitsUint8) {
    // sizeof(uint8_t) * 2 = 2 digits
    EXPECT_EQ(to_hex(uint8_t(0xAB)), "AB");
}

TEST(ToHexTest, DefaultDigitsUint16) {
    // sizeof(uint16_t) * 2 = 4 digits
    EXPECT_EQ(to_hex(uint16_t(0x0100)), "0100");
}

TEST(ToHexTest, DefaultDigitsUint32) {
    // sizeof(uint32_t) * 2 = 8 digits
    EXPECT_EQ(to_hex(uint32_t(0x00001234u)), "00001234");
}

// ---------------------------------------------------------------------------
// to_hex – integral types
// ---------------------------------------------------------------------------
TEST(ToHexTest, IntValue) {
    EXPECT_EQ(to_hex(int(255), 2), "FF");
}

TEST(ToHexTest, LongValue) {
    EXPECT_EQ(to_hex(long(0xABCD), 4), "ABCD");
}

// ---------------------------------------------------------------------------
// to_hex – returned string length
// ---------------------------------------------------------------------------
TEST(ToHexTest, ReturnedStringLength) {
    EXPECT_EQ(to_hex(uint8_t(5), 4).length(), 4u);
    EXPECT_EQ(to_hex(uint32_t(5), 8).length(), 8u);
}

// ---------------------------------------------------------------------------
// to_hex – specific protocol values used in logger tags
// ---------------------------------------------------------------------------
TEST(ToHexTest, ProtocolVersion) {
    // PROTOCOL_VERSION = 0x0100 → "0100" with 4 digits
    EXPECT_EQ(to_hex(static_cast<uint16_t>(0x0100), 4), "0100");
}
