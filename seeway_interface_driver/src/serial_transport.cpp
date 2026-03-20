#include "seeway_interface_driver/transport.hpp"

#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <cstring>
#include <array>
#include <atomic>
#include <iostream>
#include <mutex>
#include <thread>

namespace seeway_interface_driver {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static speed_t baud_to_speed(uint32_t baud) {
    switch (baud) {
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
        case 460800: return B460800;
        case 921600: return B921600;
        default:     return B0;  // invalid
    }
}

// ---------------------------------------------------------------------------
// SerialTransport
// Communicates with T113i over a tty device (/dev/ttyACM0, /dev/ttyUSB0, …).
// Strategy B: if the device cannot be opened or termios fails, start()
// returns false so that DriverNode::on_activate() returns FAILURE.
// ---------------------------------------------------------------------------
class SerialTransport final : public ITransport {
public:
    SerialTransport(const std::string& device, uint32_t baudrate)
        : device_(device), baudrate_(baudrate) {}

    ~SerialTransport() override { stop(); }

    bool start() override {
        speed_t speed = baud_to_speed(baudrate_);
        if (speed == B0) {
            std::cerr << "[SerialTransport] Unsupported baud rate: "
                      << baudrate_ << "\n";
            return false;
        }

        fd_ = open(device_.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (fd_ < 0) {
            std::cerr << "[SerialTransport] Cannot open " << device_
                      << ": " << strerror(errno) << "\n";
            return false;
        }

        // Switch to blocking mode
        int flags = fcntl(fd_, F_GETFL, 0);
        if (flags < 0 || fcntl(fd_, F_SETFL, flags & ~O_NONBLOCK) < 0) {
            std::cerr << "[SerialTransport] fcntl failed on " << device_ << "\n";
            close(fd_);
            fd_ = -1;
            return false;
        }

        struct termios tty{};
        if (tcgetattr(fd_, &tty) < 0) {
            std::cerr << "[SerialTransport] tcgetattr failed on " << device_
                      << ": " << strerror(errno) << "\n";
            close(fd_);
            fd_ = -1;
            return false;
        }

        // Raw mode, 8N1
        cfmakeraw(&tty);
        cfsetispeed(&tty, speed);
        cfsetospeed(&tty, speed);

        tty.c_cflag |= CS8 | CREAD | CLOCAL;
        tty.c_cflag &= static_cast<tcflag_t>(~(CSTOPB | CRTSCTS));

        // Blocking read with 100 ms inter-character timeout
        tty.c_cc[VMIN]  = 0;
        tty.c_cc[VTIME] = 1;  // 100 ms

        if (tcsetattr(fd_, TCSANOW, &tty) < 0) {
            std::cerr << "[SerialTransport] tcsetattr failed on " << device_
                      << ": " << strerror(errno) << "\n";
            close(fd_);
            fd_ = -1;
            return false;
        }

        tcflush(fd_, TCIOFLUSH);

        running_ = true;
        rx_thread_ = std::thread(&SerialTransport::rx_loop, this);
        std::cout << "[SerialTransport] Opened " << device_
                  << " @ " << baudrate_ << " baud\n";
        return true;
    }

    void stop() override {
        running_ = false;
        if (fd_ >= 0) {
            close(fd_);
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
        ssize_t written = write(fd_, frame, flen);
        return written == static_cast<ssize_t>(flen);
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
            ssize_t n = read(fd_, raw, sizeof(raw));
            if (n < 0) {
                if (errno == EINTR) continue;
                std::cerr << "[SerialTransport] read error: "
                          << strerror(errno) << "\n";
                break;
            }
            if (n == 0) continue;  // timeout (VTIME elapsed), no data
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
std::unique_ptr<ITransport> make_serial_transport(
    const std::string& device, uint32_t baudrate)
{
    return std::make_unique<SerialTransport>(device, baudrate);
}

}  // namespace seeway_interface_driver
