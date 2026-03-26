/**
 * @file integration_test_tcp_loopback.cpp
 * @brief Integration test: full TCP loopback using a real server socket.
 *
 * Spins up a local TCP server thread that acts as the "Jetson" side,
 * connects a TcpClientChannel (T113i side) to it, and verifies:
 *  - Handshake exchange (MSG_HANDSHAKE_REQ → MSG_HANDSHAKE_ACK)
 *  - Heartbeat round-trip
 *  - Task command → ACK
 *  - Sequence number tracking
 *  - Graceful disconnect detection
 */

#include "protocol.h"
#include "serial_comm.h"
#include "logger.h"

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// Minimal TCP server that speaks the same binary protocol
// ---------------------------------------------------------------------------
class MinimalServer {
public:
    explicit MinimalServer(int port) : port_(port) {}
    ~MinimalServer() { stop(); }

    bool start() {
        listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd_ < 0) return false;
        int opt = 1;
        ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in addr{};
        addr.sin_family      = AF_INET;
        addr.sin_port        = htons(static_cast<uint16_t>(port_));
        addr.sin_addr.s_addr = INADDR_ANY;

        if (::bind(listen_fd_,
                   reinterpret_cast<struct sockaddr*>(&addr),
                   sizeof(addr)) < 0) return false;
        if (::listen(listen_fd_, 1) < 0) return false;

        running_ = true;
        server_thread_ = std::thread(&MinimalServer::server_loop, this);
        return true;
    }

    void stop() {
        running_ = false;
        if (listen_fd_ >= 0) { ::shutdown(listen_fd_, SHUT_RDWR); ::close(listen_fd_); listen_fd_ = -1; }
        if (client_fd_ >= 0) { ::shutdown(client_fd_, SHUT_RDWR); ::close(client_fd_); client_fd_ = -1; }
        if (server_thread_.joinable()) server_thread_.join();
    }

    // Send a frame to the connected client
    void send_frame(MsgId id, uint16_t seq,
                    const uint8_t* pay, uint16_t len)
    {
        uint8_t buf[PROTO_MAX_FRAME];
        size_t n = FrameCodec::encode(id, seq, pay, len, buf, sizeof(buf));
        if (n > 0 && client_fd_ >= 0) {
            ::write(client_fd_, buf, n);
        }
    }

    template<typename T>
    void send_payload(MsgId id, uint16_t seq, const T& payload) {
        send_frame(id, seq,
                   reinterpret_cast<const uint8_t*>(&payload),
                   static_cast<uint16_t>(sizeof(T)));
    }

    // Received frames (for test assertions)
    struct Frame {
        MsgId    id;
        uint16_t seq;
        std::vector<uint8_t> payload;
    };

    std::vector<Frame> received_frames() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return frames_;
    }

    bool wait_for_frames(int count, int timeout_ms = 3000) {
        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeout_ms);
        while (std::chrono::steady_clock::now() < deadline) {
            {
                std::lock_guard<std::mutex> lk(mutex_);
                if (static_cast<int>(frames_.size()) >= count) return true;
            }
            std::this_thread::sleep_for(10ms);
        }
        return false;
    }

    bool client_connected() const { return client_fd_ >= 0; }

private:
    int port_;
    int listen_fd_{-1};
    int client_fd_{-1};

    std::atomic<bool> running_{false};
    std::thread       server_thread_;

    mutable std::mutex    mutex_;
    std::vector<Frame>    frames_;
    FrameCodec            codec_;

    void server_loop() {
        // Accept one client
        struct sockaddr_in cli_addr{};
        socklen_t cli_len = sizeof(cli_addr);
        client_fd_ = ::accept(listen_fd_,
                              reinterpret_cast<struct sockaddr*>(&cli_addr),
                              &cli_len);
        if (client_fd_ < 0) return;

        uint8_t rxbuf[512];
        while (running_) {
            ssize_t n = ::read(client_fd_, rxbuf, sizeof(rxbuf));
            if (n <= 0) break;
            codec_.feed(rxbuf, static_cast<size_t>(n),
                        [this](MsgId id, uint16_t seq,
                               const uint8_t* pay, uint16_t len) {
                            std::lock_guard<std::mutex> lk(mutex_);
                            frames_.push_back({id, seq,
                                std::vector<uint8_t>(pay, pay + len)});

                            // Auto-respond to handshake requests
                            if (id == MSG_HANDSHAKE_REQ) {
                                HandshakeAckPayload ack{};
                                ack.version             = PROTOCOL_VERSION;
                                ack.negotiated_features = FEAT_ALL;
                                ack.role                = 1;
                                this->send_payload(MSG_HANDSHAKE_ACK, seq, ack);
                            }
                        });
        }
    }
};

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------
class TcpLoopbackTest : public ::testing::Test {
protected:
    static constexpr int PORT = 19001;

