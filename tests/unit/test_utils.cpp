/**
 * @file test_utils.cpp
 * @brief Unit tests for utility functions in utils.h.
 *
 * Tests: to_hex() template with various types and digit widths.
 */

#include <gtest/gtest.h>

#include "utils.h"

#include <cstdint>
#include <string>

// ---------------------------------------------------------------------------
// Test: to_hex with uint8 and 2 digits
// ---------------------------------------------------------------------------
TEST(UtilsTest, ToHexUint8TwoDigits) {
    EXPECT_EQ(to_hex<uint8_t>(0x1A, 2), "1A");
    EXPECT_EQ(to_hex<uint8_t>(0x00, 2), "00");
    EXPECT_EQ(to_hex<uint8_t>(0xFF, 2), "FF");
}

// ---------------------------------------------------------------------------
// Test: to_hex with uint16 and 4 digits
// ---------------------------------------------------------------------------
TEST(UtilsTest, ToHexUint16FourDigits) {
    EXPECT_EQ(to_hex<uint16_t>(0xABCD, 4), "ABCD");
    EXPECT_EQ(to_hex<uint16_t>(0x0001, 4), "0001");
    EXPECT_EQ(to_hex<uint16_t>(0x0000, 4), "0000");
    EXPECT_EQ(to_hex<uint16_t>(0xFFFF, 4), "FFFF");
}

// ---------------------------------------------------------------------------
// Test: to_hex with uint32 and 8 digits
// ---------------------------------------------------------------------------
TEST(UtilsTest, ToHexUint32EightDigits) {
    EXPECT_EQ(to_hex<uint32_t>(0xDEADBEEF, 8), "DEADBEEF");
    EXPECT_EQ(to_hex<uint32_t>(0x00000001, 8), "00000001");
    EXPECT_EQ(to_hex<uint32_t>(0x00000000, 8), "00000000");
}

// ---------------------------------------------------------------------------
// Test: to_hex default digit count matches type size
// ---------------------------------------------------------------------------
TEST(UtilsTest, ToHexDefaultDigitCount) {
    // Default for uint8_t is 2 digits
    std::string r8 = to_hex<uint8_t>(0xFF);
    EXPECT_EQ(r8.size(), 2u);

    // Default for uint16_t is 4 digits
    std::string r16 = to_hex<uint16_t>(0xFFFF);
    EXPECT_EQ(r16.size(), 4u);

    // Default for uint32_t is 8 digits
    std::string r32 = to_hex<uint32_t>(0xFFFFFFFF);
    EXPECT_EQ(r32.size(), 8u);
}

// ---------------------------------------------------------------------------
// Test: to_hex output is uppercase
// ---------------------------------------------------------------------------
TEST(UtilsTest, ToHexIsUppercase) {
    std::string s = to_hex<uint8_t>(0xAB, 2);
    for (char c : s) {
        EXPECT_FALSE(std::islower(static_cast<unsigned char>(c)))
            << "Found lowercase char: " << c;
    }
}

// ---------------------------------------------------------------------------
// Test: to_hex with minimum digit width uses at least N digits
// Note: the 'digits' parameter is a MINIMUM width (like printf %0Nd).
// Values wider than 'digits' are printed fully.
// ---------------------------------------------------------------------------
TEST(UtilsTest, ToHexMinimumDigitWidth) {
    // 0x0F fits in 1 hex digit
    EXPECT_EQ(to_hex<uint8_t>(0x0F, 1), "F");
    // 0x10 needs 2 hex digits even with minimum width 1
    EXPECT_EQ(to_hex<uint8_t>(0x10, 1), "10");
    // 0x00 with width 1 → "0"
    EXPECT_EQ(to_hex<uint8_t>(0x00, 1), "0");
}

// ---------------------------------------------------------------------------
// Test: to_hex zero-pads shorter values
// ---------------------------------------------------------------------------
TEST(UtilsTest, ToHexZeroPads) {
    EXPECT_EQ(to_hex<uint16_t>(0x0001, 4), "0001");
    EXPECT_EQ(to_hex<uint8_t>(0x02, 2), "02");
}

// ---------------------------------------------------------------------------
// Test: to_hex with value 0
// ---------------------------------------------------------------------------
TEST(UtilsTest, ToHexZeroValue) {
    EXPECT_EQ(to_hex<uint32_t>(0, 8), "00000000");
    EXPECT_EQ(to_hex<uint8_t>(0, 2),  "00");
}

// ---------------------------------------------------------------------------
// Test: to_hex result is at least the requested digit width
// ---------------------------------------------------------------------------
TEST(UtilsTest, ToHexResultLengthAtLeastRequested) {
    // Values that fit within the requested width are zero-padded
    for (int digits : {2, 4, 8}) {
        std::string result = to_hex<uint32_t>(0xAB, digits);
        EXPECT_EQ(static_cast<int>(result.size()), digits)
            << "Expected " << digits << " digits for value 0xAB";
    }
    // A value wider than digits=1 will produce more than 1 char
    std::string r1 = to_hex<uint8_t>(0xAB, 1);
    EXPECT_EQ(r1, "AB"); // 0xAB needs 2 chars; minimum width 1 doesn't truncate
    EXPECT_GE(static_cast<int>(r1.size()), 1);
}
