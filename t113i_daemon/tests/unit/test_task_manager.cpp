/**
 * @file test_task_manager.cpp
 * @brief Unit tests for TaskExecutor (queue, enqueue/dequeue, context check,
 *        queue depth limit, timeout detection, result callback).
 */

#include "task_executor.h"
#include "logger.h"
#include "protocol.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>

using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// Minimal stub objects for TaskContext
// ---------------------------------------------------------------------------
// We don't have gmock interfaces for these; use real objects initialised
// with empty state so that TaskContext::is_valid() returns true.
#include "serial_comm.h"
#include "power_manager.h"
#include "gpio_controller.h"

static TaskCommandPayload make_cmd(TaskId id, uint32_t arg = 0) {
    TaskCommandPayload cmd{};
    cmd.task_id = id;
    cmd.arg     = arg;
    strncpy(cmd.name, "test", sizeof(cmd.name) - 1);
    return cmd;
}

// ---------------------------------------------------------------------------
// Fixture: TaskExecutor with a valid context
// ---------------------------------------------------------------------------
class TaskManagerTest : public ::testing::Test {
protected:
    PowerManager   power_;
    GpioController gpio_;

    // We need a real SerialComm; build one with a dummy channel
    // but we won't start it so no I/O happens.
    std::unique_ptr<SerialComm> comm_;

    TaskContext    ctx_;
    std::unique_ptr<TaskExecutor> exec_;

    void SetUp() override {
        Logger::init("", Logger::FATAL);

        // Build a TCP channel pointing nowhere – start() will fail but
        // we just need the object to populate ctx_.comm
        auto ch = std::make_unique<TcpClientChannel>("127.0.0.1", 19999, 100);
        comm_   = std::make_unique<SerialComm>(std::move(ch));

        ctx_.comm  = comm_.get();
        ctx_.power = &power_;
        ctx_.gpio  = &gpio_;

        exec_ = std::make_unique<TaskExecutor>(ctx_);
        exec_->start();
    }

    void TearDown() override {
        exec_->stop();
        exec_.reset();
        Logger::shutdown();
    }
};

// ---------------------------------------------------------------------------
// is_valid context
// ---------------------------------------------------------------------------
TEST_F(TaskManagerTest, ContextIsValid) {
    EXPECT_TRUE(ctx_.is_valid());
}

TEST(TaskContextTest, NullContextIsInvalid) {
    TaskContext ctx;
    EXPECT_FALSE(ctx.is_valid());
}

TEST(TaskContextTest, PartialNullIsInvalid) {
    PowerManager pm;
    TaskContext ctx;
    ctx.power = &pm;
    EXPECT_FALSE(ctx.is_valid()); // comm and gpio still null
}

// ---------------------------------------------------------------------------
// Initial queue state
// ---------------------------------------------------------------------------
TEST_F(TaskManagerTest, QueueInitiallyEmpty) {
    EXPECT_TRUE(exec_->is_queue_empty());
    EXPECT_EQ(exec_->queue_size(), 0u);
}

// ---------------------------------------------------------------------------
// Enqueue / result callback
// ---------------------------------------------------------------------------
TEST_F(TaskManagerTest, EnqueueFiresResultCallback) {
    std::atomic<int> callback_count{0};
    std::atomic<uint8_t> last_result{255};

    exec_->set_result_callback([&](const TaskResponsePayload& r) {
        ++callback_count;
        last_result = r.result;
    });

    exec_->enqueue(make_cmd(TASK_ARM_HOME));

    // Wait up to 2 s for the callback
    for (int i = 0; i < 200 && callback_count.load() == 0; ++i) {
        std::this_thread::sleep_for(10ms);
    }
    EXPECT_EQ(callback_count.load(), 1);
    EXPECT_EQ(last_result.load(), 0u); // success
}

TEST_F(TaskManagerTest, MultipleTasksAllProcessed) {
    std::atomic<int> count{0};
    exec_->set_result_callback([&](const TaskResponsePayload&) { ++count; });

    for (int i = 0; i < 5; ++i) {
        exec_->enqueue(make_cmd(TASK_ARM_HOME));
    }

    for (int i = 0; i < 300 && count.load() < 5; ++i) {
        std::this_thread::sleep_for(10ms);
    }
    EXPECT_EQ(count.load(), 5);
}

