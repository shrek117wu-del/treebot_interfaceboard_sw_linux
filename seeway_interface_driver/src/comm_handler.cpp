#include "seeway_interface_driver/comm_handler.hpp"
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <cstring>
#include <iostream>

namespace seeway_interface_driver {

size_t FrameCodec::encode(MsgId id, uint16_t seq, const uint8_t* payload, uint16_t payload_len, uint8_t* out, size_t cap) {
    size_t frame_len = PROTO_HEADER_TOT + payload_len + PROTO_CRC_SZ;
    if (frame_len > cap) return 0;
    size_t i = 0;
    out[i++] = PROTO_HEADER_0;
    out[i++] = PROTO_HEADER_1;
    out[i++] = (uint8_t)id;
    out[i++] = (uint8_t)(seq & 0xFF);
    out[i++] = (uint8_t)(seq >> 8);
    out[i++] = (uint8_t)(payload_len & 0xFF);
    out[i++] = (uint8_t)(payload_len >> 8);
    out[i++] = 0x00;
    out[i++] = 0x00;
    if (payload_len > 0 && payload) {
        memcpy(out + i, payload, payload_len);
        i += payload_len;
    }
    uint16_t crc = crc16_ccitt(out + 2, PROTO_HEADER_TOT - 2 + payload_len);
    out[i++] = (uint8_t)(crc & 0xFF);
    out[i++] = (uint8_t)(crc >> 8);
    return i;
}

void FrameCodec::feed(const uint8_t* data, size_t len, const FrameCallback& cb) {
    buf_.insert(buf_.end(), data, data + len);
    while (try_parse(cb)) {}
    if (buf_.size() > BUF_CAP) {
        buf_.erase(buf_.begin(), buf_.begin() + buf_.size() - BUF_CAP);
    }
}

bool FrameCodec::try_parse(const FrameCallback& cb) {
    while (buf_.size() >= 2 && !(buf_[0] == PROTO_HEADER_0 && buf_[1] == PROTO_HEADER_1)) {
        buf_.erase(buf_.begin());
    }
    if (buf_.size() < (size_t)PROTO_HEADER_TOT) return false;

    uint8_t  msg_id  = buf_[2];
    uint16_t seq     = (uint16_t)buf_[3] | ((uint16_t)buf_[4] << 8);
    uint16_t pay_len = (uint16_t)buf_[5] | ((uint16_t)buf_[6] << 8);

    if (pay_len > PROTO_MAX_PAYLOAD) {
        buf_.erase(buf_.begin());
        return true;
    }
    size_t total = PROTO_HEADER_TOT + pay_len + PROTO_CRC_SZ;
    if (buf_.size() < total) return false;

    uint16_t crc_calc = crc16_ccitt(buf_.data() + 2, PROTO_HEADER_TOT - 2 + pay_len);
    uint16_t crc_recv = (uint16_t)buf_[total - 2] | ((uint16_t)buf_[total - 1] << 8);
    if (crc_calc != crc_recv) {
        buf_.erase(buf_.begin());
        return true;
    }

    cb((MsgId)msg_id, seq, buf_.data() + PROTO_HEADER_TOT, pay_len);
    buf_.erase(buf_.begin(), buf_.begin() + total);
    return true;
}

TcpServer::TcpServer(uint16_t port) : port_(port) {}

TcpServer::~TcpServer() { stop(); }

bool TcpServer::start() {
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) return false;

    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (bind(listen_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
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
    accept_thread_ = std::thread(&TcpServer::accept_loop, this);
    return true;
}

void TcpServer::stop() {
    running_ = false;
    if (listen_fd_ >= 0) {
        shutdown(listen_fd_, SHUT_RDWR);
        close(listen_fd_);
        listen_fd_ = -1;
    }
    if (client_fd_ >= 0) {
        shutdown(client_fd_, SHUT_RDWR);
        close(client_fd_);
        client_fd_ = -1;
    }
    if (accept_thread_.joinable()) accept_thread_.join();
    if (rx_thread_.joinable()) rx_thread_.join();
}

void TcpServer::accept_loop() {
    while (running_) {
        struct sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int fd = accept(listen_fd_, (struct sockaddr*)&client_addr, &client_len);
        
        if (fd >= 0) {
            std::cout << "[TcpServer] T113i client connected\n";
            std::lock_guard<std::mutex> lk(tx_mutex_);
            if (client_fd_ >= 0) {
                close(client_fd_); // Drop old connection
            }
            int flag = 1;
            setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
            client_fd_ = fd;

            if (rx_thread_.joinable()) {
                rx_thread_.join();
            }
            rx_thread_ = std::thread(&TcpServer::rx_loop, this);
        } else {
            if (!running_) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

void TcpServer::rx_loop() {
    uint8_t raw[1024];
    while (running_ && client_fd_ >= 0) {
        int n = recv(client_fd_, raw, sizeof(raw), 0);
        if (n <= 0) {
            std::cout << "[TcpServer] T113i client disconnected\n";
            std::lock_guard<std::mutex> lk(tx_mutex_);
            close(client_fd_);
            client_fd_ = -1;
            break;
        }
        codec_.feed(raw, (size_t)n, [this](MsgId id, uint16_t seq, const uint8_t* pay, uint16_t len) {
            auto& h = handlers_[(uint8_t)id];
            if (h) h(id, seq, pay, len);
        });
    }
}

bool TcpServer::send(MsgId id, const uint8_t* payload, uint16_t len) {
    std::lock_guard<std::mutex> lk(tx_mutex_);
    if (client_fd_ < 0) return false;

    uint8_t frame[PROTO_MAX_FRAME];
    uint16_t my_seq = seq_counter_++;
    size_t frame_len = FrameCodec::encode(id, my_seq, payload, len, frame, sizeof(frame));
    if (frame_len == 0) return false;

    return ::send(client_fd_, frame, frame_len, MSG_NOSIGNAL) == (int)frame_len;
}

void TcpServer::register_handler(MsgId id, FrameCallback cb) {
    handlers_[(uint8_t)id] = std::move(cb);
}

bool TcpServer::is_connected() const {
    return client_fd_ >= 0;
}

} // namespace seeway_interface_driver
