/**
 * @file task_executor.cpp
 * @brief TaskExecutor implementation – dequeues and executes TaskCommandPayloads.
 */

#include "task_executor.h"
#include "logger.h"
#include "serial_comm.h"
#include "power_manager.h"
#include "gpio_controller.h"
#include "utils.h"

#include <cstring>

// ---------------------------------------------------------------------------
// TaskExecutor – lifecycle
// ---------------------------------------------------------------------------

TaskExecutor::TaskExecutor(const TaskContext& ctx) : ctx_(ctx) {}

TaskExecutor::~TaskExecutor() { stop(); }

bool TaskExecutor::start() {
    if (!ctx_.is_valid()) {
        Logger::error("TaskExecutor", "TaskContext is invalid, cannot start");
        return false;
    }
    running_  = true;
    worker_   = std::thread(&TaskExecutor::worker_loop, this);
    Logger::info("TaskExecutor", "Started");
    return true;
}

void TaskExecutor::stop() {
    running_ = false;
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

// ---------------------------------------------------------------------------
// Queue management
// ---------------------------------------------------------------------------

bool TaskExecutor::enqueue(const TaskCommandPayload& cmd) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (queue_.size() >= MAX_QUEUE_DEPTH) {
        Logger::warn("TaskExecutor",
            "Queue full (depth=" + std::to_string(MAX_QUEUE_DEPTH) +
            "), rejecting task " + std::to_string(cmd.task_id));
        return false;
    }
    queue_.push(cmd);
    cv_.notify_one();
    return true;
}

bool TaskExecutor::is_queue_empty() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return queue_.empty();
}

size_t TaskExecutor::queue_size() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return queue_.size();
}

void TaskExecutor::set_result_callback(ResultCb cb) {
    std::lock_guard<std::mutex> lk(mutex_);
    result_cb_ = std::move(cb);
}

// ---------------------------------------------------------------------------
// Worker thread
// ---------------------------------------------------------------------------

void TaskExecutor::worker_loop() {
    while (running_) {
        TaskCommandPayload cmd{};
        {
            std::unique_lock<std::mutex> lk(mutex_);
            cv_.wait(lk, [this] { return !queue_.empty() || !running_; });
            if (!running_ && queue_.empty()) break;
            cmd = queue_.front();
            queue_.pop();
        }

        if (!ctx_.is_valid()) {
            Logger::error("TaskExecutor",
                "TaskContext became invalid mid-execution");
            report_result(cmd.task_id, 1, "Context invalid");
            continue;
        }

        execute_task(cmd);
    }
}

void TaskExecutor::execute_task(const TaskCommandPayload& cmd) {
    Logger::info("TaskExecutor",
        std::string("Executing task ") + std::to_string(cmd.task_id) +
        " (\"" + std::string(cmd.name, strnlen(cmd.name, sizeof(cmd.name))) + "\")");

    switch (cmd.task_id) {
        case TASK_ARM_HOME:      do_arm_home(cmd);        break;
        case TASK_ESTOP:         do_estop(cmd);           break;
        case TASK_SHUTDOWN_ALL:  do_shutdown_all(cmd);    break;
        case TASK_ENABLE_VALVE:  do_valve(cmd, true);     break;
        case TASK_DISABLE_VALVE: do_valve(cmd, false);    break;
        case TASK_CUSTOM:        do_custom(cmd);          break;
        default:
            Logger::warn("TaskExecutor",
                "Unknown task_id: " + std::to_string(cmd.task_id));
            report_result(cmd.task_id, 1, "Unknown task");
            break;
    }
}

void TaskExecutor::report_result(TaskId task_id, uint8_t result,
                                  const char* msg)
{
    ResultCb cb;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        cb = result_cb_;
    }
    if (!cb) return;

    TaskResponsePayload resp{};
    resp.task_id   = task_id;
    resp.acked_seq = 0; // filled in by main.cpp result callback
    resp.result    = result;
    std::strncpy(resp.message, msg, sizeof(resp.message) - 1);
    resp.message[sizeof(resp.message) - 1] = '\0';
    cb(resp);
}

// ---------------------------------------------------------------------------
// Task implementations
// ---------------------------------------------------------------------------

void TaskExecutor::do_arm_home(const TaskCommandPayload& cmd) {
    // Send a power command to bring the arm home (application-specific)
    // This is a placeholder – integrate with actual arm interface as needed.
    Logger::info("TaskExecutor", "ARM_HOME: signalling arm to home position");
    // Example: send PWR_ARM_ON, wait, then signal home via DO command
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    report_result(cmd.task_id, 0, "ARM_HOME sent");
}

void TaskExecutor::do_estop(const TaskCommandPayload& cmd) {
    Logger::fatal("TaskExecutor", "ESTOP triggered!");

    // Turn off all power rails immediately
    PowerCommandPayload pwr{};
    pwr.command  = PWR_ALL_OFF;
    pwr.delay_ms = 0;
    ctx_.power->apply_power_command(pwr);

    report_result(cmd.task_id, 0, "ESTOP executed");
}

void TaskExecutor::do_shutdown_all(const TaskCommandPayload& cmd) {
    Logger::info("TaskExecutor", "SHUTDOWN_ALL");

    PowerCommandPayload pwr{};
    pwr.command  = PWR_ALL_OFF;
    pwr.delay_ms = 0;
    ctx_.power->apply_power_command(pwr);

    report_result(cmd.task_id, 0, "Shutdown initiated");
}

void TaskExecutor::do_valve(const TaskCommandPayload& cmd, bool enable) {
    // arg encodes bank (high byte) and pin mask (low 16 bits)
    uint8_t  bank     = static_cast<uint8_t>((cmd.arg >> 16) & 0xFF);
    uint32_t pin_mask = cmd.arg & 0xFFFFu;

    DoCommandPayload do_cmd{};
    do_cmd.bank       = bank;
    do_cmd.pin_mask   = pin_mask;
    do_cmd.pin_states = enable ? pin_mask : 0u;

    bool ok = ctx_.gpio->apply_do_command(do_cmd);
    Logger::info("TaskExecutor",
        std::string(enable ? "ENABLE" : "DISABLE") + "_VALVE bank=" +
        std::to_string(bank) + " mask=0x" +
        to_hex(pin_mask, 8));

    report_result(cmd.task_id, ok ? 0 : 1,
                  ok ? "Valve command OK" : "Valve command failed");
}

void TaskExecutor::do_custom(const TaskCommandPayload& cmd) {
    Logger::info("TaskExecutor",
        std::string("CUSTOM task: ") +
        std::string(cmd.name, strnlen(cmd.name, sizeof(cmd.name))));
    report_result(cmd.task_id, 0, "Custom task acknowledged");
}
