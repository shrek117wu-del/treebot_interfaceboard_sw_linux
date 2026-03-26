#pragma once
/**
 * @file task_executor.h
 * @brief Asynchronous task execution engine for the T113i daemon.
 *
 * Executes long-running tasks (arm_home, estop, shutdown, valve control)
 * in a dedicated thread.  Includes:
 *  - TaskContext validity checks (P0)
 *  - Queue depth limiting with overflow protection (P0)
 *  - Timeout-based task cleanup
 *  - Sequence-number–based pending task tracking (P1)
 */

#include "protocol.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>

// Forward declarations to avoid circular includes
class SerialComm;
class PowerManager;
class GpioController;

// ---------------------------------------------------------------------------
// TaskContext – holds non-owning pointers to shared subsystems
// ---------------------------------------------------------------------------
struct TaskContext {
    SerialComm*      comm{nullptr};
    PowerManager*    power{nullptr};
    GpioController*  gpio{nullptr};

    // Returns true if all pointers are non-null.
    bool is_valid() const { return comm && power && gpio; }
};

// ---------------------------------------------------------------------------
// TaskExecutor – queues and executes TaskCommandPayload
// ---------------------------------------------------------------------------
class TaskExecutor {
public:
    static constexpr size_t MAX_QUEUE_DEPTH = 64;
    static constexpr int    TASK_TIMEOUT_MS = 30000;

    explicit TaskExecutor(const TaskContext& ctx);
    ~TaskExecutor();

    bool start();
    void stop();

    // Enqueue a task command.  Returns false if the queue is full.
    bool enqueue(const TaskCommandPayload& cmd);

    // Returns true when the task queue is empty (useful for graceful shutdown).
    bool is_queue_empty() const;

    // Number of pending tasks in the queue.
    size_t queue_size() const;

    // Set callback invoked when a task completes (called from worker thread).
    using ResultCb = std::function<void(const TaskResponsePayload&)>;
    void set_result_callback(ResultCb cb);

private:
    TaskContext                    ctx_;
    ResultCb                       result_cb_;
    mutable std::mutex             mutex_;
    std::queue<TaskCommandPayload> queue_;
    std::condition_variable        cv_;
    std::atomic<bool>              running_{false};
    std::thread                    worker_;

    void worker_loop();
    void execute_task(const TaskCommandPayload& cmd);
    void report_result(TaskId task_id, uint8_t result, const char* msg);

    // Task implementations
    void do_arm_home(const TaskCommandPayload& cmd);
    void do_estop(const TaskCommandPayload& cmd);
    void do_shutdown_all(const TaskCommandPayload& cmd);
    void do_valve(const TaskCommandPayload& cmd, bool enable);
    void do_custom(const TaskCommandPayload& cmd);
};
