#include <gtest/gtest.h>

#include <echion/frame.h>


struct TestFrame : public ::testing::Test {
    void SetUp() override {
        // do nothing for now
    }
};

#if PY_VERSION_HEX >= 0x030b0000

extern int _read_varint(unsigned char* table, ssize_t size, ssize_t* i);
extern int _read_signed_varint(unsigned char* table, ssize_t size, ssize_t* i);

TEST_F(TestFrame, TestReadVarIntSingleByte) {
    // Test single-byte varint (no continuation bit set)
    // Values 0-63 can be encoded in a single byte
    unsigned char table[] = {0xFF, 0x00, 0xFF};  // First 0xFF is padding
    ssize_t i = 0;
    
    EXPECT_EQ(_read_varint(table, 3, &i), 0);
    EXPECT_EQ(i, 1);
    
    // Test value 63 (max single byte: 0b00111111)
    unsigned char table2[] = {0xFF, 63, 0xFF};
    i = 0;
    EXPECT_EQ(_read_varint(table2, 3, &i), 63);
    EXPECT_EQ(i, 1);
}

TEST_F(TestFrame, TestReadVarIntMultiByte) {
    // Test multi-byte varint with continuation bit
    // Continuation bit is bit 6 (0x40)
    // Example: 64 = 0b01000000 in first byte (0x40), then 0x01 in next
    unsigned char table[] = {0xFF, 0x40 | 0, 1};  // 64: continuation bit + 0, then 1 shifted by 6
    ssize_t i = 0;
    
    EXPECT_EQ(_read_varint(table, 3, &i), 64);
    EXPECT_EQ(i, 2);
    
    // Test larger value: 128 = 0b10000000
    // First byte: 0x40 | 0 (continuation + data=0)
    // Second byte: 2 (shifted left 6 bits = 128)
    unsigned char table2[] = {0xFF, 0x40 | 0, 2};
    i = 0;
    EXPECT_EQ(_read_varint(table2, 3, &i), 128);
    
    // Test 127 = 0b01111111
    // First byte: 0x40 | 63 (continuation + 63)
    // Second byte: 1 (1 << 6 = 64, plus 63 = 127)
    unsigned char table3[] = {0xFF, 0x40 | 63, 1};
    i = 0;
    EXPECT_EQ(_read_varint(table3, 3, &i), 63 + 64);
}

TEST_F(TestFrame, TestReadVarIntThreeByte) {
    // Test three-byte varint
    // Value: 4095 = 63 + (63 << 6) + (1 << 12)
    unsigned char table[] = {0xFF, 0x40 | 63, 0x40 | 63, 1};
    ssize_t i = 0;
    
    EXPECT_EQ(_read_varint(table, 4, &i), 63 + (63 << 6) + (1 << 12));
    EXPECT_EQ(i, 3);
}

TEST_F(TestFrame, TestReadVarIntBoundary) {
    // Test boundary condition: at guard
    unsigned char table[] = {0xFF, 0x00};
    ssize_t i = 0;
    
    EXPECT_EQ(_read_varint(table, 2, &i), 0);
    
    // Test beyond boundary
    i = 0;
    EXPECT_EQ(_read_varint(table, 1, &i), 0);
    EXPECT_EQ(i, 0);  // Should not advance
}

TEST_F(TestFrame, TestReadSignedVarIntPositive) {
    // Positive numbers: even encoded values
    // 0 -> 0, 2 -> 1, 4 -> 2, etc.
    
    // Test 0
    unsigned char table[] = {0xFF, 0};
    ssize_t i = 0;
    EXPECT_EQ(_read_signed_varint(table, 2, &i), 0);
    
    // Test 1: encoded as 2
    unsigned char table2[] = {0xFF, 2};
    i = 0;
    EXPECT_EQ(_read_signed_varint(table2, 2, &i), 1);
    
    // Test 10: encoded as 20
    unsigned char table3[] = {0xFF, 20};
    i = 0;
    EXPECT_EQ(_read_signed_varint(table3, 2, &i), 10);
}

TEST_F(TestFrame, DISABLED_TestReadSignedVarIntNegative) {
    // Negative numbers: odd encoded values
    // 1 -> -1, 3 -> -2, 5 -> -3, etc.
    
    // Test -1: encoded as 1
    unsigned char table[] = {0xFF, 1};
    ssize_t i = 0;
    EXPECT_EQ(_read_signed_varint(table, 2, &i), -1);
    
    // Test -2: encoded as 3
    unsigned char table2[] = {0xFF, 3};
    i = 0;
    EXPECT_EQ(_read_signed_varint(table2, 2, &i), -2);
    
    // Test -10: encoded as 19
    unsigned char table3[] = {0xFF, 19};
    i = 0;
    EXPECT_EQ(_read_signed_varint(table3, 2, &i), -10);
}

TEST_F(TestFrame, DISABLED_TestReadSignedVarIntMultiByte) {
    // Test signed varint with multi-byte encoding
    // -64: encoded as 127 (zigzag), which needs multi-byte
    // 127 = 63 + 64, so 0x40 | 63, then 1
    unsigned char table[] = {0xFF, 0x40 | 63, 1};
    ssize_t i = 0;
    EXPECT_EQ(_read_signed_varint(table, 3, &i), -64);
    
    // Test 64: encoded as 128
    // 128 = 0x40 | 0, then 2
    unsigned char table2[] = {0xFF, 0x40 | 0, 2};
    i = 0;
    EXPECT_EQ(_read_signed_varint(table2, 3, &i), 64);
}
#endif

