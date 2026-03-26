/**
 * @file test_logger.cpp
 * @brief Unit tests for the Logger class.
 *
 * Tests: initialization, log levels, file output, rotation trigger,
 * thread-safety (concurrent writes), and shutdown.
 */

#include "logger.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helper: read entire file into string
// ---------------------------------------------------------------------------
static std::string read_file(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return {};
    return std::string(std::istreambuf_iterator<char>(f),
                       std::istreambuf_iterator<char>());
}

// ---------------------------------------------------------------------------
// Fixture – creates a temp log file per test
// ---------------------------------------------------------------------------
class LoggerTest : public ::testing::Test {
protected:
    std::string log_path_;

    void SetUp() override {
        log_path_ = (fs::temp_directory_path() /
                     ("test_logger_" + std::to_string(getpid()) + ".log")).string();
        Logger::init(log_path_, Logger::DEBUG);
    }

    void TearDown() override {
        Logger::shutdown();
        std::error_code ec;
        fs::remove(log_path_, ec);
        fs::remove(log_path_ + ".1", ec);
    }
};

// ---------------------------------------------------------------------------
// Initialization
// ---------------------------------------------------------------------------
TEST_F(LoggerTest, InitCreatesLogFile) {
    Logger::info("Test", "init check");
    Logger::shutdown();
    EXPECT_TRUE(fs::exists(log_path_));
}

TEST_F(LoggerTest, InitNoPathDoesNotCreateFile) {
    Logger::shutdown();
    Logger::init("", Logger::INFO);
    Logger::info("Test", "stderr only");
    // No file should be created when path is empty
    Logger::shutdown();
}

// ---------------------------------------------------------------------------
// Log levels
// ---------------------------------------------------------------------------
TEST_F(LoggerTest, DebugLevelWritesDebug) {
    Logger::init(log_path_, Logger::DEBUG);
    Logger::debug("Tag", "debug message");
    Logger::shutdown();
    std::string content = read_file(log_path_);
    EXPECT_NE(content.find("DEBUG"), std::string::npos);
    EXPECT_NE(content.find("debug message"), std::string::npos);
}

TEST_F(LoggerTest, InfoLevelFiltersDebug) {
    Logger::init(log_path_, Logger::INFO);
    Logger::debug("Tag", "should be filtered");
    Logger::info("Tag", "should appear");
    Logger::shutdown();
    std::string content = read_file(log_path_);
    EXPECT_EQ(content.find("should be filtered"), std::string::npos);
    EXPECT_NE(content.find("should appear"), std::string::npos);
}

TEST_F(LoggerTest, WarnLevelFiltersInfoAndDebug) {
    Logger::init(log_path_, Logger::WARN);
    Logger::debug("Tag", "d");
    Logger::info("Tag", "i");
    Logger::warn("Tag", "w_msg");
    Logger::shutdown();
    std::string content = read_file(log_path_);
    EXPECT_EQ(content.find("d"), std::string::npos)
        << "debug should not appear at WARN level";
    EXPECT_NE(content.find("w_msg"), std::string::npos);
}

TEST_F(LoggerTest, ErrorLevelFiltersLower) {
    Logger::init(log_path_, Logger::ERROR);
    Logger::warn("Tag", "warn_msg");
    Logger::error("Tag", "error_msg");
    Logger::shutdown();
    std::string content = read_file(log_path_);
    EXPECT_EQ(content.find("warn_msg"), std::string::npos);
    EXPECT_NE(content.find("error_msg"), std::string::npos);
}

TEST_F(LoggerTest, FatalLevelAlwaysWritten) {
    Logger::init(log_path_, Logger::FATAL);
    Logger::error("Tag", "ignored");
    Logger::fatal("Tag", "fatal_msg");
    Logger::shutdown();
    std::string content = read_file(log_path_);
    EXPECT_EQ(content.find("ignored"), std::string::npos);
    EXPECT_NE(content.find("fatal_msg"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Timestamp format
// ---------------------------------------------------------------------------
TEST_F(LoggerTest, TimestampContainsDateFormat) {
    Logger::info("Tag", "ts_test");
    Logger::shutdown();
    std::string content = read_file(log_path_);
    // Expect "YYYY-MM-DD HH:MM:SS.mmm" substring
    EXPECT_TRUE(content.find("20") != std::string::npos ||
                content.find("19") != std::string::npos)
        << "Timestamp should start with year 20xx";
}

// ---------------------------------------------------------------------------
// Tag appears in output
// ---------------------------------------------------------------------------
TEST_F(LoggerTest, TagAppearsInOutput) {
    Logger::info("MyModule", "hello");
    Logger::shutdown();
    std::string content = read_file(log_path_);
    EXPECT_NE(content.find("MyModule"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Convenience wrappers
// ---------------------------------------------------------------------------
TEST_F(LoggerTest, ConvenienceWrappersWork) {
    Logger::init(log_path_, Logger::DEBUG);
    Logger::debug("T", "dbg");
    Logger::info("T", "inf");
    Logger::warn("T", "wrn");
    Logger::error("T", "err");
    Logger::fatal("T", "fat");
    Logger::shutdown();
    std::string content = read_file(log_path_);
    for (const auto& m : {"dbg", "inf", "wrn", "err", "fat"}) {
        EXPECT_NE(content.find(m), std::string::npos) << "Missing: " << m;
    }
}

// ---------------------------------------------------------------------------
// Thread safety: concurrent writes must not corrupt the file
// ---------------------------------------------------------------------------
TEST_F(LoggerTest, ConcurrentWritesDoNotCorrupt) {
    Logger::init(log_path_, Logger::DEBUG);

    static constexpr int THREADS = 8;
    static constexpr int MSGS    = 200;

    std::vector<std::thread> threads;
    std::atomic<int> total{0};

    for (int t = 0; t < THREADS; ++t) {
        threads.emplace_back([t, &total]() {
            for (int i = 0; i < MSGS; ++i) {
                Logger::info("Thread" + std::to_string(t),
                             "msg " + std::to_string(i));
                ++total;
            }
        });
    }
    for (auto& th : threads) th.join();

    Logger::shutdown();

    // File should exist and have sensible content
    std::string content = read_file(log_path_);
    EXPECT_FALSE(content.empty());
    EXPECT_EQ(total.load(), THREADS * MSGS);

    // Count newlines – each log line ends with '\n'
    int lines = 0;
    for (char c : content) if (c == '\n') ++lines;
    EXPECT_EQ(lines, THREADS * MSGS);
}

// ---------------------------------------------------------------------------
// Shutdown is idempotent
// ---------------------------------------------------------------------------
TEST_F(LoggerTest, ShutdownIsIdempotent) {
    Logger::info("Tag", "msg");
    Logger::shutdown();
    EXPECT_NO_THROW(Logger::shutdown());
}

// ---------------------------------------------------------------------------
// Reinit after shutdown
// ---------------------------------------------------------------------------
TEST_F(LoggerTest, ReinitAfterShutdown) {
    Logger::info("Tag", "first");
    Logger::shutdown();

    std::string path2 = log_path_ + "_2";
    Logger::init(path2, Logger::INFO);
    Logger::info("Tag", "second");
    Logger::shutdown();

    std::string c2 = read_file(path2);
    EXPECT_NE(c2.find("second"), std::string::npos);

    std::error_code ec;
    fs::remove(path2, ec);
}