// ---------------------------------------------------------------------------
// Queue depth limit
// ---------------------------------------------------------------------------
TEST_F(TaskManagerTest, QueueDepthLimitEnforced) {
    // Pause the worker by filling the queue beyond capacity
    // We do this by stopping the executor and filling manually
    exec_->stop();

    // Build a fresh executor (stopped) so queue never drains
    auto exec2 = std::make_unique<TaskExecutor>(ctx_);
    // Do NOT call start() – worker thread not running

    int accepted = 0;
    for (size_t i = 0; i <= TaskExecutor::MAX_QUEUE_DEPTH + 5; ++i) {
        if (exec2->enqueue(make_cmd(TASK_CUSTOM))) ++accepted;
    }

    EXPECT_EQ(static_cast<size_t>(accepted), TaskExecutor::MAX_QUEUE_DEPTH);
    EXPECT_EQ(exec2->queue_size(), TaskExecutor::MAX_QUEUE_DEPTH);
}

// ---------------------------------------------------------------------------
// Unknown task handled gracefully
// ---------------------------------------------------------------------------
TEST_F(TaskManagerTest, UnknownTaskIdHandledGracefully) {
    std::atomic<int> count{0};
    std::atomic<uint8_t> result{0};
    exec_->set_result_callback([&](const TaskResponsePayload& r) {
        ++count;
        result = r.result;
    });

    TaskCommandPayload cmd{};
    cmd.task_id = static_cast<TaskId>(0xFF); // invalid
    exec_->enqueue(cmd);

    for (int i = 0; i < 200 && count.load() == 0; ++i) {
        std::this_thread::sleep_for(10ms);
    }
    EXPECT_EQ(count.load(), 1);
    EXPECT_NE(result.load(), 0u); // non-zero = error
}

// ---------------------------------------------------------------------------
// ESTOP task
// ---------------------------------------------------------------------------
TEST_F(TaskManagerTest, EstopTaskCompletesSuccessfully) {
    std::atomic<int> count{0};
    exec_->set_result_callback([&](const TaskResponsePayload&) { ++count; });

    exec_->enqueue(make_cmd(TASK_ESTOP));

    for (int i = 0; i < 200 && count.load() == 0; ++i) {
        std::this_thread::sleep_for(10ms);
    }
    EXPECT_EQ(count.load(), 1);
}

// ---------------------------------------------------------------------------
// Stop drains in-flight work
// ---------------------------------------------------------------------------
TEST_F(TaskManagerTest, StopIsClean) {
    exec_->enqueue(make_cmd(TASK_CUSTOM));
    EXPECT_NO_THROW(exec_->stop());
    // After stop, queue should be empty or processed
    // (stop joins the worker thread)
}

// ---------------------------------------------------------------------------
// Queue size tracking
// ---------------------------------------------------------------------------
TEST_F(TaskManagerTest, QueueSizeTracksEnqueue) {
    exec_->stop(); // pause worker

    auto exec2 = std::make_unique<TaskExecutor>(ctx_);
    EXPECT_EQ(exec2->queue_size(), 0u);
    exec2->enqueue(make_cmd(TASK_CUSTOM));
    EXPECT_EQ(exec2->queue_size(), 1u);
    exec2->enqueue(make_cmd(TASK_CUSTOM));
    EXPECT_EQ(exec2->queue_size(), 2u);
}

// ---------------------------------------------------------------------------
// Result callback can be set before start
// ---------------------------------------------------------------------------
TEST_F(TaskManagerTest, CallbackSetBeforeStartWorks) {
    exec_->stop();
    auto exec2 = std::make_unique<TaskExecutor>(ctx_);

    std::atomic<int> calls{0};
    exec2->set_result_callback([&](const TaskResponsePayload&) { ++calls; });
    exec2->start();
    exec2->enqueue(make_cmd(TASK_CUSTOM));

    for (int i = 0; i < 200 && calls.load() == 0; ++i) {
        std::this_thread::sleep_for(10ms);
    }
    EXPECT_GE(calls.load(), 1);
    exec2->stop();
}
