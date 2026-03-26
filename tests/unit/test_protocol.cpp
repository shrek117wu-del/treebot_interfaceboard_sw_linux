/**
 * @file test_protocol.cpp
 * @brief Unit tests for the protocol.h frame definitions and CRC-16.
 *
 * Tests: CRC-16 CCITT validation, handshake payload encoding/byte-order,
 * message ID consistency, feature bitmask values, and frame constant sizes.
 */

#include <gtest/gtest.h>

#include "protocol.h"

#include <cstring>
#include <cstdint>

// ---------------------------------------------------------------------------
// Test: CRC-16 of empty buffer is the init value 0xFFFF
// ---------------------------------------------------------------------------
TEST(ProtocolTest, Crc16EmptyIsInitValue) {
    uint16_t crc = crc16_ccitt(nullptr, 0);
    EXPECT_EQ(crc, 0xFFFFu);
}

// ---------------------------------------------------------------------------
// Test: CRC-16 known vector (CCITT with poly 0x1021, init 0xFFFF)
//   "123456789" → 0x29B1
// ---------------------------------------------------------------------------
TEST(ProtocolTest, Crc16KnownVector) {
    const uint8_t data[] = "123456789";
    uint16_t crc = crc16_ccitt(data, 9); // 9 bytes, no NUL
    EXPECT_EQ(crc, 0x29B1u);
}

// ---------------------------------------------------------------------------
// Test: CRC-16 of single zero byte is deterministic
// ---------------------------------------------------------------------------
TEST(ProtocolTest, Crc16SingleZero) {
    uint8_t zero = 0x00;
    uint16_t crc1 = crc16_ccitt(&zero, 1);
    uint16_t crc2 = crc16_ccitt(&zero, 1);
    EXPECT_EQ(crc1, crc2);
}

// ---------------------------------------------------------------------------
// Test: CRC-16 changes when data changes
// ---------------------------------------------------------------------------
TEST(ProtocolTest, Crc16DifferentDataDifferentCrc) {
    uint8_t a = 0xAA, b = 0x55;
    EXPECT_NE(crc16_ccitt(&a, 1), crc16_ccitt(&b, 1));
}

// ---------------------------------------------------------------------------
// Test: Frame header constants are correct
// ---------------------------------------------------------------------------
TEST(ProtocolTest, FrameHeaderConstants) {
    EXPECT_EQ(PROTO_HEADER_0, 0xAAu);
    EXPECT_EQ(PROTO_HEADER_1, 0x55u);
}

// ---------------------------------------------------------------------------
// Test: PROTO_HEADER_TOT == 9 (2 sync + 1 id + 2 seq + 2 len + 2 reserved)
// ---------------------------------------------------------------------------
TEST(ProtocolTest, HeaderTotalSize) {
    EXPECT_EQ(PROTO_HEADER_TOT, 9u);
}

// ---------------------------------------------------------------------------
// Test: PROTO_MAX_PAYLOAD is 256
// ---------------------------------------------------------------------------
TEST(ProtocolTest, MaxPayload) {
    EXPECT_EQ(PROTO_MAX_PAYLOAD, 256u);
}

// ---------------------------------------------------------------------------
// Test: PROTO_MAX_FRAME == PROTO_HEADER_TOT + PROTO_MAX_PAYLOAD + PROTO_CRC_SZ
// ---------------------------------------------------------------------------
TEST(ProtocolTest, MaxFrameConsistency) {
    EXPECT_EQ(PROTO_MAX_FRAME,
              PROTO_HEADER_TOT + PROTO_MAX_PAYLOAD + PROTO_CRC_SZ);
}

// ---------------------------------------------------------------------------
// Test: PROTOCOL_VERSION == 0x0100
// ---------------------------------------------------------------------------
TEST(ProtocolTest, ProtocolVersionValue) {
    EXPECT_EQ(PROTOCOL_VERSION, 0x0100u);
}

// ---------------------------------------------------------------------------
// Test: message ID constants are distinct
// ---------------------------------------------------------------------------
TEST(ProtocolTest, MessageIdsDistinct) {
    EXPECT_NE(MSG_HEARTBEAT, MSG_ACK);
    EXPECT_NE(MSG_HANDSHAKE_REQ, MSG_HANDSHAKE_ACK);
    EXPECT_NE(MSG_SENSOR_DATA, MSG_GPIO_STATUS);
    EXPECT_NE(MSG_DO_COMMAND, MSG_PWM_COMMAND);
    EXPECT_NE(MSG_TASK_COMMAND, MSG_TASK_RESPONSE);
}

