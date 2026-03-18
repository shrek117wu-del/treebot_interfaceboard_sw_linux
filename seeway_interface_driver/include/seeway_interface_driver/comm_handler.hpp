#pragma once

#include "seeway_interface_driver/protocol.hpp"
#include <functional>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <array>
#include <string>
#include <memory>

namespace seeway_interface_driver {

using FrameCallback = std::function<void(MsgId id, uint16_t seq, const uint8_t* payload, uint16_t len)>;

class FrameCodec {
public:
    static size_t encode(MsgId id, uint16_t seq, const uint8_t* payload, uint16_t payload_len, uint8_t* out_buf, size_t out_cap);
    void feed(const uint8_t* data, size_t len, const FrameCallback& cb);

private:
    std::vector<uint8_t> buf_;
    static const size_t BUF_CAP = 2 * PROTO_MAX_FRAME;
    bool try_parse(const FrameCallback& cb);
};

// Simple TCP Server to listen for T113i daemon connection
class TcpServer {
public:
    TcpServer(uint16_t port);
    ~TcpServer();

    bool start();
    void stop();

    bool send(MsgId id, const uint8_t* payload, uint16_t len);

    template<typename T>
    bool send_payload(MsgId id, const T& payload) {
        return send(id, reinterpret_cast<const uint8_t*>(&payload), sizeof(T));
    }

    void register_handler(MsgId id, FrameCallback cb);

    bool is_connected() const;

private:
    uint16_t port_;
    int listen_fd_{-1};
    int client_fd_{-1};

    std::atomic<bool> running_{false};
    std::thread accept_thread_;
    std::thread rx_thread_;
    std::mutex tx_mutex_;

    FrameCodec codec_;
    std::array<FrameCallback, 256> handlers_{};
    uint16_t seq_counter_{0};

    void accept_loop();
    void rx_loop();
};

} // namespace seeway_interface_driver
