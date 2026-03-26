/**
 * @file test_protocol.cpp
 * @brief Unit tests for binary frame encoding/decoding and CRC validation.
 *
 * Tests: crc16_ccitt, FrameCodec::encode/feed, frame reassembly,
 * CRC rejection, handshake payload layout, and payload struct sizes.
 */

#include "protocol.h"
#include "serial_comm.h"   // FrameCodec

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

// ---------------------------------------------------------------------------
// CRC-16/CCITT tests
// ---------------------------------------------------------------------------
TEST(Crc16Test, ZeroLengthInput) {
    EXPECT_EQ(crc16_ccitt(nullptr, 0), 0xFFFFu); // init value
}

TEST(Crc16Test, SingleByte) {
    uint8_t d = 0x31; // '1'
    uint16_t crc = crc16_ccitt(&d, 1);
    EXPECT_NE(crc, 0xFFFFu);
}

TEST(Crc16Test, KnownVector) {
    // "123456789" → CRC-16/CCITT-FALSE = 0x29B1
    const char* s = "123456789";
    uint16_t crc = crc16_ccitt(
        reinterpret_cast<const uint8_t*>(s), std::strlen(s));
    EXPECT_EQ(crc, 0x29B1u);
}

TEST(Crc16Test, DifferentDataGivesDifferentCrc) {
    uint8_t a[] = {0x01, 0x02};
    uint8_t b[] = {0x01, 0x03};
    EXPECT_NE(crc16_ccitt(a, 2), crc16_ccitt(b, 2));
}

TEST(Crc16Test, SameDataSameCrc) {
    uint8_t d[] = {0xAA, 0xBB, 0xCC};
    EXPECT_EQ(crc16_ccitt(d, 3), crc16_ccitt(d, 3));
}

// ---------------------------------------------------------------------------
// Frame encoding tests
// ---------------------------------------------------------------------------
class FrameEncodeTest : public ::testing::Test {
protected:
    uint8_t  buf_[PROTO_MAX_FRAME]{};
    FrameCodec codec_;
};

static size_t encode_helper(MsgId id, uint16_t seq,
                             const uint8_t* pay, uint16_t pay_len,
                             uint8_t* buf, size_t cap)
{
    return FrameCodec::encode(id, seq, pay, pay_len, buf, cap);
}

TEST_F(FrameEncodeTest, HeaderBytes) {
    HeartbeatPayload hb{};
    size_t n = encode_helper(
        MSG_HEARTBEAT, 1,
        reinterpret_cast<const uint8_t*>(&hb), sizeof(hb),
        buf_, sizeof(buf_));
    EXPECT_GT(n, 0u);
    EXPECT_EQ(buf_[0], PROTO_HEADER_0);
    EXPECT_EQ(buf_[1], PROTO_HEADER_1);
    EXPECT_EQ(buf_[2], MSG_HEARTBEAT);
}

TEST_F(FrameEncodeTest, SequenceFieldLE) {
    uint16_t seq = 0x1234;
    HeartbeatPayload hb{};
    encode_helper(MSG_HEARTBEAT, seq,
                  reinterpret_cast<const uint8_t*>(&hb), sizeof(hb),
                  buf_, sizeof(buf_));
    // seq at byte offset 3 (little-endian)
    uint16_t seq_out = static_cast<uint16_t>(buf_[3]) |
                       (static_cast<uint16_t>(buf_[4]) << 8);
    EXPECT_EQ(seq_out, seq);
}

TEST_F(FrameEncodeTest, PayloadLengthFieldLE) {
    HeartbeatPayload hb{};
    uint16_t pay_len = sizeof(hb);
    encode_helper(MSG_HEARTBEAT, 0,
                  reinterpret_cast<const uint8_t*>(&hb), pay_len,
                  buf_, sizeof(buf_));
    // len at byte offset 5 (little-endian)
    uint16_t len_out = static_cast<uint16_t>(buf_[5]) |
                       (static_cast<uint16_t>(buf_[6]) << 8);
    EXPECT_EQ(len_out, pay_len);
}

TEST_F(FrameEncodeTest, TotalFrameSize) {
    HeartbeatPayload hb{};
    size_t n = encode_helper(MSG_HEARTBEAT, 0,
                             reinterpret_cast<const uint8_t*>(&hb), sizeof(hb),
                             buf_, sizeof(buf_));
    EXPECT_EQ(n, static_cast<size_t>(
        PROTO_HEADER_TOT + sizeof(hb) + PROTO_CRC_SZ));
}

TEST_F(FrameEncodeTest, BufferTooSmallReturnsZero) {
    HeartbeatPayload hb{};
    uint8_t tiny[4]{};
    size_t n = encode_helper(MSG_HEARTBEAT, 0,
                             reinterpret_cast<const uint8_t*>(&hb), sizeof(hb),
                             tiny, sizeof(tiny));
    EXPECT_EQ(n, 0u);
}

TEST_F(FrameEncodeTest, MaxPayloadEncodes) {
    std::vector<uint8_t> pay(PROTO_MAX_PAYLOAD, 0xAB);
    size_t n = encode_helper(MSG_TASK_COMMAND, 0,
                             pay.data(), static_cast<uint16_t>(pay.size()),
                             buf_, sizeof(buf_));
    EXPECT_EQ(n, static_cast<size_t>(
        PROTO_HEADER_TOT + PROTO_MAX_PAYLOAD + PROTO_CRC_SZ));
}

