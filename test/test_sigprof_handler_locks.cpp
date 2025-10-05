#include <bits/types/clockid_t.h>
#include <pthread.h>
#include <execinfo.h>
#include <signal.h>

#include <Python.h>
#include <mutex>

#include <gtest/gtest.h>

// Used functions
void sigprof_handler([[maybe_unused]] int signum);
void init_frame_cache(size_t capacity);

// Used globals
extern std::mutex sigprof_handler_lock;

struct TestSigProfHandlerLocks : public ::testing::Test {
    inline static bool mock_unwind_native_stack_called = false;
    inline static bool mock_unwind_python_stack_called = false;

    void SetUp() override {
        mock_unwind_native_stack_called = false;
        mock_unwind_python_stack_called = false;
    }

    void TearDown() override {
        mock_unwind_native_stack_called = false;
        mock_unwind_python_stack_called = false;
    }
};

void unwind_native_stack() {
    TestSigProfHandlerLocks::mock_unwind_native_stack_called = true;
}

void unwind_python_stack(PyThreadState*) {
    TestSigProfHandlerLocks::mock_unwind_python_stack_called = true;
}

TEST_F(TestSigProfHandlerLocks, UnlocksLock) {
    sigprof_handler_lock.lock();

    sigprof_handler(SIGPROF);

    ASSERT_TRUE(mock_unwind_native_stack_called);
    ASSERT_TRUE(mock_unwind_python_stack_called);

    // Check that it was correctly unlocked
    ASSERT_TRUE(sigprof_handler_lock.try_lock());
    
    // Cleanup
    sigprof_handler_lock.unlock();
}