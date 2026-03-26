/**
 * @file integration_test_error_recovery.cpp
 * @brief Integration test: connection timeout and auto-reconnection.
 *
 * Uses ConnectionMonitor to simulate heartbeat loss, validates TIMEOUT
 * detection, reconnect attempt counting, and state recovery.
 */

#include <gtest/gtest.h>

#include "connection_monitor.h"
#include "logger.h"

#include <chrono>
#include <thread>

class ErrorRecoveryTest : public ::testing::Test {
protected:
    void SetUp() override {
        Logger::init("", Logger::FATAL);
    }
    void TearDown() override {
        Logger::shutdown();
    }
};

// ---------------------------------------------------------------------------
// Test: connection times out when no heartbeat received
// ---------------------------------------------------------------------------
TEST_F(ErrorRecoveryTest, HeartbeatTimeoutDetected) {
    ConnectionMonitor cm(100 /* 100 ms timeout */);
    cm.on_heartbeat_received();
    cm.check_health(true); // → CONNECTED

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    bool healthy = cm.check_health(true);
    EXPECT_FALSE(healthy);
    EXPECT_EQ(cm.state(), ConnectionMonitor::State::TIMEOUT);
}

// ---------------------------------------------------------------------------
// Test: reconnect attempts are counted after timeout
// ---------------------------------------------------------------------------
TEST_F(ErrorRecoveryTest, ReconnectAttemptsCounted) {
    ConnectionMonitor cm(100);
    cm.on_heartbeat_received();
    cm.check_health(true);

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    cm.check_health(true); // timeout

    for (int i = 0; i < 5; ++i) {
        cm.on_reconnect_attempt();
    }
    EXPECT_EQ(cm.reconnect_attempts(), 5);
}

// ---------------------------------------------------------------------------
// Test: service unavailable – transport down keeps DISCONNECTED
// ---------------------------------------------------------------------------
TEST_F(ErrorRecoveryTest, ServiceUnavailableRemainDisconnected) {
    ConnectionMonitor cm(5000);

    for (int i = 0; i < 10; ++i) {
        bool healthy = cm.check_health(false /* not connected */);
        EXPECT_FALSE(healthy);
        EXPECT_EQ(cm.state(), ConnectionMonitor::State::DISCONNECTED);
    }
}

// ---------------------------------------------------------------------------
// Test: pending tasks survive simulated reconnect cycle (state machine only)
// ---------------------------------------------------------------------------
TEST_F(ErrorRecoveryTest, StateSurvivesReconnectCycle) {
    ConnectionMonitor cm(100);

    // Connect and timeout
    cm.on_heartbeat_received();
    cm.check_health(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    cm.check_health(true); // TIMEOUT

    // Simulate reconnect
    cm.on_reconnect_attempt();
    cm.reset();
    EXPECT_EQ(cm.state(), ConnectionMonitor::State::DISCONNECTED);
    EXPECT_EQ(cm.reconnect_attempts(), 0);

    // Re-connect and receive heartbeat
    cm.on_heartbeat_received();
    bool healthy = cm.check_health(true);
    EXPECT_TRUE(healthy);
    EXPECT_EQ(cm.state(), ConnectionMonitor::State::CONNECTED);
}

// ---------------------------------------------------------------------------
// Test: multiple timeout / recovery cycles
// ---------------------------------------------------------------------------
TEST_F(ErrorRecoveryTest, MultipleTimeoutRecoveryCycles) {
    ConnectionMonitor cm(100);

    for (int cycle = 0; cycle < 3; ++cycle) {
        cm.on_heartbeat_received();
        cm.check_health(true);

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        bool healthy = cm.check_health(true);
        EXPECT_FALSE(healthy) << "cycle " << cycle;
        EXPECT_EQ(cm.state(), ConnectionMonitor::State::TIMEOUT) << "cycle " << cycle;

        // Recovery
        cm.on_heartbeat_received();
        healthy = cm.check_health(true);
        EXPECT_TRUE(healthy) << "cycle " << cycle;
    }
}

// ---------------------------------------------------------------------------
// Test: graceful degradation – monitor reports unhealthy without crash
// ---------------------------------------------------------------------------
TEST_F(ErrorRecoveryTest, GracefulDegradationNoCrash) {
    ConnectionMonitor cm(50);
    // Rapid check_health calls without any heartbeats
    for (int i = 0; i < 100; ++i) {
        cm.check_health(true);
        cm.check_health(false);
    }
    // No crash = pass
    SUCCEED();
}
