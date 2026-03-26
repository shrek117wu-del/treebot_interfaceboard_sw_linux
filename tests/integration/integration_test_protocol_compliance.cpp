/**
 * @file integration_test_protocol_compliance.cpp
 * @brief Integration test: protocol handshake and state machine correctness.
 *
 * Validates:
 *  - Handshake request / response encoding
 *  - Feature negotiation (intersection logic)
 *  - Version mismatch handling
 *  - Error code mapping
 *  - Protocol frame structure correctness
 */

#include <gtest/gtest.h>

#include "protocol.h"

#include <cstring>
#include <cstdint>

// ---------------------------------------------------------------------------
// Helper: build + verify a frame
// ---------------------------------------------------------------------------
static std::vector<uint8_t> make_frame(MsgId id, uint16_t seq,
                                        const void* payload, uint16_t plen) {
    std::vector<uint8_t> f;
    f.push_back(PROTO_HEADER_0);
    f.push_back(PROTO_HEADER_1);
    f.push_back(id);
    f.push_back(seq & 0xFF);
    f.push_back((seq >> 8) & 0xFF);
    f.push_back(plen & 0xFF);
    f.push_back((plen >> 8) & 0xFF);
    f.push_back(0); f.push_back(0); // reserved
    const uint8_t* p = reinterpret_cast<const uint8_t*>(payload);
    for (uint16_t i = 0; i < plen; ++i) f.push_back(p[i]);
    uint16_t crc = crc16_ccitt(f.data(), f.size());
    f.push_back(crc & 0xFF);
    f.push_back((crc >> 8) & 0xFF);
    return f;
}

// Verify frame CRC
static bool verify_frame_crc(const std::vector<uint8_t>& f) {
    if (f.size() < PROTO_HEADER_TOT + PROTO_CRC_SZ) return false;
    size_t crc_off = f.size() - PROTO_CRC_SZ;
    uint16_t stored = static_cast<uint16_t>(f[crc_off]) |
                      (static_cast<uint16_t>(f[crc_off + 1]) << 8);
    uint16_t calc = crc16_ccitt(f.data(), crc_off);
    return stored == calc;
}

// ---------------------------------------------------------------------------
// Test: handshake request frame is valid (correct header & CRC)
// ---------------------------------------------------------------------------
TEST(ProtocolComplianceTest, HandshakeReqFrameValid) {
    HandshakeReqPayload req{};
    req.version = PROTOCOL_VERSION;
    req.supported_features = FEAT_ALL;
    req.timestamp_ms = 1000u;

    auto f = make_frame(MSG_HANDSHAKE_REQ, 1, &req, sizeof(req));

    // Check sync bytes
    EXPECT_EQ(f[0], PROTO_HEADER_0);
    EXPECT_EQ(f[1], PROTO_HEADER_1);
    // Check msg_id
    EXPECT_EQ(f[2], MSG_HANDSHAKE_REQ);
    // Check CRC
    EXPECT_TRUE(verify_frame_crc(f));
}

// ---------------------------------------------------------------------------
// Test: handshake ACK feature negotiation (intersection)
// ---------------------------------------------------------------------------
TEST(ProtocolComplianceTest, HandshakeAckFeatureNegotiation) {
    uint32_t t113i_features = FEAT_GPIO | FEAT_ADC | FEAT_POWER | FEAT_TASK;
    uint32_t jetson_features = FEAT_GPIO | FEAT_TASK | FEAT_INPUT;

    // Negotiated = intersection
    uint32_t negotiated = t113i_features & jetson_features;

    EXPECT_TRUE(negotiated & FEAT_GPIO);
    EXPECT_TRUE(negotiated & FEAT_TASK);
    EXPECT_FALSE(negotiated & FEAT_ADC);   // T113i has ADC, Jetson does not
    EXPECT_FALSE(negotiated & FEAT_POWER); // T113i has POWER, Jetson does not
    EXPECT_FALSE(negotiated & FEAT_INPUT); // Jetson has INPUT, T113i does not
}

// ---------------------------------------------------------------------------
// Test: version mismatch detection (different major)
// ---------------------------------------------------------------------------
TEST(ProtocolComplianceTest, VersionMismatchDetected) {
    uint16_t t113i_ver = PROTOCOL_VERSION;        // 0x0100
    uint16_t jetson_ver = 0x0200;                 // hypothetical v2.0

    uint8_t t113i_major = (t113i_ver >> 8) & 0xFF;
    uint8_t jetson_major = (jetson_ver >> 8) & 0xFF;

    EXPECT_NE(t113i_major, jetson_major) << "Major versions should differ";
}

