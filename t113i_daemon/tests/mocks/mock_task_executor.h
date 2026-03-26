#pragma once
/**
 * @file mock_task_executor.h
 * @brief Minimal stub for TaskExecutor used in unit and integration tests.
 *
 * Records enqueued commands; does not run a worker thread.
 * Allows tests to inspect what was enqueued and simulate result callbacks.
 */

#include "task_executor.h"
#include "protocol.h"

#include <functional>
#include <mutex>
#include <vector>

// ---------------------------------------------------------------------------
// MockTaskExecutor – synchronous stub, no worker thread
// ---------------------------------------------------------------------------
class MockTaskExecutor {
public:
    MockTaskExecutor() = default;

    bool start() { return true; }
    void stop()  {}

    bool enqueue(const TaskCommandPayload& cmd) {
        std::lock_guard<std::mutex> lk(mutex_);
        if (queue_.size() >= max_depth_) return false;
        queue_.push_back(cmd);
        return true;
    }

    bool is_queue_empty() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return queue_.empty();
    }

    size_t queue_size() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return queue_.size();
    }

    using ResultCb = std::function<void(const TaskResponsePayload&)>;
    void set_result_callback(ResultCb cb) {
        std::lock_guard<std::mutex> lk(mutex_);
        result_cb_ = std::move(cb);
    }

    // -----------------------------------------------------------------------
    // Test helpers
    // -----------------------------------------------------------------------

    // Drain queue and invoke result_cb for each with given result code.
    void flush_results(uint8_t result_code = 0) {
        std::lock_guard<std::mutex> lk(mutex_);
        for (const auto& cmd : queue_) {
            if (result_cb_) {
                TaskResponsePayload resp{};
                resp.task_id   = cmd.task_id;
                resp.acked_seq = 0;
                resp.result    = result_code;
                std::strncpy(resp.message, "mock OK", sizeof(resp.message) - 1);
                result_cb_(resp);
            }
        }
        queue_.clear();
    }

    // Set max queue depth
    void set_max_depth(size_t d) { max_depth_ = d; }

    // Access queued commands
    std::vector<TaskCommandPayload> commands() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return queue_;
    }

    void clear() {
        std::lock_guard<std::mutex> lk(mutex_);
        queue_.clear();
    }

private:
    mutable std::mutex              mutex_;
    std::vector<TaskCommandPayload> queue_;
    ResultCb                        result_cb_;
    size_t                          max_depth_{TaskExecutor::MAX_QUEUE_DEPTH};
};
