/**
 * @file input_handler.cpp
 * @brief InputHandler implementation – reads Linux evdev devices.
 */

#include "input_handler.h"
#include "logger.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/select.h>
#include <unistd.h>

// ---------------------------------------------------------------------------
// InputHandler public API
// ---------------------------------------------------------------------------

void InputHandler::add_device(const std::string& path) {
    device_paths_.push_back(path);
}

void InputHandler::set_callback(EventCb cb) {
    callback_ = std::move(cb);
}

bool InputHandler::start() {
    for (const auto& path : device_paths_) {
        int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
        if (fd < 0) {
            Logger::warn("InputHandler",
                "Cannot open device " + path + ": " + strerror(errno));
        } else {
            fds_.push_back(fd);
            Logger::info("InputHandler", "Opened evdev device: " + path);
        }
    }
    // Start thread even with no fds – it will just idle
    running_ = true;
    thread_  = std::thread(&InputHandler::read_loop, this);
    return true;
}

void InputHandler::stop() {
    running_ = false;
    for (int fd : fds_) close(fd);
    fds_.clear();
    if (thread_.joinable()) thread_.join();
}

void InputHandler::dispatch(const EventPayload& evt) {
    if (callback_) callback_(evt);
}

void InputHandler::read_loop() {
    while (running_) {
        if (fds_.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            continue;
        }

        fd_set rfds;
        FD_ZERO(&rfds);
        int maxfd = -1;
        for (int fd : fds_) {
            FD_SET(fd, &rfds);
            if (fd > maxfd) maxfd = fd;
        }

        struct timeval tv{0, 200000}; // 200 ms
        int rc = select(maxfd + 1, &rfds, nullptr, nullptr, &tv);
        if (rc <= 0) continue;

        for (int fd : fds_) {
            if (!FD_ISSET(fd, &rfds)) continue;

            struct input_event ie{};
            ssize_t n = read(fd, &ie, sizeof(ie));
            if (n < static_cast<ssize_t>(sizeof(ie))) continue;

            EventPayload evt{};
            evt.timestamp_ms = static_cast<uint32_t>(
                (uint64_t)ie.time.tv_sec * 1000 + ie.time.tv_usec / 1000);
            evt.code  = ie.code;
            evt.value = ie.value;

            if (ie.type == EV_KEY) {
                evt.type = (ie.value > 0) ? EVT_KEY_PRESS : EVT_KEY_RELEASE;
                dispatch(evt);
            } else if (ie.type == EV_ABS && ie.code == ABS_MT_TRACKING_ID) {
                evt.type = (ie.value >= 0) ? EVT_TOUCH_DOWN : EVT_TOUCH_UP;
                dispatch(evt);
            }
        }
    }
}
