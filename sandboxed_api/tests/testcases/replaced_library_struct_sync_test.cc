#include "sandboxed_api/tests/testcases/replaced_library_struct_sync.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "gtest/gtest.h"

namespace {

TEST(StructSyncTest, MungeMixedStruct) {
  MixedStruct mixed;
  InitMixedStruct(&mixed);

  EXPECT_EQ(MungeMixedStruct(&mixed), 123 + 0x12345678);

  EXPECT_EQ(MungeMixedStruct(&mixed), 123 + (2 * 0x12345678));
}

TEST(StructSyncTest, RepeatStream) {
  InOutStream stream;
  memset(&stream, 0, sizeof(stream));
  uint8_t input[3] = {'A', 'B', 'C'};
  stream.input = input;
  stream.in_size = 3;
  uint8_t output[6];
  stream.output = output;
  stream.out_size = 6;
  stream.trunc_error_msg = "truncated output!";
  stream.did_truncate_out = false;
  Count count = {0};
  stream.count = &count;
  stream.prev_count = nullptr;

  EXPECT_EQ(RepeatStream(&stream), 0);

  EXPECT_EQ(stream.in_size, 3);
  EXPECT_EQ(stream.out_size, 6);
  EXPECT_EQ(output[0], 'A');
  EXPECT_EQ(output[1], 'A');
  EXPECT_EQ(output[2], 'B');
  EXPECT_EQ(output[3], 'B');
  EXPECT_EQ(output[4], 'C');
  EXPECT_EQ(output[5], 'C');
  EXPECT_FALSE(stream.did_truncate_out);
  EXPECT_EQ(stream.count->count, 1);

  // Test truncation
  uint8_t output2[5];
  stream.output = output2;
  stream.out_size = 5;
  stream.did_truncate_out = false;

  EXPECT_EQ(RepeatStream(&stream), 0);

  EXPECT_EQ(stream.in_size, 3);
  EXPECT_EQ(stream.out_size, 5);
  EXPECT_EQ(output2[0], 'A');
  EXPECT_EQ(output2[1], 'A');
  EXPECT_EQ(output2[2], 'B');
  EXPECT_EQ(output2[3], 'B');
  EXPECT_EQ(output2[4], 'C');
  EXPECT_TRUE(stream.did_truncate_out);
  EXPECT_EQ(stream.count->count, 2);

  ClearStream(&stream);
}

TEST(StructSyncTest, CreateImage) {
  const size_t width = 3;
  const size_t height = 3;
  const size_t len = width * height;
  unsigned char data[9] = {// dark
                           2, 1, 0,
                           // medium
                           127, 126, 125,
                           // light
                           255, 254, 253};
  Span span = {data, len};
  Image* image = CreateImage(&span, width, height);
  EXPECT_NE(image, nullptr);

  for (size_t row = 0; row < height; ++row) {
    const unsigned char* row_data = GetRow(image, row);
    for (size_t col = 0; col < width; ++col) {
      EXPECT_EQ(row_data[col], data[row * width + col]);
    }
  }

  DeleteImage(image);
}

TEST(StructSyncTest, NullSyncAccessPath) {
  EXPECT_EQ(RepeatStream(nullptr), -1);

  EXPECT_EQ(CreateImage(nullptr, 3, 3), nullptr);

  Span span = {nullptr, 0};
  EXPECT_EQ(CreateImage(&span, 3, 3), nullptr);
}

}  // namespace
