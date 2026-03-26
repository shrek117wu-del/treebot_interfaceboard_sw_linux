#pragma once
/**
 * @file mock_serial_comm.h
 * @brief GMock mock for SerialComm behaviour used in integration tests.
 *
 * Provides a lightweight in-process loopback: frames written via
 * send_payload are decoded and dispatched to registered handlers on the
 * same object, simulating a loopback channel without real network I/O.
 */

#include "protocol.h"
#include "serial_comm.h"

#include <gmock/gmock.h>

#include <array>
#include <atomic>
#include <functional>
#include <mutex>
#include <vector>

// ---------------------------------------------------------------------------
// LoopbackChannel – IChannel implementation that echoes TX bytes back as RX
// ---------------------------------------------------------------------------
class LoopbackChannel final : public IChannel {
public:
    LoopbackChannel() = default;

    bool start() override { connected_ = true; return true; }
    void stop()  override { connected_ = false; }
    bool is_connected() const override { return connected_.load(); }

    void set_rx_callback(FrameCallback cb) override {
        std::lock_guard<std::mutex> lk(mutex_);
        rx_cb_ = std::move(cb);
    }

    bool write_raw(const uint8_t* data, size_t len) override {
        // Feed the written bytes straight back into the decoder
        std::lock_guard<std::mutex> lk(mutex_);
        if (rx_cb_) {
            codec_.feed(data, len, rx_cb_);
        }
        return true;
    }

    void reconnect() override { connected_ = true; }

private:
    std::atomic<bool> connected_{false};
    FrameCallback     rx_cb_;
    FrameCodec        codec_;
    mutable std::mutex mutex_;
};

// ---------------------------------------------------------------------------
// MockSerialComm – wraps a LoopbackChannel for test injection
// ---------------------------------------------------------------------------
class MockSerialComm {
public:
    MockSerialComm() {
        auto ch = std::make_unique<LoopbackChannel>();
        loopback_ptr_ = ch.get();
        comm_         = std::make_unique<SerialComm>(std::move(ch));
    }

    SerialComm& comm() { return *comm_; }

    void start() { comm_->start(); }
    void stop()  { comm_->stop(); }

    bool is_connected() const { return comm_->is_connected(); }

private:
    LoopbackChannel*          loopback_ptr_{nullptr};
    std::unique_ptr<SerialComm> comm_;
};
