/**
 * @file serial_comm.cpp
 * @brief IChannel implementations (UART, TCP client) and SerialComm facade.
 */

#include "serial_comm.h"
#include "logger.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <termios.h>
#include <unistd.h>

// ===========================================================================
// FrameCodec
// ===========================================================================

size_t FrameCodec::encode(MsgId id, uint16_t seq,
                          const uint8_t* payload, uint16_t payload_len,
                          uint8_t* out_buf, size_t out_cap)
{
    // Frame: [AA][55][id:1][seq:2LE][len:2LE][reserved:2][payload...][crc:2LE]
    const size_t frame_len = PROTO_HEADER_TOT + payload_len + PROTO_CRC_SZ;
    if (frame_len > out_cap) return 0;

    size_t i = 0;
    out_buf[i++] = PROTO_HEADER_0;
    out_buf[i++] = PROTO_HEADER_1;
    out_buf[i++] = id;
    out_buf[i++] = static_cast<uint8_t>(seq & 0xFF);
    out_buf[i++] = static_cast<uint8_t>((seq >> 8) & 0xFF);
    out_buf[i++] = static_cast<uint8_t>(payload_len & 0xFF);
    out_buf[i++] = static_cast<uint8_t>((payload_len >> 8) & 0xFF);
    out_buf[i++] = 0x00; // reserved
    out_buf[i++] = 0x00;

    for (uint16_t j = 0; j < payload_len; ++j)
        out_buf[i++] = payload[j];

    // CRC covers everything from the first header byte up to (not including) CRC
    uint16_t crc = crc16_ccitt(out_buf, i);
    out_buf[i++] = static_cast<uint8_t>(crc & 0xFF);
    out_buf[i++] = static_cast<uint8_t>((crc >> 8) & 0xFF);

    return i;
}

bool FrameCodec::try_parse(const FrameCallback& cb) {
    // Need at least a full header
    if (buf_.size() < PROTO_HEADER_TOT + PROTO_CRC_SZ) return false;

    // Scan for sync bytes
    size_t sync = 0;
    while (sync + 1 < buf_.size()) {
        if (buf_[sync] == PROTO_HEADER_0 && buf_[sync + 1] == PROTO_HEADER_1)
            break;
        ++sync;
    }
    if (sync > 0) {
        buf_.erase(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(sync));
    }

    if (buf_.size() < PROTO_HEADER_TOT + PROTO_CRC_SZ) return false;

    uint16_t payload_len = static_cast<uint16_t>(buf_[5]) |
                           static_cast<uint16_t>(buf_[6]) << 8;
    if (payload_len > PROTO_MAX_PAYLOAD) {
        buf_.erase(buf_.begin(), buf_.begin() + 2); // skip bad sync, retry
        return true;
    }

    size_t total = PROTO_HEADER_TOT + payload_len + PROTO_CRC_SZ;
    if (buf_.size() < total) return false;

    // Validate CRC
    uint16_t expected_crc = crc16_ccitt(buf_.data(), PROTO_HEADER_TOT + payload_len);
    uint16_t actual_crc   = static_cast<uint16_t>(buf_[PROTO_HEADER_TOT + payload_len]) |
                            static_cast<uint16_t>(buf_[PROTO_HEADER_TOT + payload_len + 1]) << 8;

    if (expected_crc != actual_crc) {
        Logger::warn("FrameCodec", "CRC mismatch, discarding frame");
        buf_.erase(buf_.begin(), buf_.begin() + 2);
        return true;
    }

    MsgId    id  = buf_[2];
    uint16_t seq = static_cast<uint16_t>(buf_[3]) |
                   static_cast<uint16_t>(buf_[4]) << 8;

    if (cb) {
        cb(id, seq, buf_.data() + PROTO_HEADER_TOT, payload_len);
    }

    buf_.erase(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(total));
    return true;
}