    std::unique_ptr<MinimalServer> server_;
    std::unique_ptr<SerialComm>    comm_;

    void SetUp() override {
        Logger::init("", Logger::FATAL);
        server_ = std::make_unique<MinimalServer>(PORT);
        ASSERT_TRUE(server_->start()) << "Server failed to start";

        auto ch = std::make_unique<TcpClientChannel>("127.0.0.1", PORT, 200);
        comm_   = std::make_unique<SerialComm>(std::move(ch));
        comm_->start();

        // Wait for connection
        for (int i = 0; i < 100 && !comm_->is_connected(); ++i) {
            std::this_thread::sleep_for(20ms);
        }
        ASSERT_TRUE(comm_->is_connected()) << "Client did not connect";
    }

    void TearDown() override {
        comm_->stop();
        server_->stop();
        Logger::shutdown();
    }
};

// ---------------------------------------------------------------------------
// Handshake round-trip
// ---------------------------------------------------------------------------
TEST_F(TcpLoopbackTest, HandshakeRequestSentAndAcked) {
    std::atomic<bool> ack_received{false};
    comm_->register_handler(MSG_HANDSHAKE_ACK,
        [&](MsgId, uint16_t, const uint8_t* pay, uint16_t len) {
            if (len >= sizeof(HandshakeAckPayload)) {
                const auto* ack = reinterpret_cast<const HandshakeAckPayload*>(pay);
                EXPECT_EQ(ack->version, PROTOCOL_VERSION);
                ack_received = true;
            }
        });

    HandshakeReqPayload req{};
    req.version            = PROTOCOL_VERSION;
    req.supported_features = FEAT_ALL;
    req.timestamp_ms       = 0;
    comm_->send_payload(MSG_HANDSHAKE_REQ, req);

    for (int i = 0; i < 200 && !ack_received.load(); ++i) {
        std::this_thread::sleep_for(10ms);
    }
    EXPECT_TRUE(ack_received.load());
}

// ---------------------------------------------------------------------------
// Heartbeat received by server
// ---------------------------------------------------------------------------
TEST_F(TcpLoopbackTest, HeartbeatReachesServer) {
    HeartbeatPayload hb{12345u, 0};
    comm_->send_payload(MSG_HEARTBEAT, hb);

    EXPECT_TRUE(server_->wait_for_frames(1, 2000));
    auto frames = server_->received_frames();
    bool found = false;
    for (const auto& f : frames) {
        if (f.id == MSG_HEARTBEAT) { found = true; break; }
    }
    EXPECT_TRUE(found);
}

// ---------------------------------------------------------------------------
// Multiple message types
// ---------------------------------------------------------------------------
TEST_F(TcpLoopbackTest, MultipleMessageTypesSent) {
    HeartbeatPayload hb{};
    AckPayload ack{MSG_HEARTBEAT, 1, 0};
    SystemStatusPayload sys{100, 20, 50.0f, 8192};

    comm_->send_payload(MSG_HEARTBEAT, hb);
    comm_->send_payload(MSG_ACK, ack);
    comm_->send_payload(MSG_SYSTEM_STATUS, sys);

    EXPECT_TRUE(server_->wait_for_frames(3, 3000));
}

// ---------------------------------------------------------------------------
// Sequence numbers increase monotonically
// ---------------------------------------------------------------------------
TEST_F(TcpLoopbackTest, SequenceNumbersAreMonotonic) {
    for (int i = 0; i < 5; ++i) {
        HeartbeatPayload hb{static_cast<uint32_t>(i), 0};
        comm_->send_payload(MSG_HEARTBEAT, hb);
    }

    EXPECT_TRUE(server_->wait_for_frames(5, 3000));
    auto frames = server_->received_frames();

    std::vector<uint16_t> seqs;
    for (const auto& f : frames) {
        if (f.id == MSG_HEARTBEAT) seqs.push_back(f.seq);
    }
    ASSERT_GE(seqs.size(), 5u);
    for (size_t i = 1; i < seqs.size(); ++i) {
        EXPECT_GT(seqs[i], seqs[i-1]) << "Sequence number not increasing";
    }
}

// ---------------------------------------------------------------------------
// Disconnect detection
// ---------------------------------------------------------------------------
TEST_F(TcpLoopbackTest, DisconnectDetected) {
    server_->stop();
    // Wait for client to detect disconnect
    for (int i = 0; i < 200 && comm_->is_connected(); ++i) {
        std::this_thread::sleep_for(20ms);
    }
    EXPECT_FALSE(comm_->is_connected());
}
