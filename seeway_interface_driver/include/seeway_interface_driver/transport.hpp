#pragma once

#include "seeway_interface_driver/comm_handler.hpp"
#include <memory>
#include <string>

namespace seeway_interface_driver {

// ---------------------------------------------------------------------------
// ITransport – abstract transport interface
//
// All transports share the same wire protocol (FrameCodec / protocol.hpp).
// The driver node creates one transport instance in on_configure() and
// interacts with it exclusively through this interface.
// ---------------------------------------------------------------------------
class ITransport {
public:
    virtual ~ITransport() = default;

    // Open the underlying connection / socket / device.
    // Returns false if the transport cannot be started (e.g. serial device
    // missing).  on_activate() must return FAILURE when this returns false.
    virtual bool start() = 0;

    // Gracefully close the connection and join all background threads.
    virtual void stop() = 0;

    // True when a peer is currently connected (TCP) or device is open
    // (serial).
    virtual bool is_connected() const = 0;

    // Encode and send one protocol frame.
    virtual bool send(MsgId id, const uint8_t* payload, uint16_t len) = 0;

    // Convenience wrapper – serialises any packed payload struct.
    template<typename T>
    bool send_payload(MsgId id, const T& payload) {
        return send(id, reinterpret_cast<const uint8_t*>(&payload),
                    static_cast<uint16_t>(sizeof(T)));
    }

    // Register a callback for incoming frames with the given message ID.
    virtual void register_handler(MsgId id, FrameCallback cb) = 0;
};

// ---------------------------------------------------------------------------
// Factory functions
// ---------------------------------------------------------------------------

// TCP server: Jetson listens, T113i connects.
// bind_addr – interface to bind (e.g. "0.0.0.0")
// port      – TCP port
std::unique_ptr<ITransport> make_tcp_server_transport(
    const std::string& bind_addr, uint16_t port);

// TCP client: Jetson connects to T113i acting as server.
// host         – T113i IP address
// port         – TCP port
// reconnect_ms – milliseconds between reconnect attempts
std::unique_ptr<ITransport> make_tcp_client_transport(
    const std::string& host, uint16_t port, uint32_t reconnect_ms);

// Serial / USB-CDC transport.
// device   – tty device path (e.g. "/dev/ttyACM0")
// baudrate – baud rate (e.g. 115200)
// The factory always returns a valid unique_ptr; the device is opened in
// start().  If the device is missing or termios configuration fails, start()
// returns false so that DriverNode::on_activate() returns FAILURE.
std::unique_ptr<ITransport> make_serial_transport(
    const std::string& device, uint32_t baudrate);

}  // namespace seeway_interface_driver