void FrameCodec::feed(const uint8_t* data, size_t len, const FrameCallback& cb) {
    // Guard against runaway buffer growth
    if (buf_.size() + len > BUF_CAP) {
        Logger::warn("FrameCodec", "RX buffer overflow, clearing");
        buf_.clear();
    }
    buf_.insert(buf_.end(), data, data + len);
    while (try_parse(cb)) {}
}

// ===========================================================================
// UartChannel
// ===========================================================================

static int baud_to_speed(int baud) {
    switch (baud) {
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
        case 460800: return B460800;
        case 921600: return B921600;
        default:     return B115200;
    }
}

UartChannel::UartChannel(const std::string& device, int baud_rate)
    : device_(device), baud_rate_(baud_rate) {}

UartChannel::~UartChannel() { stop(); }

bool UartChannel::start() {
    fd_ = open(device_.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd_ < 0) {
        Logger::error("UartChannel",
            "Cannot open " + device_ + ": " + strerror(errno));
        return false;
    }

    struct termios tty{};
    if (tcgetattr(fd_, &tty) != 0) {
        Logger::error("UartChannel",
            std::string("tcgetattr failed: ") + strerror(errno));
        close(fd_); fd_ = -1;
        return false;
    }

    speed_t spd = static_cast<speed_t>(baud_to_speed(baud_rate_));
    cfsetispeed(&tty, spd);
    cfsetospeed(&tty, spd);
    cfmakeraw(&tty);
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 1; // 100 ms

    if (tcsetattr(fd_, TCSANOW, &tty) != 0) {
        Logger::error("UartChannel",
            std::string("tcsetattr failed: ") + strerror(errno));
        close(fd_); fd_ = -1;
        return false;
    }

    // Switch to blocking for RX thread
    int flags = fcntl(fd_, F_GETFL, 0);
    fcntl(fd_, F_SETFL, flags & ~O_NONBLOCK);

    running_ = true;
    rx_thread_ = std::thread(&UartChannel::rx_loop, this);
    Logger::info("UartChannel",
        "Opened " + device_ + " @ " + std::to_string(baud_rate_) + " baud");
    return true;
}

void UartChannel::stop() {
    running_ = false;
    if (fd_ >= 0) {
        int tmp = fd_; fd_ = -1;
        close(tmp);
    }
    if (rx_thread_.joinable()) rx_thread_.join();
}

bool UartChannel::is_connected() const { return fd_ >= 0; }

bool UartChannel::write_raw(const uint8_t* data, size_t len) {
    std::lock_guard<std::mutex> lk(tx_mutex_);
    if (fd_ < 0) return false;
    size_t written = 0;
    while (written < len) {
        ssize_t n = write(fd_, data + written, len - written);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        written += static_cast<size_t>(n);
    }
    return true;
}

void UartChannel::set_rx_callback(FrameCallback cb) {
    rx_cb_ = std::move(cb);
}

void UartChannel::rx_loop() {
    uint8_t raw[256];
    while (running_) {
        if (fd_ < 0) break;
        ssize_t n = read(fd_, raw, sizeof(raw));
        if (n > 0 && rx_cb_) {
            codec_.feed(raw, static_cast<size_t>(n), rx_cb_);
        } else if (n == 0) {
            Logger::warn("UartChannel", "Device closed (EOF)");
            break;
        }
    }
}

// ===========================================================================
// TcpClientChannel
// ===========================================================================

TcpClientChannel::TcpClientChannel(const std::string& host, int port,
                                   int reconnect_ms)
    : host_(host), port_(port), reconnect_ms_(reconnect_ms) {}

TcpClientChannel::~TcpClientChannel() { stop(); }

bool TcpClientChannel::start() {
    running_ = true;
    io_thread_ = std::thread(&TcpClientChannel::io_loop, this);
    return true; // connection is established asynchronously
}

void TcpClientChannel::stop() {
    running_ = false;
    close_socket();
    if (io_thread_.joinable()) io_thread_.join();
}

bool TcpClientChannel::is_connected() const { return connected_.load(); }

