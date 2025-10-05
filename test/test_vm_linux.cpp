#include <gtest/gtest.h>

#include "util.h"

#include <echion/vm.h>
#include <sys/uio.h>


#if defined PL_LINUX

struct TestVm : public ::testing::Test {
    inline static bool munmap_called = false;
    inline static bool close_called = false;
    inline static bool mmap_called = false;
    inline static bool ftruncate_called = false;
    inline static bool pwritev_called = false;
    inline static bool memcpy_called = false;
    inline static bool mremap_called = false;   
    
    inline static bool ftruncate_failure = false;
    inline static bool pwritev_failure = false;
    inline static bool mmap_failure = false;
    inline static bool mremap_failure = false;

    void reset_calls() {
        munmap_called = false;
        close_called = false;
        mmap_called = false;
        ftruncate_called = false;
        pwritev_called = false;
        memcpy_called = false;
        mremap_called = false;
    }

    void SetUp() override {
        init_safe_copy();

        VmReader::reset();
        reset_calls();
    }

    VmReader* get_instance_value() {
        return VmReader::instance;
    }
};

extern "C" {

namespace {

int munmap(void* addr, size_t len) {
    TestVm::munmap_called = true;
    return real_function<int>("munmap", addr, len);
}

int close(int fd) {
    TestVm::close_called = true;
    return real_function<int>("close", fd);
}

void *mmap(void *__addr, size_t __len, int __prot, int __flags, int __fd, off_t __offset) {
    TestVm::mmap_called = true;
    if (TestVm::mmap_failure) {
        return MAP_FAILED;
    }

    return real_function<void*>("mmap", __addr, __len, __prot, __flags, __fd, __offset);
}

int ftruncate(int fd, off_t __length) {
    TestVm::ftruncate_called = true;

    if (TestVm::ftruncate_failure) {
        return -1;
    }
    
    return real_function<int>("ftruncate", fd, __length);
}

ssize_t pwritev(int fd, const struct iovec *iovec, int count, off_t offset) {
    TestVm::pwritev_called = true;

    if (TestVm::pwritev_failure) {
        return -1;
    }
    
    return real_function<ssize_t>("pwritev", fd, iovec, count, offset);
}

void *memcpy(void *dest, const void *src, size_t n) {
    TestVm::memcpy_called = true;

    return real_function<void*>("memcpy", dest, src, n);
}

void *mremap(void *old_address, size_t old_size, size_t new_size, int flags, ...) {
    TestVm::mremap_called = true;

    if (TestVm::mremap_failure) {
        return MAP_FAILED;
    }

    return real_function<void*>("mremap", old_address, old_size, new_size, flags);
}

} // namespace

} // extern "C"

TEST_F(TestVm, TestSafeCopy) {
    auto inst = VmReader::get_instance();
    ASSERT_NE(inst, nullptr);

    auto t = inst->safe_copy(getpid(), nullptr, 0, nullptr, 0, 0);
    ASSERT_EQ(t, 0);
}

TEST_F(TestVm, TestVmReaderDestructor) {
    auto inst = VmReader::get_instance();
    ASSERT_NE(inst, nullptr);

    VmReader::reset(); // this will call the destructor
    ASSERT_TRUE(TestVm::munmap_called);
    ASSERT_TRUE(TestVm::close_called);

    // The instance should be nullptr
    ASSERT_EQ(get_instance_value(), nullptr);
}

TEST_F(TestVm, TestFailsToConstructIfMmapSucceeds) {
    auto inst = VmReader::get_instance();
    ASSERT_NE(inst, nullptr);

    VmReader::reset(); // this will call the destructor
    ASSERT_TRUE(TestVm::munmap_called);
    ASSERT_TRUE(TestVm::close_called);
}

TEST_F(TestVm, TestFailsToConstructIfMmapFails) {
    TestVm::mmap_failure = true;

    // This should return nullptr but not throw an exception
    auto inst = VmReader::get_instance();
    ASSERT_EQ(inst, nullptr);

    // mmap will have been called (and failed)
    ASSERT_TRUE(TestVm::mmap_called);

    // This should not crash
    VmReader::reset();
    ASSERT_FALSE(TestVm::munmap_called);
    ASSERT_TRUE(TestVm::close_called);
}

TEST_F(TestVm, TestSafeCopyNoResizeSuccess) {
    auto inst = VmReader::get_instance();
    ASSERT_NE(inst, nullptr);

    // get_instance will have called ftruncate, reset the state
    TestVm::ftruncate_called = false;
    TestVm::memcpy_called = false;

    auto remote_buffer = std::make_unique<char[]>(1024);
    auto local_buffer = std::make_unique<char[]>(1024);

    iovec remote_iov = {remote_buffer.get(), 1024};
    iovec local_iov = {local_buffer.get(), 1024};

    auto t = inst->safe_copy(getpid(), &local_iov, 1, &remote_iov, 1, 0);
    // We shouldn't have had to resize the buffer, otherwise the test isn't valid
    ASSERT_FALSE(TestVm::ftruncate_called);

    ASSERT_TRUE(TestVm::pwritev_called);

}

