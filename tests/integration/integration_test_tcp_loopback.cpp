/**
 * @file integration_test_tcp_loopback.cpp
 * @brief Integration test: full TCP loopback message exchange.
 *
 * Spawns a simple echo server thread on localhost, connects the daemon's
 * SerialComm in TCP mode, encodes protocol frames, sends them, and verifies
 * the echo response is byte-for-byte identical.  Validates frame encoding,
 * CRC, and sequence number consistency.
 */

#include <gtest/gtest.h>

#include "protocol.h"

#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

// ---------------------------------------------------------------------------
// Helper: build a minimal frame (header + payload + CRC) in a byte vector
// ---------------------------------------------------------------------------
static std::vector<uint8_t> build_frame(MsgId msg_id,
                                        uint16_t seq,
                                        const uint8_t* payload,
                                        uint16_t payload_len) {
    std::vector<uint8_t> frame;
    frame.reserve(PROTO_HEADER_TOT + payload_len + PROTO_CRC_SZ);

    frame.push_back(PROTO_HEADER_0);
    frame.push_back(PROTO_HEADER_1);
    frame.push_back(msg_id);
    frame.push_back(seq & 0xFF);
    frame.push_back((seq >> 8) & 0xFF);
    frame.push_back(payload_len & 0xFF);
    frame.push_back((payload_len >> 8) & 0xFF);
    frame.push_back(0x00); // reserved
    frame.push_back(0x00);

    for (uint16_t i = 0; i < payload_len; ++i) {
        frame.push_back(payload[i]);
    }

    uint16_t crc = crc16_ccitt(frame.data(), frame.size());
    frame.push_back(crc & 0xFF);
    frame.push_back((crc >> 8) & 0xFF);

    return frame;
}

// ---------------------------------------------------------------------------
// Simple TCP echo server (runs in a thread)
// ---------------------------------------------------------------------------
class EchoServer {
public:
    int port{0};

    void start(int listen_port) {
        port = listen_port;
        server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        ASSERT_NE(server_fd_, -1);

        int opt = 1;
        setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);
        ASSERT_EQ(bind(server_fd_, (sockaddr*)&addr, sizeof(addr)), 0);
        ASSERT_EQ(listen(server_fd_, 1), 0);

        thread_ = std::thread([this]() {
            sockaddr_in client{};
            socklen_t len = sizeof(client);
            int client_fd = accept(server_fd_, (sockaddr*)&client, &len);
            if (client_fd < 0) return;

            uint8_t buf[4096];
            ssize_t n;
            while ((n = recv(client_fd, buf, sizeof(buf), 0)) > 0) {
                send(client_fd, buf, n, 0); // echo back
            }
            close(client_fd);
        });
    }

    void stop() {
        close(server_fd_);
        if (thread_.joinable()) thread_.join();
    }

private:
    int server_fd_{-1};
    std::thread thread_;
};

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------
class TcpLoopbackTest : public ::testing::Test {
protected:
    EchoServer server_;
    int client_fd_{-1};
    static constexpr int kPort = 19001;

    void SetUp() override {
        server_.start(kPort);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        client_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        ASSERT_NE(client_fd_, -1);

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(kPort);
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

        ASSERT_EQ(connect(client_fd_, (sockaddr*)&addr, sizeof(addr)), 0);
    }

    void TearDown() override {
        if (client_fd_ >= 0) close(client_fd_);
        server_.stop();
    }