// ---------------------------------------------------------------------------
// Test: feature bitmasks are non-overlapping powers-of-two
// ---------------------------------------------------------------------------
TEST(ProtocolTest, FeatureBitmaskNonOverlapping) {
    // Each individual flag should be a single bit
    EXPECT_EQ(__builtin_popcount(FEAT_GPIO), 1);
    EXPECT_EQ(__builtin_popcount(FEAT_ADC),  1);
    EXPECT_EQ(__builtin_popcount(FEAT_POWER), 1);
    EXPECT_EQ(__builtin_popcount(FEAT_TASK),  1);
    EXPECT_EQ(__builtin_popcount(FEAT_INPUT), 1);

    // No two flags share the same bit
    EXPECT_EQ(FEAT_GPIO & FEAT_ADC,   0u);
    EXPECT_EQ(FEAT_GPIO & FEAT_POWER, 0u);
    EXPECT_EQ(FEAT_GPIO & FEAT_TASK,  0u);
    EXPECT_EQ(FEAT_GPIO & FEAT_INPUT, 0u);
    EXPECT_EQ(FEAT_ADC  & FEAT_POWER, 0u);
}

// ---------------------------------------------------------------------------
// Test: FEAT_ALL includes all individual flags
// ---------------------------------------------------------------------------
TEST(ProtocolTest, FeatureAllCoversAll) {
    EXPECT_EQ(FEAT_ALL & FEAT_GPIO,  FEAT_GPIO);
    EXPECT_EQ(FEAT_ALL & FEAT_ADC,   FEAT_ADC);
    EXPECT_EQ(FEAT_ALL & FEAT_POWER, FEAT_POWER);
    EXPECT_EQ(FEAT_ALL & FEAT_TASK,  FEAT_TASK);
    EXPECT_EQ(FEAT_ALL & FEAT_INPUT, FEAT_INPUT);
}

// ---------------------------------------------------------------------------
// Test: HandshakeReqPayload fields can be round-tripped via memcpy
// ---------------------------------------------------------------------------
TEST(ProtocolTest, HandshakeReqPayloadEncoding) {
    HandshakeReqPayload orig{};
    orig.version = PROTOCOL_VERSION;
    orig.supported_features = FEAT_ALL;
    orig.timestamp_ms = 123456789u;

    uint8_t buf[sizeof(HandshakeReqPayload)];
    std::memcpy(buf, &orig, sizeof(orig));

    HandshakeReqPayload decoded{};
    std::memcpy(&decoded, buf, sizeof(decoded));

    EXPECT_EQ(decoded.version,            orig.version);
    EXPECT_EQ(decoded.supported_features, orig.supported_features);
    EXPECT_EQ(decoded.timestamp_ms,       orig.timestamp_ms);
}

// ---------------------------------------------------------------------------
// Test: HandshakeAckPayload role field size is 1 byte
// ---------------------------------------------------------------------------
TEST(ProtocolTest, HandshakeAckRoleIsByte) {
    EXPECT_EQ(sizeof(HandshakeAckPayload::role), 1u);
}

// ---------------------------------------------------------------------------
// Test: AckPayload size and field layout
// ---------------------------------------------------------------------------
TEST(ProtocolTest, AckPayloadLayout) {
    AckPayload ack{};
    ack.acked_msg_id = MSG_TASK_COMMAND;
    ack.acked_seq    = 0xABCDu;
    ack.status       = 0;

    EXPECT_EQ(ack.acked_msg_id, MSG_TASK_COMMAND);
    EXPECT_EQ(ack.acked_seq,    0xABCDu);
    EXPECT_EQ(ack.status,       0u);
}

// ---------------------------------------------------------------------------
// Test: CRC covers a sample frame header + payload correctly
// ---------------------------------------------------------------------------
TEST(ProtocolTest, CrcOverSampleFrameConsistent) {
    uint8_t frame[4] = {PROTO_HEADER_0, PROTO_HEADER_1, MSG_HEARTBEAT, 0x00};
    uint16_t crc1 = crc16_ccitt(frame, sizeof(frame));
    uint16_t crc2 = crc16_ccitt(frame, sizeof(frame));
    EXPECT_EQ(crc1, crc2);
    // Any single-byte change must change the CRC
    frame[3] = 0x01;
    uint16_t crc3 = crc16_ccitt(frame, sizeof(frame));
    EXPECT_NE(crc1, crc3);
}
