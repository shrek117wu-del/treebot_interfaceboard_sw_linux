#include "seeway_interface_driver/transport.hpp"

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <array>
#include <atomic>
#include <iostream>
#include <mutex>
#include <thread>

namespace seeway_interface_driver {

static speed_t to_baud(uint32_t baud) {
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

// ---------------------------------------------------------------------------
// SerialTransport
// USB-CDC or UART serial transport.  Runs a background RX thread; send() is
// called from the service-callback threads and is protected by tx_mutex_.
// ---------------------------------------------------------------------------
class SerialTransport final : public ITransport {
public:
    SerialTransport(const std::string& device, uint32_t baudrate)
        : device_(device), baudrate_(baudrate) {}

    ~SerialTransport() override { stop(); }

    bool start() override {
        fd_ = open(device_.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (fd_ < 0) {
            std::cerr << "[SerialTransport] Cannot open " << device_
                      << ": " << strerror(errno) << "\n";
            return false;
        }

        struct termios tty{};
        if (tcgetattr(fd_, &tty) != 0) {
            std::cerr << "[SerialTransport] tcgetattr failed: "
                      << strerror(errno) << "\n";
            close(fd_);
            fd_ = -1;
            return false;
        }

        speed_t spd = to_baud(baudrate_);
        cfsetispeed(&tty, spd);
        cfsetospeed(&tty, spd);
        cfmakeraw(&tty);
        tty.c_cc[VMIN]  = 0;
        tty.c_cc[VTIME] = 1; // 100 ms read timeout

        if (tcsetattr(fd_, TCSANOW, &tty) != 0) {
            std::cerr << "[SerialTransport] tcsetattr failed: "
                      << strerror(errno) << "\n";
            close(fd_);
            fd_ = -1;
            return false;
        }

        // Switch to blocking mode for the RX thread
        int flags = fcntl(fd_, F_GETFL, 0);
        fcntl(fd_, F_SETFL, flags & ~O_NONBLOCK);

        running_ = true;
        rx_thread_ = std::thread(&SerialTransport::rx_loop, this);
        std::cout << "[SerialTransport] Opened " << device_
                  << " at " << baudrate_ << " baud\n";
        return true;
    }

    void stop() override {
        running_ = false;
        if (fd_ >= 0) {
            // Unblock any pending read by closing the fd
            int tmp = fd_;
            fd_ = -1;
            close(tmp);
        }
        if (rx_thread_.joinable()) rx_thread_.join();
    }

    bool is_connected() const override { return fd_ >= 0; }

    bool send(MsgId id, uint16_t seq, const uint8_t* payload,
              uint16_t len) override {
        std::lock_guard<std::mutex> lk(tx_mutex_);
        if (fd_ < 0) return false;
        uint8_t frame[PROTO_MAX_FRAME];
        size_t flen = FrameCodec::encode(id, seq, payload, len,
                                         frame, sizeof(frame));
        if (flen == 0) return false;

        size_t written = 0;
        while (written < flen) {
            ssize_t n = write(fd_, frame + written, flen - written);
            if (n < 0) {
                if (errno == EINTR) continue;
                return false;
            }
            written += static_cast<size_t>(n);
        }
        return true;
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

    void rx_loop() {
        uint8_t raw[256];
        while (running_) {
            if (fd_ < 0) break;
            ssize_t n = read(fd_, raw, sizeof(raw));
            if (n > 0) {
                codec_.feed(raw, static_cast<size_t>(n),
                            [this](MsgId id, uint16_t seq,
                                   const uint8_t* pay, uint16_t len) {
                                auto& h = handlers_[static_cast<uint8_t>(id)];
                                if (h) h(id, seq, pay, len);
                            });
            } else if (n == 0) {
                // EOF – device (USB-CDC) was unplugged.
                // Note: the transport must be stopped and restarted (via
                // on_deactivate → on_activate) to recover after reconnection;
                // automatic reconnection is not implemented here.
                std::cout << "[SerialTransport] Device closed (EOF)\n";
                break;
            }
            // n < 0: EAGAIN/EINTR – continue
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
