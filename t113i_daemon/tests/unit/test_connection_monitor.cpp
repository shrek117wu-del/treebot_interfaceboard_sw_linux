/**
 * @file test_connection_monitor.cpp
 * @brief Unit tests for ConnectionMonitor.
 *
 * Tests: state machine transitions, heartbeat timeout detection,
 * reconnection attempt counting, and reset behaviour.
 */

#include "connection_monitor.h"
#include "logger.h"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

using namespace std::chrono_literals;

// Silence Logger output during tests
class ConnMonitorTest : public ::testing::Test {
protected:
    void SetUp() override {
        Logger::init("", Logger::FATAL); // suppress output
    }
    void TearDown() override {
        Logger::shutdown();
    }
};

// ---------------------------------------------------------------------------
// Initial state
// ---------------------------------------------------------------------------
TEST_F(ConnMonitorTest, InitialStateIsDisconnected) {
    ConnectionMonitor mon(5000);
    EXPECT_EQ(mon.state(), ConnectionMonitor::State::DISCONNECTED);
}

TEST_F(ConnMonitorTest, InitialMsSinceHeartbeatIsNegativeOne) {
    ConnectionMonitor mon(5000);
    EXPECT_EQ(mon.ms_since_last_heartbeat(), -1);
}

// ---------------------------------------------------------------------------
// check_health when transport is disconnected
// ---------------------------------------------------------------------------
TEST_F(ConnMonitorTest, DisconnectedTransportReturnsFalse) {
    ConnectionMonitor mon(5000);
    bool healthy = mon.check_health(false);
    EXPECT_FALSE(healthy);
    EXPECT_EQ(mon.state(), ConnectionMonitor::State::DISCONNECTED);
}

// ---------------------------------------------------------------------------
// check_health with no heartbeat yet → transport connected but no HB
// ---------------------------------------------------------------------------
TEST_F(ConnMonitorTest, ConnectedNoHeartbeatIsHealthy) {
    // No heartbeat has been received; elapsed == -1, so timeout cannot fire.
    ConnectionMonitor mon(5000);
    bool healthy = mon.check_health(true);
    EXPECT_TRUE(healthy);
    EXPECT_EQ(mon.state(), ConnectionMonitor::State::CONNECTED);
}

// ---------------------------------------------------------------------------
// Heartbeat received → healthy
// ---------------------------------------------------------------------------
TEST_F(ConnMonitorTest, RecentHeartbeatIsHealthy) {
    ConnectionMonitor mon(5000);
    mon.on_heartbeat_received();
    bool healthy = mon.check_health(true);
    EXPECT_TRUE(healthy);
    EXPECT_EQ(mon.state(), ConnectionMonitor::State::CONNECTED);
}

// ---------------------------------------------------------------------------
// Heartbeat timeout
// ---------------------------------------------------------------------------
TEST_F(ConnMonitorTest, HeartbeatTimeoutMakesUnhealthy) {
    ConnectionMonitor mon(50); // 50 ms timeout
    mon.on_heartbeat_received();
    std::this_thread::sleep_for(100ms);
    bool healthy = mon.check_health(true);
    EXPECT_FALSE(healthy);
    EXPECT_EQ(mon.state(), ConnectionMonitor::State::TIMEOUT);
}

TEST_F(ConnMonitorTest, HeartbeatRefreshPreventsTimeout) {
    ConnectionMonitor mon(200);
    for (int i = 0; i < 5; ++i) {
        mon.on_heartbeat_received();
        std::this_thread::sleep_for(50ms);
        EXPECT_TRUE(mon.check_health(true))
            << "Should be healthy at iteration " << i;
    }
}

// ---------------------------------------------------------------------------
// State transitions: CONNECTED → DISCONNECTED
// ---------------------------------------------------------------------------
TEST_F(ConnMonitorTest, ConnectedThenDisconnect) {
    ConnectionMonitor mon(5000);
    mon.on_heartbeat_received();
    EXPECT_EQ(mon.state(), ConnectionMonitor::State::DISCONNECTED); // before check
    mon.check_health(true);
    EXPECT_EQ(mon.state(), ConnectionMonitor::State::CONNECTED);
    mon.check_health(false);
    EXPECT_EQ(mon.state(), ConnectionMonitor::State::DISCONNECTED);
}