bool TcpClientChannel::write_raw(const uint8_t* data, size_t len) {
    std::lock_guard<std::mutex> lk(tx_mutex_);
    if (sock_ < 0) return false;
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(sock_, data + sent, len - sent, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            connected_ = false;
            return false;
        }
        sent += static_cast<size_t>(n);
    }
    return true;
}

void TcpClientChannel::set_rx_callback(FrameCallback cb) {
    rx_cb_ = std::move(cb);
}

void TcpClientChannel::reconnect() {
    close_socket();
}

bool TcpClientChannel::try_connect() {
    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    std::string port_str = std::to_string(port_);
    if (getaddrinfo(host_.c_str(), port_str.c_str(), &hints, &res) != 0) {
        return false;
    }

    int s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s < 0) { freeaddrinfo(res); return false; }

    // Set a connect timeout via non-blocking socket
    int flags = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, flags | O_NONBLOCK);

    connect(s, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);

    fd_set wfds;
    FD_ZERO(&wfds);
    FD_SET(s, &wfds);
    struct timeval tv{5, 0}; // 5 s timeout
    int rc = select(s + 1, nullptr, &wfds, nullptr, &tv);
    if (rc <= 0) { close(s); return false; }

    int err = 0;
    socklen_t errlen = sizeof(err);
    getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &errlen);
    if (err != 0) { close(s); return false; }

    // Restore blocking
    fcntl(s, F_SETFL, flags & ~O_NONBLOCK);

    {
        std::lock_guard<std::mutex> lk(tx_mutex_);
        sock_     = s;
        connected_ = true;
    }
    Logger::info("TcpClient",
        "Connected to " + host_ + ":" + std::to_string(port_));
    return true;
}

void TcpClientChannel::close_socket() {
    std::lock_guard<std::mutex> lk(tx_mutex_);
    if (sock_ >= 0) {
        int tmp = sock_; sock_ = -1;
        shutdown(tmp, SHUT_RDWR);
        close(tmp);
    }
    connected_ = false;
}

void TcpClientChannel::io_loop() {
    while (running_) {
        if (!connected_.load()) {
            if (!try_connect()) {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(reconnect_ms_));
                continue;
            }
        }

        // RX
        uint8_t raw[1024];
        ssize_t n = recv(sock_, raw, sizeof(raw), 0);
        if (n > 0 && rx_cb_) {
            codec_.feed(raw, static_cast<size_t>(n), rx_cb_);
        } else if (n == 0 || (n < 0 && errno != EINTR && errno != EAGAIN)) {
            Logger::warn("TcpClient", "Connection closed by peer");
            close_socket();
        }
    }
}

// ===========================================================================
// SerialComm
// ===========================================================================

SerialComm::SerialComm(std::unique_ptr<IChannel> channel)
    : channel_(std::move(channel))
{}

bool SerialComm::start() {
    if (!channel_) return false;
    channel_->set_rx_callback(
        [this](MsgId id, uint16_t seq, const uint8_t* pay, uint16_t len) {
            auto& h = handlers_[static_cast<uint8_t>(id)];
            if (h) h(id, seq, pay, len);
        });
    return channel_->start();
}

void SerialComm::stop() {
    if (channel_) channel_->stop();
}

bool SerialComm::is_connected() const {
    return channel_ && channel_->is_connected();
}

void SerialComm::register_handler(MsgId id, FrameCallback cb) {
    handlers_[static_cast<uint8_t>(id)] = std::move(cb);
}

bool SerialComm::send_raw(MsgId id, const uint8_t* data, uint16_t len) {
    if (!channel_ || !channel_->is_connected()) return false;
    uint8_t frame[PROTO_MAX_FRAME];
    uint16_t seq = seq_.fetch_add(1);
    size_t flen = FrameCodec::encode(id, seq, data, len, frame, sizeof(frame));
    if (flen == 0) return false;
    return channel_->write_raw(frame, flen);
}

void SerialComm::reconnect() {
    if (channel_) channel_->reconnect();
}

void SerialComm::flush_pending(int timeout_ms) {
    // For this implementation: just wait a short time so TCP buffers flush.
    (void)timeout_ms;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}
