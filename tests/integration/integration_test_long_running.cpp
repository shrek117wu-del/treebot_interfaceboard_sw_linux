/**
 * @file integration_test_long_running.cpp
 * @brief Integration test: stability over many message round-trips.
 *
 * Sends 10 000 frames through a TCP loopback server and verifies:
 *  - Zero message loss
 *  - Sequence number consistency
 *  - No memory growth (RSS stays stable)
 */

#include <gtest/gtest.h>

#include "protocol.h"

#include <arpa/inet.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

// ---------------------------------------------------------------------------
// Minimal loopback infrastructure (duplicated to keep tests self-contained)
// ---------------------------------------------------------------------------
static std::vector<uint8_t> build_hb_frame(uint16_t seq) {
    HeartbeatPayload hb{};
    hb.timestamp_ms = seq * 10u;
    hb.role = 0;

    std::vector<uint8_t> f;
    f.reserve(PROTO_HEADER_TOT + sizeof(hb) + PROTO_CRC_SZ);
    f.push_back(PROTO_HEADER_0);
    f.push_back(PROTO_HEADER_1);
    f.push_back(MSG_HEARTBEAT);
    f.push_back(seq & 0xFF);
    f.push_back((seq >> 8) & 0xFF);
    uint16_t pl = sizeof(hb);
    f.push_back(pl & 0xFF);
    f.push_back((pl >> 8) & 0xFF);
    f.push_back(0); f.push_back(0); // reserved
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&hb);
    for (size_t i = 0; i < sizeof(hb); ++i) f.push_back(p[i]);
    uint16_t crc = crc16_ccitt(f.data(), f.size());
    f.push_back(crc & 0xFF);
    f.push_back((crc >> 8) & 0xFF);
    return f;
}

static long rss_kb() {
    FILE* fp = fopen("/proc/self/status", "r");
    if (!fp) return -1;
    long rss = -1; char line[256];
    while (fgets(line, sizeof(line), fp))
        if (sscanf(line, "VmRSS: %ld kB", &rss) == 1) break;
    fclose(fp);
    return rss;
}

// ---------------------------------------------------------------------------
// Fixture: spawns echo server once for all tests
// ---------------------------------------------------------------------------
class LongRunningTest : public ::testing::Test {
protected:
    int server_fd_{-1};
    int client_fd_{-1};
    std::thread server_thread_;
    static constexpr int kPort = 19002;
    static constexpr int kMessages = 10000;

    void SetUp() override {
        server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        int opt = 1;
        setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(kPort);
        bind(server_fd_, (sockaddr*)&addr, sizeof(addr));
        listen(server_fd_, 1);

        server_thread_ = std::thread([this]() {
            sockaddr_in c{}; socklen_t l = sizeof(c);
            int cfd = accept(server_fd_, (sockaddr*)&c, &l);
            if (cfd < 0) return;
            uint8_t buf[4096];
            ssize_t n;
            while ((n = recv(cfd, buf, sizeof(buf), 0)) > 0)
                send(cfd, buf, n, 0);
            close(cfd);
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        client_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in saddr{};
        saddr.sin_family = AF_INET;
        saddr.sin_port = htons(kPort);
        inet_pton(AF_INET, "127.0.0.1", &saddr.sin_addr);
        connect(client_fd_, (sockaddr*)&saddr, sizeof(saddr));
    }

    void TearDown() override {
        close(client_fd_);
        close(server_fd_);
        if (server_thread_.joinable()) server_thread_.join();
    }
};

// ---------------------------------------------------------------------------
// Test: 10 000 messages – zero loss, sequence numbers consistent
// ---------------------------------------------------------------------------
TEST_F(LongRunningTest, TenThousandMessagesZeroLoss) {
    long rss_start = rss_kb();
    int lost = 0;

    for (int seq = 1; seq <= kMessages; ++seq) {
        auto frame = build_hb_frame(static_cast<uint16_t>(seq));
        ssize_t sent = send(client_fd_, frame.data(), frame.size(), 0);
        ASSERT_EQ(sent, static_cast<ssize_t>(frame.size()))
            << "Send failed at seq=" << seq;

        std::vector<uint8_t> resp(frame.size());
        size_t received = 0;
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (received < frame.size() &&
               std::chrono::steady_clock::now() < deadline) {
            ssize_t n = recv(client_fd_, resp.data() + received,
                             frame.size() - received, MSG_DONTWAIT);
            if (n > 0) received += n;
            else std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        if (received != frame.size() || resp != frame) {
            ++lost;
        }
    }

    long rss_end = rss_kb();
    long rss_growth = rss_end - rss_start;

    printf("Messages sent: %d, lost: %d, RSS growth: %ld kB\n",
           kMessages, lost, rss_growth);

    EXPECT_EQ(lost, 0) << "Message loss detected";
    // Memory growth should be < 5 MB (5120 kB) for this test
    EXPECT_LT(rss_growth, 5120L) << "Excessive memory growth";
}
