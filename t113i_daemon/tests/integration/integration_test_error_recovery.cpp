/**
 * @file integration_test_error_recovery.cpp
 * @brief Integration test: network error recovery, timeout handling,
 *        and automatic reconnection.
 *
 * Tests:
 *  - ConnectionMonitor detects heartbeat timeout
 *  - TcpClientChannel reconnects automatically after server restart
 *  - Multiple reconnect cycles
 *  - Graceful handling of server-side connection drop
 */

#include "connection_monitor.h"
#include "serial_comm.h"
#include "logger.h"

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// Simple accept-and-hold server
// ---------------------------------------------------------------------------
class HoldServer {
public:
    explicit HoldServer(int port) : port_(port) {}
    ~HoldServer() { stop(); }

    bool start() {
        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd_ < 0) return false;
        int opt = 1;
        ::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        struct sockaddr_in addr{};
        addr.sin_family      = AF_INET;
        addr.sin_port        = htons(static_cast<uint16_t>(port_));
        addr.sin_addr.s_addr = INADDR_ANY;
        if (::bind(fd_, reinterpret_cast<struct sockaddr*>(&addr),
                   sizeof(addr)) < 0) return false;
        if (::listen(fd_, 5) < 0) return false;
        running_ = true;
        accept_thread_ = std::thread(&HoldServer::accept_loop, this);
        return true;
    }

    void stop() {
        running_ = false;
        if (fd_ >= 0) { ::shutdown(fd_, SHUT_RDWR); ::close(fd_); fd_ = -1; }
        for (auto c : clients_) { ::close(c); }
        clients_.clear();
        if (accept_thread_.joinable()) accept_thread_.join();
    }

    void drop_all_clients() {
        for (auto c : clients_) { ::close(c); }
        clients_.clear();
    }

    int connection_count() const { return static_cast<int>(clients_.size()); }

private:
    int  port_;
    int  fd_{-1};
    bool running_{false};
    std::thread accept_thread_;
    std::vector<int> clients_;

    void accept_loop() {
        while (running_) {
            struct timeval tv{0, 100000}; // 100ms
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(fd_, &rfds);
            if (::select(fd_ + 1, &rfds, nullptr, nullptr, &tv) <= 0) continue;
            int c = ::accept(fd_, nullptr, nullptr);
            if (c >= 0) clients_.push_back(c);
        }
    }
};

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------
class ErrorRecoveryTest : public ::testing::Test {
protected:
    static constexpr int PORT = 19002;

    void SetUp() override {
        Logger::init("", Logger::FATAL);
    }
    void TearDown() override {
        Logger::shutdown();
    }
};

// ---------------------------------------------------------------------------
// ConnectionMonitor detects heartbeat timeout
// ---------------------------------------------------------------------------
TEST_F(ErrorRecoveryTest, HeartbeatTimeoutDetected) {
    ConnectionMonitor mon(100); // 100 ms timeout
    mon.on_heartbeat_received();

    // Within timeout → healthy
    EXPECT_TRUE(mon.check_health(true));

    // After timeout → unhealthy
    std::this_thread::sleep_for(200ms);
    EXPECT_FALSE(mon.check_health(true));
    EXPECT_EQ(mon.state(), ConnectionMonitor::State::TIMEOUT);
}

// ---------------------------------------------------------------------------
// ConnectionMonitor: reconnect attempt increments, then resets on reconnect
// ---------------------------------------------------------------------------
TEST_F(ErrorRecoveryTest, ReconnectCounterResetAfterSuccess) {
    ConnectionMonitor mon(5000);
    mon.on_reconnect_attempt();
    mon.on_reconnect_attempt();
    EXPECT_EQ(mon.reconnect_attempts(), 2);

    // Simulate successful reconnect
    mon.reset();
    EXPECT_EQ(mon.reconnect_attempts(), 0);
    EXPECT_EQ(mon.state(), ConnectionMonitor::State::DISCONNECTED);
}

// ---------------------------------------------------------------------------
// TcpClientChannel: no server available → not connected
// ---------------------------------------------------------------------------
TEST_F(ErrorRecoveryTest, NoServerMeansNotConnected) {
    auto ch = std::make_unique<TcpClientChannel>("127.0.0.1", 19099, 100);
    SerialComm comm(std::move(ch));
    comm.start();

    std::this_thread::sleep_for(300ms);
    EXPECT_FALSE(comm.is_connected());
    comm.stop();
}

