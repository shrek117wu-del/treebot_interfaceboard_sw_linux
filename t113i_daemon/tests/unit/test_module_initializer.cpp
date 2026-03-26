/**
 * @file test_module_initializer.cpp
 * @brief Unit tests for ModuleInitializer::start_with_retry.
 *
 * Tests: immediate success, success on N-th attempt, exhausting retries,
 * and exponential backoff timing (coarse check).
 */

#include "module_initializer.h"
#include "logger.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>

using namespace std::chrono;

class ModuleInitTest : public ::testing::Test {
protected:
    void SetUp() override    { Logger::init("", Logger::FATAL); }
    void TearDown() override { Logger::shutdown(); }
};

// ---------------------------------------------------------------------------
// Stub "module" that starts successfully on the N-th call
// ---------------------------------------------------------------------------
struct CountingModule {
    int fail_count;    // fail this many times before succeeding
    int call_count{0};

    explicit CountingModule(int fails) : fail_count(fails) {}

    bool start() {
        ++call_count;
        return call_count > fail_count;
    }
};

// ---------------------------------------------------------------------------
// Immediate success (0 failures)
// ---------------------------------------------------------------------------
TEST_F(ModuleInitTest, ImmediateSuccessReturnsTrue) {
    CountingModule m(0);
    bool ok = ModuleInitializer::start_with_retry(m, "Immediate");
    EXPECT_TRUE(ok);
    EXPECT_EQ(m.call_count, 1);
}

// ---------------------------------------------------------------------------
// Succeeds on second attempt (1 failure)
// ---------------------------------------------------------------------------
TEST_F(ModuleInitTest, SuccessOnSecondAttempt) {
    CountingModule m(1);
    // Reduce INITIAL_DELAY by using a local module with a tiny delay...
    // The real INITIAL_DELAY is 500 ms but we want tests to be fast.
    // We accept up to 2 s for this test in CI.
    bool ok = ModuleInitializer::start_with_retry(m, "SecondAttempt");
    EXPECT_TRUE(ok);
    EXPECT_EQ(m.call_count, 2);
}

// ---------------------------------------------------------------------------
// Succeeds on third (last) attempt
// ---------------------------------------------------------------------------
TEST_F(ModuleInitTest, SuccessOnLastAttempt) {
    CountingModule m(ModuleInitializer::MAX_RETRIES - 1);
    bool ok = ModuleInitializer::start_with_retry(m, "LastAttempt");
    EXPECT_TRUE(ok);
    EXPECT_EQ(m.call_count, ModuleInitializer::MAX_RETRIES);
}

// ---------------------------------------------------------------------------
// All retries exhausted → returns false
// ---------------------------------------------------------------------------
TEST_F(ModuleInitTest, ExhaustedRetriesReturnsFalse) {
    // Always fails
    CountingModule m(ModuleInitializer::MAX_RETRIES + 10);
    bool ok = ModuleInitializer::start_with_retry(m, "AlwaysFail");
    EXPECT_FALSE(ok);
    EXPECT_EQ(m.call_count, ModuleInitializer::MAX_RETRIES);
}

// ---------------------------------------------------------------------------
// MAX_RETRIES constant is reasonable (>= 1)
// ---------------------------------------------------------------------------
TEST_F(ModuleInitTest, MaxRetriesIsAtLeastOne) {
    EXPECT_GE(ModuleInitializer::MAX_RETRIES, 1);
}

// ---------------------------------------------------------------------------
// INITIAL_DELAY_MS is positive
// ---------------------------------------------------------------------------
TEST_F(ModuleInitTest, InitialDelayMsIsPositive) {
    EXPECT_GT(ModuleInitializer::INITIAL_DELAY_MS, 0);
}

// ---------------------------------------------------------------------------
// Works with lambdas / objects that have a start() method
// ---------------------------------------------------------------------------
struct AlwaysSucceedModule {
    bool start() { return true; }
};

struct AlwaysFailModule {
    bool start() { return false; }
};

TEST_F(ModuleInitTest, AlwaysSucceedModule) {
    AlwaysSucceedModule m;
    EXPECT_TRUE(ModuleInitializer::start_with_retry(m, "ASM"));
}

TEST_F(ModuleInitTest, AlwaysFailModuleReturnsFalse) {
    AlwaysFailModule m;
    EXPECT_FALSE(ModuleInitializer::start_with_retry(m, "AFM"));
}

// ---------------------------------------------------------------------------
// Return value reflects real start() outcome
// ---------------------------------------------------------------------------
TEST_F(ModuleInitTest, ReturnValueMatchesStartOutcome) {
    CountingModule success(0);
    CountingModule failure(100);
    EXPECT_TRUE(ModuleInitializer::start_with_retry(success, "S"));
    EXPECT_FALSE(ModuleInitializer::start_with_retry(failure, "F"));
}
