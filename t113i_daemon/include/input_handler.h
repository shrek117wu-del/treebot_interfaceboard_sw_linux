#pragma once
/**
 * @file input_handler.h
 * @brief Local input event handler (keyboard, touch screen, GPIO buttons).
 *
 * Reads from Linux evdev devices in a background thread and converts events
 * to EventPayload for forwarding to SerialComm.
 */

#include "protocol.h"

#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
// InputHandler
// ---------------------------------------------------------------------------
class InputHandler {
public:
    InputHandler()  = default;
    ~InputHandler() = default;

    // Add an evdev device to monitor (e.g. /dev/input/event0).
    void add_device(const std::string& path);

    bool start();
    void stop();

    using EventCb = std::function<void(const EventPayload&)>;
    void set_callback(EventCb cb);

private:
    std::vector<std::string>  device_paths_;
    std::vector<int>          fds_;
    EventCb                   callback_;

    std::atomic<bool>         running_{false};
    std::thread               thread_;

    void read_loop();
    void dispatch(const EventPayload& evt);
};
