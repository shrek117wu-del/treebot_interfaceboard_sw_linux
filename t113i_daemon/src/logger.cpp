/**
 * @file logger.cpp
 * @brief Logger implementation with timestamps and optional file output.
 */

#include "logger.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <iostream>

// ---------------------------------------------------------------------------
// Static member definitions
// ---------------------------------------------------------------------------
std::mutex        Logger::mutex_;
std::ofstream     Logger::file_;
std::atomic<int>  Logger::level_{Logger::INFO};
std::string       Logger::log_path_;

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void Logger::init(const std::string& log_path, Level level) {
    std::lock_guard<std::mutex> lk(mutex_);
    level_.store(static_cast<int>(level));
    log_path_ = log_path;

    if (!log_path.empty()) {
        file_.open(log_path, std::ios_base::app);
        if (!file_.is_open()) {
            std::cerr << "[Logger] WARNING: Cannot open log file: " << log_path << "\n";
        }
    }
}

void Logger::shutdown() {
    std::lock_guard<std::mutex> lk(mutex_);
    if (file_.is_open()) {
        file_.flush();
        file_.close();
    }
}

void Logger::log(Level lvl, const std::string& tag, const std::string& msg) {
    if (static_cast<int>(lvl) < level_.load()) return;

    std::string line = current_timestamp() + " [" + level_str(lvl) + "] " +
                       tag + ": " + msg + "\n";

    std::lock_guard<std::mutex> lk(mutex_);
    std::cerr << line;

    if (file_.is_open()) {
        rotate_if_needed();
        file_ << line;
        file_.flush();
    }
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

const char* Logger::level_str(Level lvl) {
    switch (lvl) {
        case DEBUG: return "DEBUG";
        case INFO:  return "INFO ";
        case WARN:  return "WARN ";
        case ERROR: return "ERROR";
        case FATAL: return "FATAL";
        default:    return "?????";
    }
}

std::string Logger::current_timestamp() {
    using namespace std::chrono;
    auto now     = system_clock::now();
    auto time_t_ = system_clock::to_time_t(now);
    auto ms      = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

    struct tm tm_info{};
#ifdef _WIN32
    localtime_s(&tm_info, &time_t_);
#else
    localtime_r(&time_t_, &tm_info);
#endif

    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_info);

    char ms_buf[8];
    std::snprintf(ms_buf, sizeof(ms_buf), ".%03d", static_cast<int>(ms.count()));

    return std::string(buf) + ms_buf;
}

void Logger::rotate_if_needed() {
    // rotate_ is called with mutex_ held and file_ open
    if (log_path_.empty() || !file_.is_open()) return;

    // Check file size via tellp
    auto pos = file_.tellp();
    // tellp() returns -1 on error; skip rotation if we cannot determine size
    if (pos < static_cast<std::streampos>(0)) return;
    if (static_cast<size_t>(pos) < MAX_LOG_SIZE) return;

    file_.close();
    std::string backup = log_path_ + ".1";
    // Remove old backup and rename current
    std::error_code ec;
    std::filesystem::remove(backup, ec);
    std::filesystem::rename(log_path_, backup, ec);
    if (ec) {
        // Rotation failed – reopen in append mode to avoid losing future logs
        file_.open(log_path_, std::ios_base::app);
        return;
    }
    file_.open(log_path_, std::ios_base::trunc);
    if (!file_.is_open()) {
        std::cerr << "[Logger] WARNING: Cannot reopen log file after rotation\n";
    }
}