TEST_F(TestVm, TestSafeCopyNoResizeWriteFailure) {
    auto inst = VmReader::get_instance();
    ASSERT_NE(inst, nullptr);

    // get_instance will have called ftruncate, reset the state
    TestVm::ftruncate_called = false;
    TestVm::memcpy_called = false;

    // Set up
    TestVm::pwritev_failure = true;

    // Input data
    auto remote_buffer = std::make_unique<char[]>(1024);
    auto local_buffer = std::make_unique<char[]>(1024);

    iovec remote_iov = {remote_buffer.get(), 1024};
    iovec local_iov = {local_buffer.get(), 1024};

    // Call
    auto t = inst->safe_copy(getpid(), &local_iov, 1, &remote_iov, 1, 0);
    
    // We shouldn't have had to resize the buffer, otherwise the test isn't valid
    ASSERT_FALSE(TestVm::ftruncate_called);

    // We should have called pwritev, but it failed
    ASSERT_TRUE(TestVm::pwritev_called);

    // Since it failed, we should have returned -1
    ASSERT_EQ(t, -1);

    // ... and not memcpy'd anything
    ASSERT_FALSE(TestVm::memcpy_called);        
}

TEST_F(TestVm, TestSafeCopyResizeNeededSuccess) {
    auto inst = VmReader::get_instance();
    ASSERT_NE(inst, nullptr);

    // get_instance will have called ftruncate, reset the state
    reset_calls();

    // Default size is 1024 * 1024, we need to be bigger that that to trigger a resize
    const size_t remote_buffer_size = 1024 * (1024 + 10);
    auto remote_buffer = std::make_unique<char[]>(remote_buffer_size);
    iovec remote_iov = {remote_buffer.get(), remote_buffer_size};

    // Local buffer is 1024 bytes   
    const size_t local_buffer_size = 1024;
    auto local_buffer = std::make_unique<char[]>(local_buffer_size);
    iovec local_iov = {local_buffer.get(), local_buffer_size};
    
    // Call
    auto t = inst->safe_copy(getpid(), &local_iov, 1, &remote_iov, 1, 0);
    
    // Based on the case, we should have called ftruncate
    ASSERT_TRUE(TestVm::ftruncate_called);

    // ftruncate succeeded, we should have continued onto mremap
    ASSERT_TRUE(TestVm::mremap_called);

    // mremap succeeded, we should have continued onto pwritev
    ASSERT_TRUE(TestVm::pwritev_called);

    // And we should have copied the whole buffer
    ASSERT_EQ(t, remote_buffer_size);
}

TEST_F(TestVm, TestSafeCopyResizeNeededTruncateFailure) {
    auto inst = VmReader::get_instance();
    ASSERT_NE(inst, nullptr);

    // Set up
    TestVm::ftruncate_failure = true;

    // get_instance will have called ftruncate, reset the state
    reset_calls();

    // Default size is 1024 * 1024, we need to be bigger that that to trigger a resize
    const size_t remote_buffer_size = 1024 * (1024 + 10);
    auto remote_buffer = std::make_unique<char[]>(remote_buffer_size);
    iovec remote_iov = {remote_buffer.get(), remote_buffer_size};

    // Local buffer is 1024 bytes   
    const size_t local_buffer_size = 1024;
    auto local_buffer = std::make_unique<char[]>(local_buffer_size);
    iovec local_iov = {local_buffer.get(), local_buffer_size};

    // Call
    auto t = inst->safe_copy(getpid(), &local_iov, 1, &remote_iov, 1, 0);

    // Based on the case, we should have called ftruncate
    ASSERT_TRUE(TestVm::ftruncate_called);

    // ftruncate failed, we should not have continued onto mremap
    ASSERT_FALSE(TestVm::mremap_called);

    // We should have returned 0
    ASSERT_EQ(t, 0);

    // ... and not done the next thing (mremap)
    ASSERT_FALSE(TestVm::mremap_called);
}

TEST_F(TestVm, TestSafeCopyResizeNeededMremapFailure) {
    auto inst = VmReader::get_instance();
    ASSERT_NE(inst, nullptr);

    // Set up
    TestVm::mremap_failure = true;

    // get_instance will have called ftruncate, reset the state
    reset_calls();

    // Default size is 1024 * 1024, we need to be bigger that that to trigger a resize
    const size_t remote_buffer_size = 1024 * (1024 + 10);
    auto remote_buffer = std::make_unique<char[]>(remote_buffer_size);
    iovec remote_iov = {remote_buffer.get(), remote_buffer_size};

    // Local buffer is 1024 bytes   
    const size_t local_buffer_size = 1024;
    auto local_buffer = std::make_unique<char[]>(local_buffer_size);
    iovec local_iov = {local_buffer.get(), local_buffer_size};

    // Call
    auto t = inst->safe_copy(getpid(), &local_iov, 1, &remote_iov, 1, 0);

    // Based on the case, we should have called ftruncate
    ASSERT_TRUE(TestVm::ftruncate_called);

    // ftruncate succeeded, we should have continued onto mremap
    ASSERT_TRUE(TestVm::mremap_called);

    // mremap failed, we should not have continued onto pwritev
    ASSERT_FALSE(TestVm::pwritev_called);

    // We should have returned 0
    ASSERT_EQ(t, 0);
}

#endif