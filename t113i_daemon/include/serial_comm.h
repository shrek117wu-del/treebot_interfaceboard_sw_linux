#pragma once
/**
 * @file serial_comm.h
 * @brief Communication layer for the T113i daemon.
 *
 * Defines:
 *  - IChannel: abstract I/O channel (UART or TCP)
 *  - UartChannel: UART/serial implementation
 *  - TcpClientChannel: TCP client implementation (connects to Jetson server)
 *  - SerialComm: frame codec + channel wrapper with handler dispatch
 */

#include "protocol.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using FrameCallback = std::function<void(MsgId, uint16_t, const uint8_t*, uint16_t)>;

// ---------------------------------------------------------------------------
// FrameCodec – stateful reassembly buffer
// ---------------------------------------------------------------------------
class FrameCodec {
public:
    // Encode one frame.  Returns bytes written, or 0 on error.
    static size_t encode(MsgId id, uint16_t seq,
                         const uint8_t* payload, uint16_t payload_len,
                         uint8_t* out_buf, size_t out_cap);

    // Feed raw bytes; invokes cb for each complete, CRC-valid frame.
    void feed(const uint8_t* data, size_t len, const FrameCallback& cb);

private:
    std::vector<uint8_t> buf_;
    static const size_t  BUF_CAP = 2u * PROTO_MAX_FRAME;
    bool try_parse(const FrameCallback& cb);
};

// ---------------------------------------------------------------------------
// IChannel – abstract transport interface
// ---------------------------------------------------------------------------
class IChannel {
public:
    virtual ~IChannel() = default;
    virtual bool     start()              = 0;
    virtual void     stop()               = 0;
    virtual bool     is_connected() const = 0;
    // Write raw bytes; returns true on success.
    virtual bool     write_raw(const uint8_t* data, size_t len) = 0;
    // Register frame callback (called from internal RX thread).
    virtual void     set_rx_callback(FrameCallback cb) = 0;
    // Optional: trigger reconnect attempt (no-op for channels that do it
    // automatically).
    virtual void     reconnect() {}
};

// ---------------------------------------------------------------------------
// UartChannel – UART / USB-CDC serial channel
// ---------------------------------------------------------------------------
class UartChannel final : public IChannel {
public:
    UartChannel(const std::string& device, int baud_rate);
    ~UartChannel() override;

    bool start()               override;
    void stop()                override;
    bool is_connected() const  override;
    bool write_raw(const uint8_t* data, size_t len) override;
    void set_rx_callback(FrameCallback cb)           override;

private:
    std::string  device_;
    int          baud_rate_;
    int          fd_{-1};

    FrameCallback      rx_cb_;
    FrameCodec         codec_;
    std::atomic<bool>  running_{false};
    std::thread        rx_thread_;
    std::mutex         tx_mutex_;

    void rx_loop();
    static int  open_uart(const std::string& dev, int baud);
};

// ---------------------------------------------------------------------------
// TcpClientChannel – TCP client (T113i connects to Jetson server)
// ---------------------------------------------------------------------------
class TcpClientChannel final : public IChannel {
public:
    TcpClientChannel(const std::string& host, int port,
                     int reconnect_ms = 2000);
    ~TcpClientChannel() override;

    bool start()               override;
    void stop()                override;
    bool is_connected() const  override;
    bool write_raw(const uint8_t* data, size_t len) override;
    void set_rx_callback(FrameCallback cb)           override;
    void reconnect()           override;

private:
    std::string host_;
    int         port_;
    int         reconnect_ms_;
    int         sock_{-1};

    FrameCallback      rx_cb_;
    FrameCodec         codec_;
    std::atomic<bool>  running_{false};
    std::atomic<bool>  connected_{false};
    std::thread        io_thread_;
    std::mutex         tx_mutex_;

    void io_loop();
    bool try_connect();
    void close_socket();
};

// ---------------------------------------------------------------------------
// SerialComm – high-level communication facade
//
// Wraps an IChannel, manages a sequence counter, dispatches incoming frames
// to registered handlers, and provides send_payload<T>().
// ---------------------------------------------------------------------------
class SerialComm {
public:
    explicit SerialComm(std::unique_ptr<IChannel> channel);

    bool start();
    void stop();

    bool is_connected() const;

    // Register a handler for a specific message ID.
    void register_handler(MsgId id, FrameCallback cb);

    // Send a payload struct as a framed message.
    template<typename T>
    bool send_payload(MsgId id, const T& payload) {
        return send_raw(id,
                        reinterpret_cast<const uint8_t*>(&payload),
                        static_cast<uint16_t>(sizeof(T)));
    }

    // Trigger a manual reconnect attempt.
    void reconnect();

    // Flush: wait up to timeout_ms for the send queue to drain (best-effort).
    void flush_pending(int timeout_ms = 5000);

private:
    std::unique_ptr<IChannel>          channel_;
    std::array<FrameCallback, 256>     handlers_{};
    std::atomic<uint16_t>              seq_{0};

    bool send_raw(MsgId id, const uint8_t* data, uint16_t len);
};
