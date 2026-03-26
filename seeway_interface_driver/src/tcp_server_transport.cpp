#include "seeway_interface_driver/transport.hpp"

#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <cstring>
#include <array>
#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>

namespace seeway_interface_driver {

// ---------------------------------------------------------------------------
// TcpServerTransport
// Jetson acts as the TCP server; T113i daemon connects as a client.
// ---------------------------------------------------------------------------
class TcpServerTransport final : public ITransport {
public:
    TcpServerTransport(const std::string& bind_addr, uint16_t port)
        : bind_addr_(bind_addr), port_(port) {}

    ~TcpServerTransport() override { stop(); }

    bool start() override {
        listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd_ < 0) return false;

        int opt = 1;
        setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        if (bind_addr_.empty() || bind_addr_ == "0.0.0.0") {
            addr.sin_addr.s_addr = INADDR_ANY;
        } else {
            if (inet_pton(AF_INET, bind_addr_.c_str(), &addr.sin_addr) <= 0) {
                close(listen_fd_);
                listen_fd_ = -1;
                return false;
            }
        }
        addr.sin_port = htons(port_);

        if (bind(listen_fd_, reinterpret_cast<struct sockaddr*>(&addr),
                 sizeof(addr)) < 0) {
            close(listen_fd_);
            listen_fd_ = -1;
            return false;
        }
        if (listen(listen_fd_, 1) < 0) {
            close(listen_fd_);
            listen_fd_ = -1;
            return false;
        }

        running_ = true;
        accept_thread_ = std::thread(&TcpServerTransport::accept_loop, this);
        std::cout << "[TcpServerTransport] Listening on "
                  << bind_addr_ << ":" << port_ << "\n";
        return true;
    }

    void stop() override {
        running_ = false;
        if (listen_fd_ >= 0) {
            shutdown(listen_fd_, SHUT_RDWR);
            close(listen_fd_);
            listen_fd_ = -1;
        }
        {
            std::lock_guard<std::mutex> lk(tx_mutex_);
            if (client_fd_ >= 0) {
                shutdown(client_fd_, SHUT_RDWR);
                close(client_fd_);
                client_fd_ = -1;
            }
        }
        if (accept_thread_.joinable()) accept_thread_.join();
        if (rx_thread_.joinable()) rx_thread_.join();
    }

    bool is_connected() const override { return client_fd_ >= 0; }

    bool send(MsgId id, uint16_t seq, const uint8_t* payload,
              uint16_t len) override {
        std::lock_guard<std::mutex> lk(tx_mutex_);
        if (client_fd_ < 0) return false;
        uint8_t frame[PROTO_MAX_FRAME];
        size_t flen = FrameCodec::encode(id, seq, payload, len,
                                         frame, sizeof(frame));
        if (flen == 0) return false;
        return ::send(client_fd_, frame, flen, MSG_NOSIGNAL) ==
               static_cast<ssize_t>(flen);
    }

    void register_handler(MsgId id, FrameCallback cb) override {
        handlers_[static_cast<uint8_t>(id)] = std::move(cb);
    }

private:
    std::string bind_addr_;
    uint16_t    port_;
    int         listen_fd_{-1};
    int         client_fd_{-1};

    std::atomic<bool> running_{false};
    std::thread       accept_thread_;
    std::thread       rx_thread_;
    std::mutex        tx_mutex_;

    FrameCodec                        codec_;
    std::array<FrameCallback, 256>    handlers_{};

    void accept_loop() {
        while (running_) {
            struct sockaddr_in peer{};
            socklen_t peer_len = sizeof(peer);
            int fd = accept(listen_fd_,
                            reinterpret_cast<struct sockaddr*>(&peer),
                            &peer_len);
            if (fd >= 0) {
                char peer_ip[INET_ADDRSTRLEN]{};
                inet_ntop(AF_INET, &peer.sin_addr, peer_ip, sizeof(peer_ip));
                std::cout << "[TcpServerTransport] T113i connected from "
                          << peer_ip << "\n";

                int flag = 1;
                setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

                {
                    std::lock_guard<std::mutex> lk(tx_mutex_);
                    if (client_fd_ >= 0) {
                        shutdown(client_fd_, SHUT_RDWR);
                        close(client_fd_);
                    }
                    client_fd_ = fd;
                }

                if (rx_thread_.joinable()) rx_thread_.join();
                rx_thread_ = std::thread(&TcpServerTransport::rx_loop, this);
            } else {
                if (!running_) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }

    void rx_loop() {
        uint8_t raw[1024];
        while (running_ && client_fd_ >= 0) {
            int n = recv(client_fd_, raw, sizeof(raw), 0);
            if (n <= 0) {
                std::cout << "[TcpServerTransport] T113i disconnected\n";
                std::lock_guard<std::mutex> lk(tx_mutex_);
                close(client_fd_);
                client_fd_ = -1;
                break;
            }
            codec_.feed(raw, static_cast<size_t>(n),
                        [this](MsgId id, uint16_t seq,
                               const uint8_t* pay, uint16_t len) {
                            auto& h = handlers_[static_cast<uint8_t>(id)];
                            if (h) h(id, seq, pay, len);
                        });
        }
    }
};

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------
std::unique_ptr<ITransport> make_tcp_server_transport(
    const std::string& bind_addr, uint16_t port)
{
    return std::make_unique<TcpServerTransport>(bind_addr, port);
}

}  // namespace seeway_interface_driver
