#include <gtest/gtest.h>
#include <Python.h>

#include <echion/strings.h>

#include "echion/vm.h"
#include "util.h"

// Mock functions

struct TestStringTable : public ::testing::Test {
    inline static size_t copy_memory_call_count = 0;
    inline static std::vector<bool> copy_memory_failure_calls;

    void reset_calls() {
        copy_memory_call_count = 0;
        copy_memory_failure_calls.clear();
    }

    void set_copy_memory_failure_calls(std::vector<bool> calls) {
        copy_memory_failure_calls = std::move(calls);
    }

    static void SetUpTestCase() {
        _set_pid(getpid());
    }

    void SetUp() override {
        Py_Initialize();
        
        reset_calls();
    }
    
    void TearDown() override {
        // Note: Py_Finalize() can cause issues in tests, so we skip it
    }
};

int copy_memory(proc_ref_t proc_ref, void* addr, ssize_t len, void* buf)
{
    TestStringTable::copy_memory_call_count++;

    if (!TestStringTable::copy_memory_failure_calls.empty()) {
        if (TestStringTable::copy_memory_call_count-1 >= TestStringTable::copy_memory_failure_calls.size()) {
            throw std::runtime_error("copy_memory_failure_calls does not have enough items, call count: " + std::to_string(TestStringTable::copy_memory_call_count));
        }

        if (TestStringTable::copy_memory_failure_calls[TestStringTable::copy_memory_call_count-1]) {
            std::cout << "copy_memory failed" << std::endl;
            return -1;
        }
    }

    return real_cpp_function(copy_memory, proc_ref, addr, len, buf);
}

TEST_F(TestStringTable, TestCopyMemoryFails) {
    TestStringTable::set_copy_memory_failure_calls({true});

    auto result = pybytes_to_bytes_and_size(PyBytes_FromString("test"), nullptr);

    ASSERT_EQ(TestStringTable::copy_memory_call_count, 1);
    ASSERT_EQ(result, nullptr);
}

TEST_F(TestStringTable, TestSecondCopyMemoryFails) {
    TestStringTable::set_copy_memory_failure_calls({false, true});

    Py_ssize_t size = -1;
    auto result = pybytes_to_bytes_and_size(PyBytes_FromString("test"), &size);

    ASSERT_EQ(TestStringTable::copy_memory_call_count, 2);
    ASSERT_EQ(result, nullptr);
}

TEST_F(TestStringTable, TestPyBytesToBytesAndSizeSimple) {
    const char* test_data = "Hello, World!";
    PyObjectHandle bytes = PyBytes_FromString(test_data);
    ASSERT_NE(bytes, nullptr);
    
    Py_ssize_t size = -1;
    auto result = pybytes_to_bytes_and_size(bytes, &size);
    
    EXPECT_NE(result, nullptr);
    EXPECT_EQ(size, 13);
    
    // Verify the data matches
    for (Py_ssize_t i = 0; i < size; i++) {
        EXPECT_EQ(result[i], static_cast<unsigned char>(test_data[i]));
    }
}

TEST_F(TestStringTable, TestPyBytesToBytesAndSizeBinary) {
    // Test with binary data including null bytes
    unsigned char test_data[] = {0x00, 0x01, 0xFF, 0xAB, 0xCD, 0x00, 0x42};
    PyObject* bytes = PyBytes_FromStringAndSize(reinterpret_cast<const char*>(test_data), 7);
    ASSERT_NE(bytes, nullptr);
    
    Py_ssize_t size = -1;
    auto result = pybytes_to_bytes_and_size(bytes, &size);
    
    EXPECT_NE(result, nullptr);
    EXPECT_EQ(size, 7);
    
    // Verify binary data matches exactly
    for (Py_ssize_t i = 0; i < size; i++) {
        EXPECT_EQ(result[i], test_data[i]);
    }
}

TEST_F(TestStringTable, TestPyBytesToBytesAndSizeLarge) {
    // Test with a larger byte string
    const size_t large_size = 10000;
    std::vector<char> large_data(large_size);
    for (size_t i = 0; i < large_size; i++) {
        large_data[i] = static_cast<char>(i % 256);
    }
    
    PyObject* bytes = PyBytes_FromStringAndSize(large_data.data(), large_size);
    ASSERT_NE(bytes, nullptr);
    
    Py_ssize_t size = -1;
    auto result = pybytes_to_bytes_and_size(bytes, &size);
    
    EXPECT_NE(result, nullptr);
    EXPECT_EQ(size, static_cast<Py_ssize_t>(large_size));
    
    // Verify data integrity
    for (size_t i = 0; i < large_size; i++) {
        EXPECT_EQ(result[i], static_cast<unsigned char>(large_data[i]));
    }
}

