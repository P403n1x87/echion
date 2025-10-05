#include <gtest/gtest.h>

#include <echion/timing.h>

struct TestTiming : public ::testing::Test {
    inline static bool clock_gettime_success = false;
    inline static std::optional<struct timespec> clock_gettime_result;

    inline static bool clock_gettime_called = false;

    void clock_gettime_result_success(struct timespec ts) {
        clock_gettime_success = true;
        clock_gettime_result = ts;
    }

    void clock_gettime_result_failure() {
        clock_gettime_success = false;
        clock_gettime_result = std::nullopt;
    }

    void SetUp() override {
        clock_gettime_success = false;
        clock_gettime_result = std::nullopt;
        clock_gettime_called = false;
    }

    void TearDown() override {
        clock_gettime_success = false;
        clock_gettime_result = std::nullopt;
        clock_gettime_called = false;
    }
};

namespace {

extern "C" {
    int clock_gettime(clockid_t clockid, struct timespec* ts) {
        TestTiming::clock_gettime_called = true;

        if (!TestTiming::clock_gettime_success) {
            return 123;
        }

        *ts = TestTiming::clock_gettime_result.value();
        return 0;
    }
}

}

TEST_F(TestTiming, GetTimeReturnsTimeIfClockGettimeSucceeds) {
    clock_gettime_result_success({1, 2});

    auto t = gettime();
    // gettime returns an integer number of microseconds
    ASSERT_EQ(t, 1000000);
    ASSERT_TRUE(clock_gettime_called);
}

TEST_F(TestTiming, GetTimeReturnsZeroIfClockGettimeFails) {
    clock_gettime_result_failure();

    // gettime returns 0 if clock_gettime fails
    ASSERT_EQ(gettime(), 0);
    ASSERT_TRUE(clock_gettime_called);
}

TEST_F(TestTiming, TestTsToMicrosecond) {
    {
        struct timespec ts = {1, 0};
        ASSERT_DOUBLE_EQ(TS_TO_MICROSECOND(ts), 1000000.0);
    }
    

    {
        struct timespec ts = {1, 2};
        ASSERT_DOUBLE_EQ(TS_TO_MICROSECOND(ts), 1000000.002);
    }

    {
        struct timespec ts = {1, 200};
        ASSERT_DOUBLE_EQ(TS_TO_MICROSECOND(ts), 1000000.2);
    }

}

#if defined PL_DARWIN
TEST_F(TestTiming, TestTvToMicrosecond) {
    struct timeval tv = {1, 200};
    ASSERT_EQ(TV_TO_MICROSECOND(tv), 100200);
}
#endif