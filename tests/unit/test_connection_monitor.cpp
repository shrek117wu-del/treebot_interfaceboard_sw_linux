/**
 * @file test_connection_monitor.cpp
 * @brief Unit tests for ConnectionMonitor state machine.
 *
 * Tests: state transitions, heartbeat timeout detection, reconnect counting,
 * exponential-backoff assumptions, and recovery after timeout.
 */

#include <gtest/gtest.h>

#include "connection_monitor.h"

#include <chrono>
#include <thread>

// ---------------------------------------------------------------------------
// Helper: sleep for N milliseconds (shorthand)
// ---------------------------------------------------------------------------
static void sleep_ms(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// ---------------------------------------------------------------------------
// Test: initial state is DISCONNECTED
// ---------------------------------------------------------------------------
TEST(ConnectionMonitorTest, InitialStateIsDisconnected) {
    ConnectionMonitor cm(500);
    EXPECT_EQ(cm.state(), ConnectionMonitor::State::DISCONNECTED);
}

// ---------------------------------------------------------------------------
// Test: check_health(false) keeps state DISCONNECTED
// ---------------------------------------------------------------------------
TEST(ConnectionMonitorTest, NotConnectedStaysDisconnected) {
    ConnectionMonitor cm(500);
    bool healthy = cm.check_health(false);
    EXPECT_FALSE(healthy);
    EXPECT_EQ(cm.state(), ConnectionMonitor::State::DISCONNECTED);
}

// ---------------------------------------------------------------------------
// Test: check_health(true) without any heartbeat → CONNECTED (no timeout yet)
// ---------------------------------------------------------------------------
TEST(ConnectionMonitorTest, ConnectedWithoutHeartbeatIsConnected) {
    // When transport is up but no heartbeat has ever been received,
    // ms_since_last_heartbeat() returns -1, so the timeout condition
    // (elapsed >= 0 && elapsed > timeout_ms) is false.
    // The implementation therefore sets state to CONNECTED.
    ConnectionMonitor cm(500);
    bool healthy = cm.check_health(true);
    EXPECT_TRUE(healthy);
    EXPECT_EQ(cm.state(), ConnectionMonitor::State::CONNECTED);
}

// ---------------------------------------------------------------------------
// Test: heartbeat received → CONNECTED and healthy
// ---------------------------------------------------------------------------
TEST(ConnectionMonitorTest, HeartbeatReceivedGoesConnected) {
    ConnectionMonitor cm(5000);
    cm.on_heartbeat_received();
    cm.check_health(true);
    EXPECT_EQ(cm.state(), ConnectionMonitor::State::CONNECTED);
}

// ---------------------------------------------------------------------------
// Test: heartbeat timeout causes TIMEOUT state
// ---------------------------------------------------------------------------
TEST(ConnectionMonitorTest, HeartbeatTimeoutDetected) {
    // 100 ms timeout for fast test
    ConnectionMonitor cm(100);
    cm.on_heartbeat_received();
    cm.check_health(true); // mark CONNECTED

    // Wait past timeout
    sleep_ms(200);

    bool healthy = cm.check_health(true);
    EXPECT_FALSE(healthy);
    EXPECT_EQ(cm.state(), ConnectionMonitor::State::TIMEOUT);
}

// ---------------------------------------------------------------------------
// Test: ms_since_last_heartbeat returns -1 before any heartbeat
// ---------------------------------------------------------------------------
TEST(ConnectionMonitorTest, MsSinceHeartbeatNegativeBeforeFirst) {
    ConnectionMonitor cm(5000);
    EXPECT_EQ(cm.ms_since_last_heartbeat(), -1);
}

// ---------------------------------------------------------------------------
// Test: ms_since_last_heartbeat is positive after heartbeat
// ---------------------------------------------------------------------------
TEST(ConnectionMonitorTest, MsSinceHeartbeatPositiveAfterReceive) {
    ConnectionMonitor cm(5000);
    cm.on_heartbeat_received();
    sleep_ms(10);
    int64_t elapsed = cm.ms_since_last_heartbeat();
    EXPECT_GT(elapsed, 0);
}

// ---------------------------------------------------------------------------
// Test: on_reconnect_attempt increments counter
// ---------------------------------------------------------------------------
TEST(ConnectionMonitorTest, ReconnectAttemptCountIncremented) {
    ConnectionMonitor cm(5000);
    EXPECT_EQ(cm.reconnect_attempts(), 0);
    cm.on_reconnect_attempt();
    EXPECT_EQ(cm.reconnect_attempts(), 1);
    cm.on_reconnect_attempt();
    EXPECT_EQ(cm.reconnect_attempts(), 2);
}

// ---------------------------------------------------------------------------
// Test: reset() clears reconnect counter and sets DISCONNECTED
// ---------------------------------------------------------------------------
TEST(ConnectionMonitorTest, ResetClearsCounterAndState) {
    ConnectionMonitor cm(5000);
    cm.on_reconnect_attempt();
    cm.on_reconnect_attempt();
    cm.on_heartbeat_received();
    cm.check_health(true); // → CONNECTED

    cm.reset();
    EXPECT_EQ(cm.reconnect_attempts(), 0);
    EXPECT_EQ(cm.state(), ConnectionMonitor::State::DISCONNECTED);
}

// ---------------------------------------------------------------------------
// Test: state_str() returns a non-empty string for each state
// ---------------------------------------------------------------------------
TEST(ConnectionMonitorTest, StateStrNonEmpty) {
    ConnectionMonitor cm(5000);
    EXPECT_STRNE(cm.state_str(), "");
    cm.on_heartbeat_received();
    cm.check_health(true);
    EXPECT_STRNE(cm.state_str(), "");
}

// ---------------------------------------------------------------------------
// Test: recovery – new heartbeat after timeout transitions back to CONNECTED
// ---------------------------------------------------------------------------
TEST(ConnectionMonitorTest, RecoveryAfterTimeout) {
    ConnectionMonitor cm(100);
    cm.on_heartbeat_received();
    cm.check_health(true); // CONNECTED

    sleep_ms(200); // timeout
    cm.check_health(true); // TIMEOUT

    // Peer recovers – send new heartbeat
    cm.on_heartbeat_received();
    bool healthy = cm.check_health(true);
    EXPECT_TRUE(healthy);
    EXPECT_EQ(cm.state(), ConnectionMonitor::State::CONNECTED);
}

// ---------------------------------------------------------------------------
// Test: max_reconnect_attempts = 0 means unlimited (counter keeps growing)
// ---------------------------------------------------------------------------
TEST(ConnectionMonitorTest, UnlimitedReconnectAttempts) {
    ConnectionMonitor cm(5000, 0 /* unlimited */);
    for (int i = 0; i < 100; ++i) {
        cm.on_reconnect_attempt();
    }
    EXPECT_EQ(cm.reconnect_attempts(), 100);
}