// ---------------------------------------------------------------------------
// Frame decode (FrameCodec::feed) tests
// ---------------------------------------------------------------------------
TEST_F(FrameEncodeTest, FeedDecodesCompleteFrame) {
    HeartbeatPayload hb{12345, 0};
    size_t n = encode_helper(MSG_HEARTBEAT, 7,
                             reinterpret_cast<const uint8_t*>(&hb), sizeof(hb),
                             buf_, sizeof(buf_));
    ASSERT_GT(n, 0u);

    MsgId    rx_id  = 0;
    uint16_t rx_seq = 0;
    bool     called = false;

    codec_.feed(buf_, n, [&](MsgId id, uint16_t seq, const uint8_t* pay, uint16_t len) {
        rx_id  = id;
        rx_seq = seq;
        called = true;
        ASSERT_EQ(len, sizeof(HeartbeatPayload));
        HeartbeatPayload out{};
        std::memcpy(&out, pay, sizeof(out));
        EXPECT_EQ(out.timestamp_ms, 12345u);
    });

    EXPECT_TRUE(called);
    EXPECT_EQ(rx_id, MSG_HEARTBEAT);
    EXPECT_EQ(rx_seq, 7u);
}

TEST_F(FrameEncodeTest, FeedRejectsBadCrc) {
    HeartbeatPayload hb{};
    size_t n = encode_helper(MSG_HEARTBEAT, 0,
                             reinterpret_cast<const uint8_t*>(&hb), sizeof(hb),
                             buf_, sizeof(buf_));
    // Corrupt the last CRC byte
    buf_[n - 1] ^= 0xFF;

    bool called = false;
    codec_.feed(buf_, n, [&](MsgId, uint16_t, const uint8_t*, uint16_t) {
        called = true;
    });
    EXPECT_FALSE(called);
}

TEST_F(FrameEncodeTest, FeedHandlesFragmentedInput) {
    HeartbeatPayload hb{42, 1};
    size_t n = encode_helper(MSG_HEARTBEAT, 3,
                             reinterpret_cast<const uint8_t*>(&hb), sizeof(hb),
                             buf_, sizeof(buf_));

    bool called = false;
    // Feed one byte at a time
    for (size_t i = 0; i < n; ++i) {
        codec_.feed(buf_ + i, 1, [&](MsgId, uint16_t, const uint8_t* pay, uint16_t len) {
            EXPECT_EQ(len, sizeof(HeartbeatPayload));
            HeartbeatPayload out{};
            std::memcpy(&out, pay, sizeof(out));
            EXPECT_EQ(out.timestamp_ms, 42u);
            called = true;
        });
    }
    EXPECT_TRUE(called);
}

TEST_F(FrameEncodeTest, FeedHandlesMultipleFrames) {
    HeartbeatPayload hb1{1, 0};
    HeartbeatPayload hb2{2, 0};
    std::vector<uint8_t> stream;
    stream.resize(2 * PROTO_MAX_FRAME);

    size_t n1 = encode_helper(MSG_HEARTBEAT, 1,
                              reinterpret_cast<const uint8_t*>(&hb1), sizeof(hb1),
                              stream.data(), stream.size());
    size_t n2 = encode_helper(MSG_HEARTBEAT, 2,
                              reinterpret_cast<const uint8_t*>(&hb2), sizeof(hb2),
                              stream.data() + n1, stream.size() - n1);

    int count = 0;
    codec_.feed(stream.data(), n1 + n2,
                [&](MsgId, uint16_t seq, const uint8_t*, uint16_t) {
                    EXPECT_TRUE(seq == 1 || seq == 2);
                    ++count;
                });
    EXPECT_EQ(count, 2);
}

// ---------------------------------------------------------------------------
// Payload struct sizes and packing
// ---------------------------------------------------------------------------
TEST(ProtocolPayloadTest, HandshakeReqPayloadSize) {
    // version(2) + features(4) + timestamp(4) = 10 bytes
    EXPECT_EQ(sizeof(HandshakeReqPayload), 10u);
}

TEST(ProtocolPayloadTest, HandshakeAckPayloadSize) {
    // version(2) + features(4) + role(1) = 7 bytes
    EXPECT_EQ(sizeof(HandshakeAckPayload), 7u);
}

TEST(ProtocolPayloadTest, AckPayloadSize) {
    // msg_id(1) + seq(2) + status(1) = 4 bytes
    EXPECT_EQ(sizeof(AckPayload), 4u);
}

TEST(ProtocolPayloadTest, HeartbeatPayloadSize) {
    // timestamp(4) + role(1) = 5 bytes
    EXPECT_EQ(sizeof(HeartbeatPayload), 5u);
}

TEST(ProtocolPayloadTest, TaskCommandPayloadSize) {
    // task_id(1) + arg(4) + name[32] = 37 bytes
    EXPECT_EQ(sizeof(TaskCommandPayload), 37u);
}

// ---------------------------------------------------------------------------
// Protocol version constant
// ---------------------------------------------------------------------------
TEST(ProtocolVersionTest, VersionIsOneZero) {
    EXPECT_EQ(PROTOCOL_VERSION, 0x0100u);
}

// ---------------------------------------------------------------------------
// Feature bitmask sanity
// ---------------------------------------------------------------------------
TEST(ProtocolFeatureTest, FeatAllCoversAllFeatures) {
    EXPECT_EQ(FEAT_ALL, FEAT_GPIO | FEAT_ADC | FEAT_POWER | FEAT_TASK | FEAT_INPUT);
}
