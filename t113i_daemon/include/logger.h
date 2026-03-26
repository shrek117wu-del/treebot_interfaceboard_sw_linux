#pragma once
/**
 * @file logger.h
 * @brief Centralized logging system for the T113i daemon.
 *
 * Provides log levels (DEBUG, INFO, WARN, ERROR, FATAL), timestamps, and
 * optional file logging with automatic rotation.
 */

#include <atomic>
#include <fstream>
#include <mutex>
#include <string>

class Logger {
public:
    enum Level { DEBUG = 0, INFO = 1, WARN = 2, ERROR = 3, FATAL = 4 };

    // Initialize the logger.  Must be called once before any log() calls.
    // log_path: path for the log file ("" disables file logging)
    // level:    minimum level to emit
    static void init(const std::string& log_path, Level level = INFO);

    // Close the log file (call on shutdown).
    static void shutdown();

    // Core logging function.
    static void log(Level lvl, const std::string& tag, const std::string& msg);

    // Convenience wrappers
    static void debug(const std::string& tag, const std::string& msg) {
        log(DEBUG, tag, msg);
    }
    static void info(const std::string& tag, const std::string& msg) {
        log(INFO, tag, msg);
    }
    static void warn(const std::string& tag, const std::string& msg) {
        log(WARN, tag, msg);
    }
    static void error(const std::string& tag, const std::string& msg) {
        log(ERROR, tag, msg);
    }
    static void fatal(const std::string& tag, const std::string& msg) {
        log(FATAL, tag, msg);
    }

private:
    static std::mutex        mutex_;
    static std::ofstream     file_;
    static std::atomic<int>  level_;
    static std::string       log_path_;

    static const char* level_str(Level lvl);
    static std::string current_timestamp();
    static void        rotate_if_needed();
    static const size_t MAX_LOG_SIZE = 10u * 1024u * 1024u; // 10 MB
};