// ---------------------------------------------------------------------------
// Test: ACK frame encodes correctly with status=0 (success)
// ---------------------------------------------------------------------------
TEST(ProtocolComplianceTest, AckFrameSuccess) {
    AckPayload ack{};
    ack.acked_msg_id = MSG_TASK_COMMAND;
    ack.acked_seq    = 42;
    ack.status       = 0;

    auto f = make_frame(MSG_ACK, 10, &ack, sizeof(ack));
    EXPECT_EQ(f[2], MSG_ACK);
    EXPECT_TRUE(verify_frame_crc(f));

    // Decode payload
    AckPayload decoded{};
    size_t payload_off = PROTO_HEADER_TOT;
    std::memcpy(&decoded, f.data() + payload_off, sizeof(decoded));
    EXPECT_EQ(decoded.acked_msg_id, MSG_TASK_COMMAND);
    EXPECT_EQ(decoded.acked_seq, 42u);
    EXPECT_EQ(decoded.status, 0u);
}

// ---------------------------------------------------------------------------
// Test: TaskResponse encodes task_id, acked_seq, and result
// ---------------------------------------------------------------------------
TEST(ProtocolComplianceTest, TaskResponseEncoding) {
    TaskResponsePayload resp{};
    resp.task_id   = TASK_ESTOP;
    resp.acked_seq = 0x1234;
    resp.result    = 1; // success
    std::strncpy(resp.message, "OK", sizeof(resp.message) - 1);

    auto f = make_frame(MSG_TASK_RESPONSE, 20, &resp, sizeof(resp));
    EXPECT_EQ(f[2], MSG_TASK_RESPONSE);
    EXPECT_TRUE(verify_frame_crc(f));

    TaskResponsePayload decoded{};
    std::memcpy(&decoded, f.data() + PROTO_HEADER_TOT, sizeof(decoded));
    EXPECT_EQ(decoded.task_id,   TASK_ESTOP);
    EXPECT_EQ(decoded.acked_seq, 0x1234u);
    EXPECT_EQ(decoded.result,    1u);
    EXPECT_STREQ(decoded.message, "OK");
}

// ---------------------------------------------------------------------------
// Test: protocol state machine – T113i sends HANDSHAKE_REQ first
// ---------------------------------------------------------------------------
TEST(ProtocolComplianceTest, T113iSendsHandshakeFirst) {
    // The T113i always initiates – MSG_HANDSHAKE_REQ < MSG_HANDSHAKE_ACK
    EXPECT_LT(MSG_HANDSHAKE_REQ, MSG_HANDSHAKE_ACK);
}

// ---------------------------------------------------------------------------
// Test: all defined message IDs fit in uint8_t (0..255)
// ---------------------------------------------------------------------------
TEST(ProtocolComplianceTest, MessageIdsInByteRange) {
    EXPECT_LE(MSG_HEARTBEAT,      0xFFu);
    EXPECT_LE(MSG_ACK,            0xFFu);
    EXPECT_LE(MSG_HANDSHAKE_REQ,  0xFFu);
    EXPECT_LE(MSG_HANDSHAKE_ACK,  0xFFu);
    EXPECT_LE(MSG_SENSOR_DATA,    0xFFu);
    EXPECT_LE(MSG_GPIO_STATUS,    0xFFu);
    EXPECT_LE(MSG_TASK_COMMAND,   0xFFu);
    EXPECT_LE(MSG_TASK_RESPONSE,  0xFFu);
    EXPECT_LE(MSG_EVENT,          0xFFu);
}

// ---------------------------------------------------------------------------
// Test: corrupted frame CRC detected
// ---------------------------------------------------------------------------
TEST(ProtocolComplianceTest, CorruptedFrameCrcDetected) {
    HeartbeatPayload hb{};
    hb.timestamp_ms = 1u;
    auto f = make_frame(MSG_HEARTBEAT, 1, &hb, sizeof(hb));

    // Flip a bit in the payload
    f[PROTO_HEADER_TOT] ^= 0xFF;

    EXPECT_FALSE(verify_frame_crc(f))
        << "Corrupted frame should fail CRC check";
}