// ---------------------------------------------------------------------------
// Reconnect attempt counting
// ---------------------------------------------------------------------------
TEST_F(ConnMonitorTest, ReconnectAttemptCounterIncrements) {
    ConnectionMonitor mon(5000);
    EXPECT_EQ(mon.reconnect_attempts(), 0);
    mon.on_reconnect_attempt();
    EXPECT_EQ(mon.reconnect_attempts(), 1);
    mon.on_reconnect_attempt();
    EXPECT_EQ(mon.reconnect_attempts(), 2);
    EXPECT_EQ(mon.state(), ConnectionMonitor::State::CONNECTING);
}

TEST_F(ConnMonitorTest, ResetClearsAttemptCounter) {
    ConnectionMonitor mon(5000);
    mon.on_reconnect_attempt();
    mon.on_reconnect_attempt();
    mon.reset();
    EXPECT_EQ(mon.reconnect_attempts(), 0);
}

TEST_F(ConnMonitorTest, ResetClearsHeartbeat) {
    ConnectionMonitor mon(5000);
    mon.on_heartbeat_received();
    mon.reset();
    EXPECT_EQ(mon.ms_since_last_heartbeat(), -1);
}

TEST_F(ConnMonitorTest, ResetSetsStateToDisconnected) {
    ConnectionMonitor mon(5000);
    mon.on_heartbeat_received();
    mon.check_health(true); // → CONNECTED
    mon.reset();
    EXPECT_EQ(mon.state(), ConnectionMonitor::State::DISCONNECTED);
}

// ---------------------------------------------------------------------------
// state_str()
// ---------------------------------------------------------------------------
TEST_F(ConnMonitorTest, StateStrDisconnected) {
    ConnectionMonitor mon(5000);
    EXPECT_STREQ(mon.state_str(), "DISCONNECTED");
}

TEST_F(ConnMonitorTest, StateStrConnected) {
    ConnectionMonitor mon(5000);
    mon.on_heartbeat_received();
    mon.check_health(true);
    EXPECT_STREQ(mon.state_str(), "CONNECTED");
}

TEST_F(ConnMonitorTest, StateStrConnecting) {
    ConnectionMonitor mon(5000);
    mon.on_reconnect_attempt();
    EXPECT_STREQ(mon.state_str(), "CONNECTING");
}

TEST_F(ConnMonitorTest, StateStrTimeout) {
    ConnectionMonitor mon(10);
    mon.on_heartbeat_received();
    std::this_thread::sleep_for(30ms);
    mon.check_health(true);
    EXPECT_STREQ(mon.state_str(), "TIMEOUT");
}

// ---------------------------------------------------------------------------
// ms_since_last_heartbeat accuracy (rough)
// ---------------------------------------------------------------------------
TEST_F(ConnMonitorTest, MsSinceHeartbeatIsReasonable) {
    ConnectionMonitor mon(5000);
    mon.on_heartbeat_received();
    std::this_thread::sleep_for(100ms);
    int64_t elapsed = mon.ms_since_last_heartbeat();
    // Should be ~100ms; accept 50–500ms for CI jitter
    EXPECT_GE(elapsed, 50);
    EXPECT_LE(elapsed, 500);
}

// ---------------------------------------------------------------------------
// Custom timeout value
// ---------------------------------------------------------------------------
TEST_F(ConnMonitorTest, CustomTimeoutRespected) {
    ConnectionMonitor mon(300);
    mon.on_heartbeat_received();
    std::this_thread::sleep_for(150ms);
    EXPECT_TRUE(mon.check_health(true));  // within timeout
    std::this_thread::sleep_for(200ms);
    EXPECT_FALSE(mon.check_health(true)); // past timeout
}
