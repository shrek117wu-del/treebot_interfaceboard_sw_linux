#pragma once
/**
 * @file connection_monitor.h
 * @brief Connection state machine with heartbeat-based health checks.
 *
 * Addresses P2 issues: connection health monitoring and reconnection strategy.
 * Tracks whether the communication link is alive by monitoring time elapsed
 * since the last received heartbeat.
 */

#include <atomic>
#include <chrono>
#include <string>

class ConnectionMonitor {
public:
    enum class State {
        DISCONNECTED,
        CONNECTING,
        CONNECTED,
        TIMEOUT,
    };

    explicit ConnectionMonitor(int heartbeat_timeout_ms = 5000,
                               int max_reconnect_attempts = 0);

    // Call whenever a heartbeat frame is received from the peer.
    void on_heartbeat_received();

    // Returns the current connection state.
    State state() const;

    // Update state based on is_connected flag and heartbeat timing.
    // Returns true if the link is healthy.
    bool check_health(bool transport_connected);

    // Call when a reconnection attempt is made.
    void on_reconnect_attempt();

    // Reset reconnect counter (call on successful connection).
    void reset();

    int reconnect_attempts() const { return reconnect_attempts_.load(); }

    // Returns ms since the last heartbeat (or -1 if never received).
    int64_t ms_since_last_heartbeat() const;

    const char* state_str() const;

private:
    int heartbeat_timeout_ms_;
    int max_reconnect_attempts_;

    std::atomic<int>   reconnect_attempts_{0};
    std::atomic<State> state_{State::DISCONNECTED};

    // Time point of last received heartbeat, encoded as duration since epoch
    std::atomic<int64_t> last_heartbeat_ms_{-1};
};
