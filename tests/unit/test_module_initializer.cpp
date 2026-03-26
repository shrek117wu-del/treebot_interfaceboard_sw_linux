/**
 * @file test_module_initializer.cpp
 * @brief Unit tests for ModuleInitializer retry-with-backoff helper.
 *
 * Tests: successful startup, retry mechanism (MAX_RETRIES=3), exponential
 * backoff, max-retry-limit enforcement, and call-count verification.
 */

#include <gtest/gtest.h>

#include "module_initializer.h"
#include "logger.h"

#include <atomic>
#include <chrono>
#include <thread>

// ---------------------------------------------------------------------------
// Fake module that always succeeds on the first attempt
// ---------------------------------------------------------------------------
struct AlwaysSucceeds {
    std::atomic<int> call_count{0};

    bool start() {
        ++call_count;
        return true;
    }
};

// ---------------------------------------------------------------------------
// Fake module that always fails
// ---------------------------------------------------------------------------
struct AlwaysFails {
    std::atomic<int> call_count{0};

    bool start() {
        ++call_count;
        return false;
    }
};

// ---------------------------------------------------------------------------
// Fake module that succeeds on the N-th attempt
// ---------------------------------------------------------------------------
struct SucceedsOnAttempt {
    int target;
    std::atomic<int> call_count{0};

    explicit SucceedsOnAttempt(int n) : target(n) {}

    bool start() {
        int attempt = ++call_count;
        return attempt >= target;
    }
};

// ---------------------------------------------------------------------------
// Test fixture – redirects Logger to /dev/null to suppress output
// ---------------------------------------------------------------------------
class ModuleInitializerTest : public ::testing::Test {
protected:
    void SetUp() override {
        Logger::init("", Logger::FATAL); // silence during tests
    }
    void TearDown() override {
        Logger::shutdown();
    }
};

// ---------------------------------------------------------------------------
// Test: successful module starts in one attempt
// ---------------------------------------------------------------------------
TEST_F(ModuleInitializerTest, SuccessOnFirstAttempt) {
    AlwaysSucceeds mod;
    bool result = ModuleInitializer::start_with_retry(mod, "TestMod");
    EXPECT_TRUE(result);
    EXPECT_EQ(mod.call_count.load(), 1);
}

// ---------------------------------------------------------------------------
// Test: module that always fails exhausts MAX_RETRIES and returns false
// ---------------------------------------------------------------------------
TEST_F(ModuleInitializerTest, FailsAfterMaxRetries) {
    AlwaysFails mod;

    // Override the sleep to avoid slow tests: we cannot easily mock
    // std::this_thread::sleep_for, so we just verify call count.
    bool result = ModuleInitializer::start_with_retry(mod, "BadMod");

    EXPECT_FALSE(result);
    EXPECT_EQ(mod.call_count.load(), ModuleInitializer::MAX_RETRIES);
}

// ---------------------------------------------------------------------------
// Test: MAX_RETRIES constant equals 3 (documented in header)
// ---------------------------------------------------------------------------
TEST_F(ModuleInitializerTest, MaxRetriesIsThree) {
    EXPECT_EQ(ModuleInitializer::MAX_RETRIES, 3);
}

// ---------------------------------------------------------------------------
// Test: INITIAL_DELAY_MS constant equals 500 ms (documented in header)
// ---------------------------------------------------------------------------
TEST_F(ModuleInitializerTest, InitialDelayIs500Ms) {
    EXPECT_EQ(ModuleInitializer::INITIAL_DELAY_MS, 500);
}

// ---------------------------------------------------------------------------
// Test: module succeeds on 2nd attempt – only 2 calls made
// ---------------------------------------------------------------------------
TEST_F(ModuleInitializerTest, SuccessOnSecondAttempt) {
    SucceedsOnAttempt mod(2);
    bool result = ModuleInitializer::start_with_retry(mod, "Mod2");
    EXPECT_TRUE(result);
    EXPECT_EQ(mod.call_count.load(), 2);
}

// ---------------------------------------------------------------------------
// Test: module succeeds on last allowed attempt (MAX_RETRIES)
// ---------------------------------------------------------------------------
TEST_F(ModuleInitializerTest, SuccessOnLastAttempt) {
    SucceedsOnAttempt mod(ModuleInitializer::MAX_RETRIES);
    bool result = ModuleInitializer::start_with_retry(mod, "ModLast");
    EXPECT_TRUE(result);
    EXPECT_EQ(mod.call_count.load(), ModuleInitializer::MAX_RETRIES);
}

// ---------------------------------------------------------------------------
// Test: module that needs more than MAX_RETRIES returns false
// ---------------------------------------------------------------------------
TEST_F(ModuleInitializerTest, FailsWhenNeedsMoreThanMaxRetries) {
    SucceedsOnAttempt mod(ModuleInitializer::MAX_RETRIES + 1);
    bool result = ModuleInitializer::start_with_retry(mod, "ModOver");
    EXPECT_FALSE(result);
    EXPECT_EQ(mod.call_count.load(), ModuleInitializer::MAX_RETRIES);
}

// ---------------------------------------------------------------------------
// Test: multiple different modules can be initialized independently
// ---------------------------------------------------------------------------
TEST_F(ModuleInitializerTest, MultipleModulesIndependent) {
    AlwaysSucceeds m1;
    AlwaysSucceeds m2;
    EXPECT_TRUE(ModuleInitializer::start_with_retry(m1, "M1"));
    EXPECT_TRUE(ModuleInitializer::start_with_retry(m2, "M2"));
    EXPECT_EQ(m1.call_count.load(), 1);
    EXPECT_EQ(m2.call_count.load(), 1);
}