// ---------------------------------------------------------------------------
// TcpClientChannel: connects after server starts
// ---------------------------------------------------------------------------
TEST_F(ErrorRecoveryTest, ReconnectsWhenServerBecomesAvailable) {
    auto ch = std::make_unique<TcpClientChannel>("127.0.0.1", PORT, 200);
    SerialComm comm(std::move(ch));
    comm.start();

    // No server yet
    std::this_thread::sleep_for(100ms);
    EXPECT_FALSE(comm.is_connected());

    // Start server
    HoldServer server(PORT);
    ASSERT_TRUE(server.start());

    // Client should reconnect within ~2 s
    for (int i = 0; i < 200 && !comm.is_connected(); ++i) {
        std::this_thread::sleep_for(20ms);
    }
    EXPECT_TRUE(comm.is_connected());

    comm.stop();
    server.stop();
}

// ---------------------------------------------------------------------------
// TcpClientChannel: reconnects after server drops connection
// ---------------------------------------------------------------------------
TEST_F(ErrorRecoveryTest, ReconnectsAfterServerDropsConnection) {
    HoldServer server(PORT + 1);
    ASSERT_TRUE(server.start());

    auto ch = std::make_unique<TcpClientChannel>("127.0.0.1", PORT + 1, 200);
    SerialComm comm(std::move(ch));
    comm.start();

    // Wait for initial connection
    for (int i = 0; i < 100 && !comm.is_connected(); ++i) {
        std::this_thread::sleep_for(20ms);
    }
    ASSERT_TRUE(comm.is_connected()) << "Initial connect failed";

    // Drop all clients
    server.drop_all_clients();

    // Wait for client to detect disconnect
    for (int i = 0; i < 100 && comm.is_connected(); ++i) {
        std::this_thread::sleep_for(20ms);
    }

    // Client will attempt to reconnect automatically
    for (int i = 0; i < 200 && !comm.is_connected(); ++i) {
        std::this_thread::sleep_for(20ms);
    }
    EXPECT_TRUE(comm.is_connected()) << "Did not reconnect";

    comm.stop();
    server.stop();
}

// ---------------------------------------------------------------------------
// Multiple reconnect cycles
// ---------------------------------------------------------------------------
TEST_F(ErrorRecoveryTest, MultipleReconnectCycles) {
    static constexpr int PORT_MRC = PORT + 2;
    static constexpr int CYCLES   = 2; // Keep short for CI

    HoldServer server(PORT_MRC);
    ASSERT_TRUE(server.start());

    auto ch = std::make_unique<TcpClientChannel>("127.0.0.1", PORT_MRC, 200);
    SerialComm comm(std::move(ch));
    comm.start();

    for (int c = 0; c < CYCLES; ++c) {
        // Wait for connection
        for (int i = 0; i < 100 && !comm.is_connected(); ++i)
            std::this_thread::sleep_for(20ms);
        EXPECT_TRUE(comm.is_connected()) << "cycle " << c;

        // Drop clients
        server.drop_all_clients();
        for (int i = 0; i < 100 && comm.is_connected(); ++i)
            std::this_thread::sleep_for(20ms);
    }

    comm.stop();
    server.stop();
}

// ---------------------------------------------------------------------------
// Manual reconnect() call
// ---------------------------------------------------------------------------
TEST_F(ErrorRecoveryTest, ManualReconnectTriggersAttempt) {
    static constexpr int PORT_MR = PORT + 3;

    HoldServer server(PORT_MR);
    ASSERT_TRUE(server.start());

    auto ch = std::make_unique<TcpClientChannel>("127.0.0.1", PORT_MR, 200);
    SerialComm comm(std::move(ch));
    comm.start();

    for (int i = 0; i < 100 && !comm.is_connected(); ++i)
        std::this_thread::sleep_for(20ms);
    ASSERT_TRUE(comm.is_connected());

    // Force reconnect manually
    comm.reconnect();

    // Should still be connected (or reconnect quickly)
    for (int i = 0; i < 100 && !comm.is_connected(); ++i)
        std::this_thread::sleep_for(20ms);
    EXPECT_TRUE(comm.is_connected());

    comm.stop();
    server.stop();
}
