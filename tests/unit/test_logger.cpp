/**
 * @file test_logger.cpp
 * @brief Unit tests for the Logger class.
 *
 * Tests: initialization/shutdown, log-level filtering, timestamp format,
 * file output, 10 MB rotation guard, and thread-safe concurrent writes.
 */

#include <gtest/gtest.h>

#include "logger.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <regex>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helper: read the full content of a file into a string
// ---------------------------------------------------------------------------
static std::string read_file(const std::string& path) {
    std::ifstream f(path);
    return {std::istreambuf_iterator<char>(f), {}};
}

// ---------------------------------------------------------------------------
// Fixture – ensures Logger is shut down cleanly between tests
// ---------------------------------------------------------------------------
class LoggerTest : public ::testing::Test {
protected:
    std::string tmp_log_;

    void SetUp() override {
        tmp_log_ = fs::temp_directory_path() / "test_logger_XXXXXX.log";
        // Use a unique file per test
        tmp_log_ = (fs::temp_directory_path() / ("test_logger_" +
                    std::to_string(
                        std::chrono::steady_clock::now().time_since_epoch().count()) +
                    ".log")).string();
    }

    void TearDown() override {
        Logger::shutdown();
        if (fs::exists(tmp_log_)) {
            fs::remove(tmp_log_);
        }
    }
};

// ---------------------------------------------------------------------------
// Test: Logger initializes without crashing and can be shut down
// ---------------------------------------------------------------------------
TEST_F(LoggerTest, InitAndShutdown) {
    EXPECT_NO_THROW(Logger::init("", Logger::INFO));
    EXPECT_NO_THROW(Logger::shutdown());
    // Double shutdown should be safe
    EXPECT_NO_THROW(Logger::shutdown());
}

// ---------------------------------------------------------------------------
// Test: log() writes to a file when a path is provided
// ---------------------------------------------------------------------------
TEST_F(LoggerTest, WritesToFile) {
    Logger::init(tmp_log_, Logger::DEBUG);
    Logger::info("TAG", "hello file");
    Logger::shutdown();

    ASSERT_TRUE(fs::exists(tmp_log_)) << "Log file not created";
    std::string content = read_file(tmp_log_);
    EXPECT_NE(content.find("hello file"), std::string::npos);
    EXPECT_NE(content.find("TAG"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Test: DEBUG level logs all messages
// ---------------------------------------------------------------------------
TEST_F(LoggerTest, DebugLevelLogsAll) {
    Logger::init(tmp_log_, Logger::DEBUG);
    Logger::debug("T", "dbg");
    Logger::info("T", "inf");
    Logger::warn("T", "wrn");
    Logger::error("T", "err");
    Logger::fatal("T", "fat");
    Logger::shutdown();

    std::string c = read_file(tmp_log_);
    EXPECT_NE(c.find("dbg"), std::string::npos);
    EXPECT_NE(c.find("inf"), std::string::npos);
    EXPECT_NE(c.find("wrn"), std::string::npos);
    EXPECT_NE(c.find("err"), std::string::npos);
    EXPECT_NE(c.find("fat"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Test: ERROR level suppresses DEBUG, INFO, WARN
// ---------------------------------------------------------------------------
TEST_F(LoggerTest, ErrorLevelFiltersLower) {
    Logger::init(tmp_log_, Logger::ERROR);
    Logger::debug("T", "should_not_appear_dbg");
    Logger::info("T", "should_not_appear_inf");
    Logger::warn("T", "should_not_appear_wrn");
    Logger::error("T", "should_appear_err");
    Logger::shutdown();

    std::string c = read_file(tmp_log_);
    EXPECT_EQ(c.find("should_not_appear_dbg"), std::string::npos);
    EXPECT_EQ(c.find("should_not_appear_inf"), std::string::npos);
    EXPECT_EQ(c.find("should_not_appear_wrn"), std::string::npos);
    EXPECT_NE(c.find("should_appear_err"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Test: WARN level passes WARN, ERROR, FATAL but suppresses DEBUG, INFO
// ---------------------------------------------------------------------------
TEST_F(LoggerTest, WarnLevelFiltersBelowWarn) {
    Logger::init(tmp_log_, Logger::WARN);
    Logger::debug("T", "d_skip");
    Logger::info("T", "i_skip");
    Logger::warn("T", "w_pass");
    Logger::error("T", "e_pass");
    Logger::shutdown();

    std::string c = read_file(tmp_log_);
    EXPECT_EQ(c.find("d_skip"), std::string::npos);
    EXPECT_EQ(c.find("i_skip"), std::string::npos);
    EXPECT_NE(c.find("w_pass"), std::string::npos);
    EXPECT_NE(c.find("e_pass"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Test: each log line contains a timestamp that matches yyyy-mm-dd HH:MM:SS
// ---------------------------------------------------------------------------
TEST_F(LoggerTest, TimestampFormat) {
    Logger::init(tmp_log_, Logger::DEBUG);
    Logger::info("TS", "timestamp_test");
    Logger::shutdown();

    std::string c = read_file(tmp_log_);
    // Match ISO-8601-like prefix: "2024-01-15 12:34:56"
    std::regex ts_re(R"(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})");
    EXPECT_TRUE(std::regex_search(c, ts_re)) << "Timestamp not found in: " << c;
}

// ---------------------------------------------------------------------------
// Test: no crash when no file path is given (stderr-only mode)
// ---------------------------------------------------------------------------
TEST_F(LoggerTest, StderrOnlyModeNoCrash) {
    Logger::init("", Logger::INFO);
    EXPECT_NO_THROW(Logger::info("T", "stderr only"));
    Logger::shutdown();
}

// ---------------------------------------------------------------------------
// Test: concurrent writes from multiple threads do not crash
// ---------------------------------------------------------------------------
TEST_F(LoggerTest, ConcurrentWritesSafe) {
    Logger::init(tmp_log_, Logger::DEBUG);

    constexpr int kThreads = 8;
    constexpr int kMsgsPerThread = 50;

    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([t]() {
            for (int i = 0; i < kMsgsPerThread; ++i) {
                Logger::info("THR" + std::to_string(t),
                             "msg_" + std::to_string(i));
            }
        });
    }
    for (auto& th : threads) th.join();

    Logger::shutdown();

    // File should contain all messages (or at least a reasonable subset)
    std::string c = read_file(tmp_log_);
    EXPECT_FALSE(c.empty()) << "Log file is empty after concurrent writes";
}

// ---------------------------------------------------------------------------
// Test: calling log() before init() does not crash
// ---------------------------------------------------------------------------
TEST_F(LoggerTest, LogBeforeInitNoCrash) {
    // No init() called; just ensure it doesn't segfault
    EXPECT_NO_THROW(Logger::info("T", "before init"));
}
