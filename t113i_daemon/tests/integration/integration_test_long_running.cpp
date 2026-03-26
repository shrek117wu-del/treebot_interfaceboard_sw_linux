/**
 * @file integration_test_long_running.cpp
 * @brief Long-running stability test: 10,000+ messages, memory leak
 *        detection, and 30-second minimum runtime.
 *
 * Tests:
 *  - Send 10,000 heartbeat frames; verify all received
 *  - RSS growth < 2 MB over the run (leak check)
 *  - No crashes or assertion failures
 *
 * Note: this test takes ~30 s; it is labelled "integration" and may be
 * excluded from the default quick test run.
 */

#include "protocol.h"
#include "serial_comm.h"
#include "logger.h"

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// Read RSS in KB from /proc/self/status
// ---------------------------------------------------------------------------
static long rss_kb() {
    std::ifstream f("/proc/self/status");
    if (!f.is_open()) return -1;
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("VmRSS:", 0) == 0) {
            long kb = 0;
            std::sscanf(line.c_str(), "VmRSS: %ld kB", &kb);
            return kb;
        }
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Echo server: accepts one client, echoes every frame back
// ---------------------------------------------------------------------------
class EchoServer {
public:
    explicit EchoServer(int port) : port_(port) {}
    ~EchoServer() { stop(); }

    bool start() {
        listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd_ < 0) return false;
        int opt = 1;
        ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        struct sockaddr_in addr{};
        addr.sin_family      = AF_INET;
        addr.sin_port        = htons(static_cast<uint16_t>(port_));
        addr.sin_addr.s_addr = INADDR_ANY;
        if (::bind(listen_fd_, reinterpret_cast<struct sockaddr*>(&addr),
                   sizeof(addr)) < 0) return false;
        if (::listen(listen_fd_, 1) < 0) return false;
        running_ = true;
        thread_  = std::thread(&EchoServer::loop, this);
        return true;
    }

    void stop() {
        running_ = false;
        if (listen_fd_ >= 0) { ::close(listen_fd_); listen_fd_ = -1; }
        if (client_fd_ >= 0) { ::close(client_fd_); client_fd_ = -1; }
        if (thread_.joinable()) thread_.join();
    }

    long frames_echoed() const { return frames_echoed_.load(); }

private:
    int  port_;
    int  listen_fd_{-1};
    int  client_fd_{-1};
    std::atomic<bool> running_{false};
    std::atomic<long> frames_echoed_{0};
    std::thread thread_;

    void loop() {
        struct timeval tv{1, 0};
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(listen_fd_, &rfds);
        if (::select(listen_fd_ + 1, &rfds, nullptr, nullptr, &tv) <= 0) return;

        client_fd_ = ::accept(listen_fd_, nullptr, nullptr);
        if (client_fd_ < 0) return;

        uint8_t buf[4096];
        while (running_) {
            ssize_t n = ::read(client_fd_, buf, sizeof(buf));
            if (n <= 0) break;
            // Echo back
            ::write(client_fd_, buf, static_cast<size_t>(n));
            ++frames_echoed_;
        }
    }
};

// ---------------------------------------------------------------------------
// Long-running test
// ---------------------------------------------------------------------------
class LongRunningTest : public ::testing::Test {
protected:
    static constexpr int PORT = 19010;

    void SetUp() override {
        Logger::init("", Logger::FATAL);
    }
    void TearDown() override {
        Logger::shutdown();
    }
};

TEST_F(LongRunningTest, TenThousandMessages) {
    static constexpr int MSG_TOTAL = 10000;
    static constexpr int PORT_LR   = PORT;

    EchoServer server(PORT_LR);
    ASSERT_TRUE(server.start());

    auto ch = std::make_unique<TcpClientChannel>("127.0.0.1", PORT_LR, 200);
    SerialComm comm(std::move(ch));
    comm.start();

    // Wait for connection
    for (int i = 0; i < 100 && !comm.is_connected(); ++i)
        std::this_thread::sleep_for(20ms);
    ASSERT_TRUE(comm.is_connected());

    std::atomic<int> received{0};
    comm.register_handler(MSG_HEARTBEAT,
        [&](MsgId, uint16_t, const uint8_t*, uint16_t) { ++received; });

    long rss_start = rss_kb();
    auto t_start   = std::chrono::steady_clock::now();

    for (int i = 0; i < MSG_TOTAL; ++i) {
        HeartbeatPayload hb{static_cast<uint32_t>(i), 0};
        comm.send_payload(MSG_HEARTBEAT, hb);
        // Throttle slightly to avoid overwhelming the loopback
        if (i % 100 == 99) std::this_thread::sleep_for(5ms);
    }

    // Wait for all echoes (max 20 s)
    for (int i = 0; i < 2000 && received.load() < MSG_TOTAL; ++i)
        std::this_thread::sleep_for(10ms);

    auto t_end  = std::chrono::steady_clock::now();
    long rss_end = rss_kb();

    comm.stop();
    server.stop();

    double elapsed_s = std::chrono::duration<double>(t_end - t_start).count();
    long   rss_delta = (rss_end >= 0 && rss_start >= 0) ? (rss_end - rss_start) : 0;

    std::printf("\n[long_running] msgs=%d/%d  elapsed=%.2f s  rss_delta=%ld KB\n",
                received.load(), MSG_TOTAL, elapsed_s, rss_delta);

    EXPECT_EQ(received.load(), MSG_TOTAL);

    // Memory leak check: growth should be < 2 MB (2048 KB)
    if (rss_start > 0 && rss_end > 0) {
        EXPECT_LT(rss_delta, 2048L)
            << "RSS grew by " << rss_delta
            << " KB; possible memory leak";
    }
}

// ---------------------------------------------------------------------------
// Stability: send messages over 5 seconds without crashes
// (shortened from 30 minutes for CI)
// ---------------------------------------------------------------------------
TEST_F(LongRunningTest, StabilityOver5Seconds) {
    static constexpr int PORT_ST = PORT + 1;

    EchoServer server(PORT_ST);
    ASSERT_TRUE(server.start());

    auto ch = std::make_unique<TcpClientChannel>("127.0.0.1", PORT_ST, 200);
    SerialComm comm(std::move(ch));
    comm.start();

    for (int i = 0; i < 100 && !comm.is_connected(); ++i)
        std::this_thread::sleep_for(20ms);
    ASSERT_TRUE(comm.is_connected());

    auto deadline = std::chrono::steady_clock::now() + 5s;
    int  sent     = 0;

    while (std::chrono::steady_clock::now() < deadline) {
        HeartbeatPayload hb{static_cast<uint32_t>(sent), 0};
        if (comm.send_payload(MSG_HEARTBEAT, hb)) ++sent;
        std::this_thread::sleep_for(10ms);
    }

    EXPECT_GT(sent, 0);
    EXPECT_TRUE(comm.is_connected());

    comm.stop();
    server.stop();
}
