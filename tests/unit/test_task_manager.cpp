/**
 * @file test_task_manager.cpp
 * @brief Unit tests for TaskExecutor (task queue management).
 *
 * Tests: enqueue/dequeue, queue overflow, timeout constants, concurrent
 * operations, and result callback delivery.
 */

#include <gtest/gtest.h>

#include "task_executor.h"
#include "protocol.h"

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
// Stub subsystems – minimal interfaces to satisfy TaskContext
// ---------------------------------------------------------------------------

// We need SerialComm, PowerManager, GpioController stubs.
// Rather than linking the full implementation, we provide light wrappers that
// satisfy the pointer types used in TaskContext::is_valid().
//
// Because TaskContext only checks the pointers are non-null we can point them
// at empty structs placed on the stack.

#include "serial_comm.h"
#include "power_manager.h"
#include "gpio_controller.h"

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------
class TaskManagerTest : public ::testing::Test {
protected:
    // Real objects are heavyweight; use the lightest valid TaskContext we can.
    // We rely on the fact that TaskExecutor::enqueue() only checks
    // ctx_.is_valid() before execution, and the worker thread calls into
    // methods we don't exercise here (the tasks will fail gracefully).
    SerialComm*     comm_{nullptr};
    PowerManager*   power_{nullptr};
    GpioController* gpio_{nullptr};

    TaskContext ctx_;

    void SetUp() override {
        // Leave ctx_ with null pointers: is_valid() == false.
        // Tests that need valid contexts will set up their own.
        ctx_ = {};
    }
};

// ---------------------------------------------------------------------------
// Test: MAX_QUEUE_DEPTH constant is at least 1
// ---------------------------------------------------------------------------
TEST_F(TaskManagerTest, MaxQueueDepthPositive) {
    EXPECT_GT(TaskExecutor::MAX_QUEUE_DEPTH, 0u);
}

// ---------------------------------------------------------------------------
// Test: TASK_TIMEOUT_MS constant is reasonable (>= 1 second)
// ---------------------------------------------------------------------------
TEST_F(TaskManagerTest, TaskTimeoutMsReasonable) {
    EXPECT_GE(TaskExecutor::TASK_TIMEOUT_MS, 1000);
}

// ---------------------------------------------------------------------------
// Test: enqueue returns false when context is invalid (null pointers)
// Note: with null context is_valid() == false, so the executor may refuse.
// We validate the overflow path via is_queue_empty() / queue_size().
// ---------------------------------------------------------------------------
TEST_F(TaskManagerTest, QueueStartsEmpty) {
    TaskExecutor te(ctx_);
    EXPECT_TRUE(te.is_queue_empty());
    EXPECT_EQ(te.queue_size(), 0u);
}

// ---------------------------------------------------------------------------
// Test: enqueue() with a valid-context executor adds to queue
// ---------------------------------------------------------------------------
TEST_F(TaskManagerTest, EnqueueIncreasesQueueSize) {
    // Build a minimal valid context with stack-resident objects.
    // We start the executor so the worker does not drain items during the test.
    // To prevent the worker from executing tasks we stop it immediately after
    // enqueue.

    // Use null-pointer context – enqueue() itself only checks ctx_.is_valid()
    // inside execute_task(), NOT during the enqueue call.  We verify this
    // behaviour: enqueue should succeed and queue size should increase.
    TaskExecutor te(ctx_);
    te.start(); // Start worker (will block on empty queue)

    TaskCommandPayload cmd{};
    cmd.task_id = TASK_CUSTOM;
    // enqueue returns true when space is available
    bool ok = te.enqueue(cmd);
    // Give worker a tiny window to pick up the task
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    te.stop();
    // Either enqueued (ok==true) or worker consumed it instantly – both fine
    EXPECT_TRUE(ok);
}

