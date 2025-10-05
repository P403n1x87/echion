#include <optional>
#include <bits/types/clockid_t.h>
#include <pthread.h>
#include <execinfo.h>

#include <gtest/gtest.h>

#include <Python.h>

#include <echion/threads.h>

struct TestThreadInfo : public ::testing::Test {
    inline static bool pthread_getcpuclockid_success = false;
    inline static std::optional<clockid_t> pthread_getcpuclockid_result;

    inline static bool clock_gettime_success = false;
    inline static std::optional<struct timespec> clock_gettime_result;

    inline static bool pthread_getcpuclockid_called = false;
    inline static bool clock_gettime_called = false;

    void pthread_getcpuclockid_result_success(clockid_t clockid) {
        pthread_getcpuclockid_success = true;
        pthread_getcpuclockid_result = clockid;
    }

    void clock_gettime_result_success(struct timespec ts) {
        clock_gettime_success = true;
        clock_gettime_result = ts;
    }

    void pthread_getcpuclockid_result_failure() {
        pthread_getcpuclockid_success = false;
        pthread_getcpuclockid_result = std::nullopt;
    }

    void clock_gettime_result_failure() {
        clock_gettime_success = false;
        clock_gettime_result = std::nullopt;
    }

    void SetUp() override {
        pthread_getcpuclockid_success = false;
        pthread_getcpuclockid_result = std::nullopt;

        clock_gettime_success = false;
        clock_gettime_result = std::nullopt;

        pthread_getcpuclockid_called = false;
        clock_gettime_called = false;
    }

    void TearDown() override {
        pthread_getcpuclockid_success = false;
        pthread_getcpuclockid_result = std::nullopt;

        clock_gettime_success = false;
        clock_gettime_result = std::nullopt;
    }
};

extern "C" {
    namespace {
        int pthread_getcpuclockid(pthread_t a, clockid_t* clockid) {
            TestThreadInfo::pthread_getcpuclockid_called = true;

            if (!TestThreadInfo::pthread_getcpuclockid_success) {
                return 123;
            }

            *clockid = TestThreadInfo::pthread_getcpuclockid_result.value();
            return 0;
        }

        /* Note: mocking this function leads to nonsensical timing in test output. We cannot do anything about it. */
        int clock_gettime(clockid_t clockid, struct timespec* ts) {
            TestThreadInfo::clock_gettime_called = true;

            if (!TestThreadInfo::clock_gettime_success) {
                return 123;
            }

            *ts = TestThreadInfo::clock_gettime_result.value();
            return 0;
        }
    }
}

TEST_F(TestThreadInfo, ConstructorDoesNotThrowIfGetCpuClockIdSucceeds) {
    pthread_getcpuclockid_result_success(123);
    clock_gettime_result_success({1, 2});

    auto thread_info = ThreadInfo(1, 1, "test");
    ASSERT_EQ(thread_info.cpu_clock_id, 123);
    ASSERT_TRUE(pthread_getcpuclockid_called);
}


TEST_F(TestThreadInfo, ThrowsIfGetCpuClockIdFails) {
    clock_gettime_result_success({1, 2});
    pthread_getcpuclockid_result_failure();

    ASSERT_THROW(ThreadInfo(1, 1, "test"), ThreadInfo::Error);
    ASSERT_TRUE(pthread_getcpuclockid_called);
}

TEST_F(TestThreadInfo, UpdateCpuTimeThrowsIfClockGettimeFails) {
    pthread_getcpuclockid_result_success(123);
    clock_gettime_result_failure();

    ASSERT_THROW(ThreadInfo(1, 1, "test"), ThreadInfo::CpuTimeError);
    ASSERT_TRUE(pthread_getcpuclockid_called);
    ASSERT_TRUE(clock_gettime_called);
}