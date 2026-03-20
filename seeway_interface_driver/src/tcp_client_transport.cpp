#include "seeway_interface_driver/transport.hpp"

#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <cstring>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>

namespace seeway_interface_driver {

// ---------------------------------------------------------------------------
// TcpClientTransport
// Jetson acts as TCP client; T113i acts as server.  A background reconnect
// loop keeps trying until stop() is called.
// ---------------------------------------------------------------------------
class TcpClientTransport final : public ITransport {
public:
    TcpClientTransport(const std::string& host, uint16_t port,
                       uint32_t reconnect_ms)
        : host_(host), port_(port), reconnect_ms_(reconnect_ms) {}

    ~TcpClientTransport() override { stop(); }

    bool start() override {
        running_ = true;
        connect_thread_ = std::thread(&TcpClientTransport::connect_loop, this);
        return true;
    }

    void stop() override {
        running_ = false;
        {
            std::lock_guard<std::mutex> lk(tx_mutex_);
            if (sock_fd_ >= 0) {
                shutdown(sock_fd_, SHUT_RDWR);
                close(sock_fd_);
                sock_fd_ = -1;
            }
        }
        cv_.notify_all();
        if (connect_thread_.joinable()) connect_thread_.join();
        if (rx_thread_.joinable()) rx_thread_.join();
    }

    bool is_connected() const override { return sock_fd_ >= 0; }

    bool send(MsgId id, const uint8_t* payload, uint16_t len) override {
        std::lock_guard<std::mutex> lk(tx_mutex_);
        if (sock_fd_ < 0) return false;
        uint8_t frame[PROTO_MAX_FRAME];
        size_t flen = FrameCodec::encode(id, seq_counter_++, payload, len,
                                         frame, sizeof(frame));
        if (flen == 0) return false;
        return ::send(sock_fd_, frame, flen, MSG_NOSIGNAL) ==
               static_cast<ssize_t>(flen);
    }

    void register_handler(MsgId id, FrameCallback cb) override {
        handlers_[static_cast<uint8_t>(id)] = std::move(cb);
    }

private:
    std::string host_;
    uint16_t    port_;
    uint32_t    reconnect_ms_;
    int         sock_fd_{-1};

    std::atomic<bool> running_{false};
    std::thread       connect_thread_;
    std::thread       rx_thread_;
    std::mutex        tx_mutex_;
    std::mutex        cv_mutex_;
    std::condition_variable cv_;

    FrameCodec                     codec_;
    std::array<FrameCallback, 256> handlers_{};
    uint16_t                       seq_counter_{0};

    void connect_loop() {
        while (running_) {
            int fd = socket(AF_INET, SOCK_STREAM, 0);
            if (fd < 0) {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(reconnect_ms_));
                continue;
            }

            struct sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port   = htons(port_);
            if (inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) <= 0) {
                std::cerr << "[TcpClientTransport] Invalid host: " << host_ << "\n";
                close(fd);
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(reconnect_ms_));
                continue;
            }

            if (connect(fd, reinterpret_cast<struct sockaddr*>(&addr),
                        sizeof(addr)) < 0) {
                close(fd);
                if (!running_) break;
                std::cout << "[TcpClientTransport] Connect to "
                          << host_ << ":" << port_
                          << " failed, retry in " << reconnect_ms_ << "ms\n";
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(reconnect_ms_));
                continue;
            }

            int flag = 1;
            setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

            std::cout << "[TcpClientTransport] Connected to "
                      << host_ << ":" << port_ << "\n";
            {
                std::lock_guard<std::mutex> lk(tx_mutex_);
                sock_fd_ = fd;
            }

            if (rx_thread_.joinable()) rx_thread_.join();
            rx_thread_ = std::thread(&TcpClientTransport::rx_loop, this);
            // Wait until rx_loop signals disconnect (sock_fd_ < 0) or stop().
            {
                std::unique_lock<std::mutex> lk(cv_mutex_);
                cv_.wait(lk, [this] { return !running_ || sock_fd_ < 0; });
            }

            if (!running_) break;
            std::cout << "[TcpClientTransport] Disconnected, retry in "
                      << reconnect_ms_ << "ms\n";
            std::this_thread::sleep_for(
                std::chrono::milliseconds(reconnect_ms_));
        }
    }

    void rx_loop() {
        uint8_t raw[1024];
        while (running_ && sock_fd_ >= 0) {
            int n = recv(sock_fd_, raw, sizeof(raw), 0);
            if (n <= 0) {
                std::cout << "[TcpClientTransport] Server closed connection\n";
                break;
            }
            codec_.feed(raw, static_cast<size_t>(n),
                        [this](MsgId id, uint16_t seq,
                               const uint8_t* pay, uint16_t len) {
                            auto& h = handlers_[static_cast<uint8_t>(id)];
                            if (h) h(id, seq, pay, len);
                        });
        }
        // Close socket and signal connect_loop to retry or exit.
        {
            std::lock_guard<std::mutex> lk(tx_mutex_);
            if (sock_fd_ >= 0) {
                close(sock_fd_);
                sock_fd_ = -1;
            }
        }
        cv_.notify_all();
    }
};

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------
std::unique_ptr<ITransport> make_tcp_client_transport(
    const std::string& host, uint16_t port, uint32_t reconnect_ms)
{
    return std::make_unique<TcpClientTransport>(host, port, reconnect_ms);
}

}  // namespace seeway_interface_driver