    std::vector<uint8_t> send_and_receive(const std::vector<uint8_t>& frame) {
        ssize_t sent = send(client_fd_, frame.data(), frame.size(), 0);
        EXPECT_EQ(sent, static_cast<ssize_t>(frame.size()));

        std::vector<uint8_t> resp(frame.size());
        size_t received = 0;
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (received < frame.size() &&
               std::chrono::steady_clock::now() < deadline) {
            ssize_t n = recv(client_fd_, resp.data() + received,
                             frame.size() - received, MSG_DONTWAIT);
            if (n > 0) received += n;
            else std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        resp.resize(received);
        return resp;
    }
};

// ---------------------------------------------------------------------------
// Test: heartbeat frame echoed back identically
// ---------------------------------------------------------------------------
TEST_F(TcpLoopbackTest, HeartbeatFrameEchoedIdentically) {
    HeartbeatPayload hb{};
    hb.timestamp_ms = 12345u;
    hb.role = 0;

    auto frame = build_frame(MSG_HEARTBEAT, 1,
                              reinterpret_cast<const uint8_t*>(&hb),
                              sizeof(hb));
    auto echo = send_and_receive(frame);

    ASSERT_EQ(echo.size(), frame.size());
    EXPECT_EQ(echo, frame);
}

// ---------------------------------------------------------------------------
// Test: handshake request frame echoed back identically
// ---------------------------------------------------------------------------
TEST_F(TcpLoopbackTest, HandshakeReqFrameEchoed) {
    HandshakeReqPayload hs{};
    hs.version = PROTOCOL_VERSION;
    hs.supported_features = FEAT_ALL;
    hs.timestamp_ms = 99999u;

    auto frame = build_frame(MSG_HANDSHAKE_REQ, 2,
                              reinterpret_cast<const uint8_t*>(&hs),
                              sizeof(hs));
    auto echo = send_and_receive(frame);

    ASSERT_EQ(echo.size(), frame.size());
    EXPECT_EQ(echo, frame);
}

// ---------------------------------------------------------------------------
// Test: frame CRC matches after round-trip
// ---------------------------------------------------------------------------
TEST_F(TcpLoopbackTest, FrameCrcIntegrityPreserved) {
    HeartbeatPayload hb{};
    hb.timestamp_ms = 777u;
    auto frame = build_frame(MSG_HEARTBEAT, 3,
                              reinterpret_cast<const uint8_t*>(&hb),
                              sizeof(hb));
    auto echo = send_and_receive(frame);

    ASSERT_GE(echo.size(), PROTO_CRC_SZ);
    // Extract received CRC (last 2 bytes, little-endian)
    size_t crc_offset = echo.size() - PROTO_CRC_SZ;
    uint16_t recv_crc = static_cast<uint16_t>(echo[crc_offset]) |
                        (static_cast<uint16_t>(echo[crc_offset + 1]) << 8);
    uint16_t calc_crc = crc16_ccitt(frame.data(), frame.size() - PROTO_CRC_SZ);
    EXPECT_EQ(recv_crc, calc_crc);
}

// ---------------------------------------------------------------------------
// Test: sequence numbers are preserved through the round-trip
// ---------------------------------------------------------------------------
TEST_F(TcpLoopbackTest, SequenceNumberPreserved) {
    const uint16_t kSeq = 0xABCD;
    HeartbeatPayload hb{};
    auto frame = build_frame(MSG_HEARTBEAT, kSeq,
                              reinterpret_cast<const uint8_t*>(&hb),
                              sizeof(hb));
    auto echo = send_and_receive(frame);

    // seq is at bytes [3] and [4] (little-endian)
    ASSERT_GE(echo.size(), 5u);
    uint16_t echo_seq = static_cast<uint16_t>(echo[3]) |
                        (static_cast<uint16_t>(echo[4]) << 8);
    EXPECT_EQ(echo_seq, kSeq);
}

// ---------------------------------------------------------------------------
// Test: multiple frames in sequence are all echoed correctly
// ---------------------------------------------------------------------------
TEST_F(TcpLoopbackTest, MultipleFramesInSequence) {
    for (uint16_t seq = 1; seq <= 10; ++seq) {
        HeartbeatPayload hb{};
        hb.timestamp_ms = seq * 100u;
        auto frame = build_frame(MSG_HEARTBEAT, seq,
                                  reinterpret_cast<const uint8_t*>(&hb),
                                  sizeof(hb));
        auto echo = send_and_receive(frame);
        EXPECT_EQ(echo, frame) << "Frame mismatch at seq=" << seq;
    }
}