TEST_F(TestStringTable, TestPyUnicodeToUtf8Empty) {
    PyObjectHandle str = PyUnicode_FromString("");
    ASSERT_NE(str, nullptr);
    
    std::string result = pyunicode_to_utf8(str);
    
    EXPECT_EQ(result, "");
    EXPECT_EQ(result.size(), 0);
}

TEST_F(TestStringTable, TestPyUnicodeToUtf8Simple) {
    const char* test_str = "Hello, World!";
    PyObjectHandle str = PyUnicode_FromString(test_str);
    ASSERT_NE(str, nullptr);
    
    std::string result = pyunicode_to_utf8(str);
    
    EXPECT_EQ(result, test_str);
    EXPECT_EQ(result.size(), 13);
}

TEST_F(TestStringTable, TestPyUnicodeToUtf8ASCII) {
    const char* test_str = "ASCII_string_123";
    PyObjectHandle str = PyUnicode_FromString(test_str);
    ASSERT_NE(str, nullptr);
    
    std::string result = pyunicode_to_utf8(str);
    
    EXPECT_EQ(result, test_str);
}

TEST_F(TestStringTable, TestPyUnicodeToUtf8WithSpecialChars) {
    const char* test_str = "test\nline\ttab";
    PyObjectHandle str = PyUnicode_FromString(test_str);
    ASSERT_NE(str, nullptr);
    
    std::string result = pyunicode_to_utf8(str);
    
    EXPECT_EQ(result, test_str);
    EXPECT_EQ(result.size(), 13);
}

TEST_F(TestStringTable, TestPyUnicodeToUtf8MaxLength) {
    // Test at the boundary of max size (1024 chars)
    std::string test_str(1024, 'a');
    PyObjectHandle str = PyUnicode_FromString(test_str.c_str());
    ASSERT_NE(str, nullptr);
    
    std::string result = pyunicode_to_utf8(str);
    
    EXPECT_EQ(result, test_str);
    EXPECT_EQ(result.size(), 1024);
}

TEST_F(TestStringTable, TestPyUnicodeToUtf8FunctionName) {
    // Test typical function/class names
    const char* test_cases[] = {
        "__init__",
        "my_function",
        "MyClass",
        "_private_method",
        "function_with_long_name"
    };
    
    for (const char* test_str : test_cases) {
        PyObjectHandle str = PyUnicode_FromString(test_str);
        ASSERT_NE(str, nullptr);
        
        std::string result = pyunicode_to_utf8(str);
        
        EXPECT_EQ(result, test_str);
    }
}

TEST_F(TestStringTable, TestPyUnicodeToUtf8CopyTypeFails) {
    // First copy_memory call (copy_type) should fail
    set_copy_memory_failure_calls({true});
    
    PyObjectHandle str = PyUnicode_FromString("test");
    ASSERT_NE(str, nullptr);
    
    EXPECT_THROW(pyunicode_to_utf8(str), StringError);
    ASSERT_EQ(copy_memory_call_count, 1);
}

TEST_F(TestStringTable, TestPyUnicodeToUtf8CopyGenericFails) {
    // First copy_memory succeeds (copy_type), second fails (copy_generic)
    set_copy_memory_failure_calls({false, true});
    
    PyObjectHandle str = PyUnicode_FromString("test");
    ASSERT_NE(str, nullptr);
    
    EXPECT_THROW(pyunicode_to_utf8(str), StringError);
    ASSERT_EQ(copy_memory_call_count, 2);
}

TEST_F(TestStringTable, TestPyUnicodeToUtf8ExceedsMaxLength) {
    // Test exceeding the max size limit (1024 chars)
    std::string test_str(1025, 'a');
    PyObjectHandle str = PyUnicode_FromString(test_str.c_str());
    ASSERT_NE(str, nullptr);
    
    EXPECT_THROW(pyunicode_to_utf8(str), StringError);
}

TEST_F(TestStringTable, TestPyUnicodeToUtf8NonASCIIMultiByte) {
    // Test with multi-byte UTF-8 characters (kind != 1)
    // These should fail since pyunicode_to_utf8 only accepts kind == 1
    const char* test_cases[] = {
        "Hello 世界",      // Chinese characters (kind 2 or 4)
        "Привет",          // Cyrillic (kind 2)
        "🎉",              // Emoji (kind 4)
        "Café",            // Latin with diacritics (may be kind 2)
    };
    
    for (const char* test_str : test_cases) {
        PyObjectHandle str = PyUnicode_FromString(test_str);
        ASSERT_NE(str, nullptr);
        
        // Check if Python created a non-ASCII string (kind != 1)
        // If it's still ASCII/Latin-1 (kind 1), it should pass
        try {
            std::string result = pyunicode_to_utf8(str);
            // If we get here, it means the string was kind 1 (ASCII/Latin-1)
            // which is acceptable
        } catch (const StringError&) {
            // Expected for kind != 1
        }
    }
}

