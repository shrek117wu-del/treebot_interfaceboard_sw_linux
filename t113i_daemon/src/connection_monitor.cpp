/**
 * @file connection_monitor.cpp
 * @brief Connection state machine implementation.
 */

#include "connection_monitor.h"
#include "logger.h"

#include <chrono>

using namespace std::chrono;

ConnectionMonitor::ConnectionMonitor(int heartbeat_timeout_ms,
                                     int max_reconnect_attempts)
    : heartbeat_timeout_ms_(heartbeat_timeout_ms)
    , max_reconnect_attempts_(max_reconnect_attempts)
{}

void ConnectionMonitor::on_heartbeat_received() {
    auto now_ms = duration_cast<milliseconds>(
        steady_clock::now().time_since_epoch()).count();
    last_heartbeat_ms_.store(now_ms);
}

int64_t ConnectionMonitor::ms_since_last_heartbeat() const {
    auto last = last_heartbeat_ms_.load();
    if (last < 0) return -1;
    auto now_ms = duration_cast<milliseconds>(
        steady_clock::now().time_since_epoch()).count();
    return now_ms - last;
}

bool ConnectionMonitor::check_health(bool transport_connected) {
    if (!transport_connected) {
        if (state_.load() == State::CONNECTED) {
            Logger::warn("ConnMonitor", "Connection lost");
        }
        state_.store(State::DISCONNECTED);
        return false;
    }

    // Transport reports connected – check heartbeat freshness
    int64_t elapsed = ms_since_last_heartbeat();
    if (elapsed >= 0 && elapsed > heartbeat_timeout_ms_) {
        if (state_.load() != State::TIMEOUT) {
            Logger::warn("ConnMonitor",
                "Heartbeat timeout (" + std::to_string(elapsed) + " ms)");
        }
        state_.store(State::TIMEOUT);
        return false;
    }

    state_.store(State::CONNECTED);
    return true;
}

void ConnectionMonitor::on_reconnect_attempt() {
    state_.store(State::CONNECTING);
    int attempts = reconnect_attempts_.fetch_add(1) + 1;
    Logger::info("ConnMonitor",
        "Reconnect attempt #" + std::to_string(attempts));
}

void ConnectionMonitor::reset() {
    reconnect_attempts_.store(0);
    last_heartbeat_ms_.store(-1);
    state_.store(State::DISCONNECTED);
}

const char* ConnectionMonitor::state_str() const {
    switch (state_.load()) {
        case State::DISCONNECTED: return "DISCONNECTED";
        case State::CONNECTING:   return "CONNECTING";
        case State::CONNECTED:    return "CONNECTED";
        case State::TIMEOUT:      return "TIMEOUT";
        default:                  return "UNKNOWN";
    }
}
