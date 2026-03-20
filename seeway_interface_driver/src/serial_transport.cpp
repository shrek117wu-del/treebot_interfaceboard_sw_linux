#include "seeway_interface_driver/transport.hpp"

#include <errno.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <mutex>
#include <thread>

namespace seeway_interface_driver {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static speed_t baud_to_speed(uint32_t baud)
{
    switch (baud) {
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
        case 460800: return B460800;
        case 921600: return B921600;
        default:     return B0;
    }
}

static bool write_all(int fd, const uint8_t* buf, size_t len)
{
    size_t off = 0;
    while (off < len) {
        ssize_t n = ::write(fd, buf + off, len - off);
        if (n > 0) {
            off += static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        return false;   // n == 0 or unrecoverable error
    }
    return true;
}

// ---------------------------------------------------------------------------
// SerialTransport
// USB-CDC / UART transport implementing ITransport.
// ---------------------------------------------------------------------------
class SerialTransport final : public ITransport {
public:
    SerialTransport(const std::string& device, uint32_t baudrate)
        : device_(device), baudrate_(baudrate) {}

    ~SerialTransport() override { stop(); }

    bool start() override {
        speed_t speed = baud_to_speed(baudrate_);
        if (speed == B0) {
            std::cerr << "[SerialTransport] Unsupported baudrate: "
                      << baudrate_ << "\n";
            return false;
        }

        int fd = ::open(device_.c_str(), O_RDWR | O_NOCTTY | O_CLOEXEC);
        if (fd < 0) {
            std::cerr << "[SerialTransport] open(" << device_
                      << ") failed: " << std::strerror(errno) << "\n";
            return false;
        }

        struct termios tio{};
        if (tcgetattr(fd, &tio) < 0) {
            std::cerr << "[SerialTransport] tcgetattr failed: "
                      << std::strerror(errno) << "\n";
            ::close(fd);
            return false;
        }

        // Raw mode: 8N1, no flow control
        cfmakeraw(&tio);
        cfsetispeed(&tio, speed);
        cfsetospeed(&tio, speed);
        tio.c_cflag |= (CLOCAL | CREAD);
        tio.c_cflag &= ~CRTSCTS;

        if (tcsetattr(fd, TCSANOW, &tio) < 0) {
            std::cerr << "[SerialTransport] tcsetattr failed: "
                      << std::strerror(errno) << "\n";
            ::close(fd);
            return false;
        }

        fd_ = fd;
        running_ = true;
        rx_thread_ = std::thread(&SerialTransport::rx_loop, this);

        std::cout << "[SerialTransport] Opened " << device_
                  << " at " << baudrate_ << " baud\n";
        return true;
    }

    void stop() override {
        running_ = false;
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
        if (rx_thread_.joinable()) rx_thread_.join();
    }

    bool is_connected() const override { return fd_ >= 0; }

    bool send(MsgId id, const uint8_t* payload, uint16_t len) override {
        std::lock_guard<std::mutex> lk(tx_mutex_);
        if (fd_ < 0) return false;

        uint8_t frame[PROTO_MAX_FRAME];
        size_t flen = FrameCodec::encode(id, seq_counter_++, payload, len,
                                         frame, sizeof(frame));
        if (flen == 0) return false;

        return write_all(fd_, frame, flen);
    }

    void register_handler(MsgId id, FrameCallback cb) override {
        handlers_[static_cast<uint8_t>(id)] = std::move(cb);
    }

private:
    std::string device_;
    uint32_t    baudrate_;
    int         fd_{-1};

    std::atomic<bool> running_{false};
    std::thread       rx_thread_;
    std::mutex        tx_mutex_;

    FrameCodec                     codec_;
    std::array<FrameCallback, 256> handlers_{};
    uint16_t                       seq_counter_{0};

    void rx_loop() {
        uint8_t raw[256];
        while (running_ && fd_ >= 0) {
            ssize_t n = ::read(fd_, raw, sizeof(raw));
            if (n <= 0) {
                if (n < 0 && errno == EINTR) continue;
                break;
            }
            codec_.feed(raw, static_cast<size_t>(n),
                        [this](MsgId id, uint16_t seq,
                               const uint8_t* pay, uint16_t plen) {
                            auto& h = handlers_[static_cast<uint8_t>(id)];
                            if (h) h(id, seq, pay, plen);
                        });
        }
    }
};

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------
std::unique_ptr<ITransport> make_serial_transport(
    const std::string& device, uint32_t baudrate)
{
    return std::make_unique<SerialTransport>(device, baudrate);
}

}  // namespace seeway_interface_driver