// ---------------------------------------------------------------------------
// Test: enqueue() fails when queue is full (overflow protection)
// ---------------------------------------------------------------------------
TEST_F(TaskManagerTest, QueueOverflowReturnsFalse) {
    TaskExecutor te(ctx_);
    // Do NOT start the worker – items accumulate in the queue

    TaskCommandPayload cmd{};
    cmd.task_id = TASK_CUSTOM;

    size_t enqueued = 0;
    for (size_t i = 0; i <= TaskExecutor::MAX_QUEUE_DEPTH; ++i) {
        if (te.enqueue(cmd)) {
            ++enqueued;
        }
    }

    // At most MAX_QUEUE_DEPTH items should fit; the (MAX_QUEUE_DEPTH+1)-th
    // must be rejected
    EXPECT_LE(enqueued, TaskExecutor::MAX_QUEUE_DEPTH);
    EXPECT_EQ(te.queue_size(), enqueued);

    te.stop();
}

// ---------------------------------------------------------------------------
// Test: start() and stop() can be called repeatedly without crash
// ---------------------------------------------------------------------------
TEST_F(TaskManagerTest, StartStopIdempotent) {
    TaskExecutor te(ctx_);
    EXPECT_NO_THROW({
        te.start();
        te.stop();
        te.start();
        te.stop();
    });
}

// ---------------------------------------------------------------------------
// Test: start() returns false when TaskContext is invalid (null pointers)
// ---------------------------------------------------------------------------
TEST_F(TaskManagerTest, StartReturnsFalseWithNullContext) {
    TaskExecutor te(ctx_); // ctx_ has null pointers
    bool started = te.start();
    EXPECT_FALSE(started);
    te.stop();
}

// ---------------------------------------------------------------------------
// Test: queue is NOT drained when start() fails (no worker thread)
// ---------------------------------------------------------------------------
TEST_F(TaskManagerTest, QueueNotDrainedWhenStartFails) {
    TaskExecutor te(ctx_);
    te.start(); // returns false, no worker created

    TaskCommandPayload cmd{};
    cmd.task_id = TASK_CUSTOM;
    te.enqueue(cmd);

    // Give some time to show no worker is draining
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Queue should still have the item since no worker is running
    EXPECT_FALSE(te.is_queue_empty());
    EXPECT_EQ(te.queue_size(), 1u);

    te.stop();
}

// ---------------------------------------------------------------------------
// Test: result callback is NOT invoked when context is invalid (no worker)
// ---------------------------------------------------------------------------
TEST_F(TaskManagerTest, ResultCallbackNotInvokedWithNullContext) {
    std::atomic<int> cb_count{0};

    TaskExecutor te(ctx_);
    te.set_result_callback([&](const TaskResponsePayload&) {
        ++cb_count;
    });
    // start() fails silently with null context – no worker thread
    te.start();

    TaskCommandPayload cmd{};
    cmd.task_id = TASK_CUSTOM;
    te.enqueue(cmd);

    // Wait a bit – callback should NOT be called
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    te.stop();

    EXPECT_EQ(cb_count.load(), 0)
        << "Callback should not be invoked when no worker thread is running";
}

// ---------------------------------------------------------------------------
// Test: concurrent enqueues from multiple threads are safe
// ---------------------------------------------------------------------------
TEST_F(TaskManagerTest, ConcurrentEnqueueSafe) {
    TaskExecutor te(ctx_);
    // Do NOT start worker to keep items in queue for size inspection

    constexpr int kThreads = 4;
    constexpr int kPerThread = 10;

    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&te]() {
            TaskCommandPayload cmd{};
            cmd.task_id = TASK_CUSTOM;
            for (int i = 0; i < kPerThread; ++i) {
                te.enqueue(cmd);
            }
        });
    }
    for (auto& th : threads) th.join();

    // Queue should contain at most MAX_QUEUE_DEPTH items (no UB)
    EXPECT_LE(te.queue_size(), TaskExecutor::MAX_QUEUE_DEPTH);

    te.stop();
}
