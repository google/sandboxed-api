#include "sandboxed_api/tests/testcases/replaced_library_context_bound.h"

#include <string>

#include "gtest/gtest.h"
#include "sandboxed_api/tests/testcases/replaced_library_context_bound_struct.h"

namespace {

TEST(Test, ContextWithSandboxOwnedBuffer) {
  ContextWithSandboxOwnedBuffer* context = create_context_with_sb_buffer();

  EXPECT_EQ(std::string(get_buff_inline(context)), "hello");
  EXPECT_EQ(std::string(get_buff_null_terminated(context)), "world");

  to_upper_context_sb_buffers(context);
  EXPECT_EQ(std::string(get_buff_inline(context)), "HELLO");
  EXPECT_EQ(std::string(get_buff_null_terminated(context)), "WORLD");

  destroy_context_with_sb_buffer(context);
}

TEST(Test, ContextWithSizedByParams) {
  ContextWithSizedByParams* context = create_context_sized_by_params(3, 4);

  EXPECT_EQ(std::string(get_buff_sized_by_params(context), 3 * 4),
            std::string(3 * 4, '\0'));

  fill_context_sized_by_params(context, 'x');
  EXPECT_EQ(std::string(get_buff_sized_by_params(context), 3 * 4),
            std::string(3 * 4, 'x'));
  destroy_context_sized_by_params(context);
}

TEST(Test, ContextWithSizedAfterDecoding) {
  char data_to_decode[] = {2, 3, 'a', 'b', 'c', 'd', 'e', 'f'};
  ContextWithSizedAfterDecoding* context = create_context_sized_after_decoding(
      data_to_decode, sizeof(data_to_decode));
  Dimensions dimensions;
  get_dimensions_sized_after_decoding(context, &dimensions);
  EXPECT_EQ(dimensions.width, 2);
  EXPECT_EQ(dimensions.height, 3);

  const char* out_buffer_ret = get_buff_sized_after_decoding(context);
  EXPECT_EQ(std::string(out_buffer_ret, dimensions.width * dimensions.height),
            "abcdef");
  char* out_buffer_outparam = nullptr;
  get_buff_sized_after_decoding_outparam(context, &out_buffer_outparam);
  // NOTE: we know the content of "get_buff_sized_after_decoding"
  // vs "get_buff_sized_after_decoding_outparam" will be the same.
  // However, we don't necessarily make the pointers alias.
  EXPECT_EQ(
      std::string(out_buffer_outparam, dimensions.width * dimensions.height),
      "abcdef");

  increment_buff_sized_after_decoding(context);

  out_buffer_ret = get_buff_sized_after_decoding(context);
  EXPECT_EQ(std::string(out_buffer_ret, dimensions.width * dimensions.height),
            "bcdefg");
  get_buff_sized_after_decoding_outparam(context, &out_buffer_outparam);
  EXPECT_EQ(
      std::string(out_buffer_outparam, dimensions.width * dimensions.height),
      "bcdefg");

  destroy_context_sized_after_decoding(context);
}

TEST(Test, ContextWithSizedAfterDecodingUnsignedIntBuff) {
  char data_to_decode[] = {4,   3,   '1', '2', '3', '4', '5',
                           '6', '7', '8', '9', 'a', 'b', 'c'};
  ContextWithSizedAfterDecoding* context = create_context_sized_after_decoding(
      data_to_decode, sizeof(data_to_decode));
  Dimensions dimensions;
  get_dimensions_sized_after_decoding(context, &dimensions);
  EXPECT_EQ(dimensions.width, 4);
  EXPECT_EQ(dimensions.height, 3);

  unsigned int* out_buffer_outparam = nullptr;
  get_unsigned_int_buff_sized_after_decoding_outparam(context,
                                                      &out_buffer_outparam);
  EXPECT_EQ(out_buffer_outparam[0], 0x80000000);
  EXPECT_EQ(out_buffer_outparam[1], 0x80000001);
  EXPECT_EQ(out_buffer_outparam[2], 0x80000002);

  // Null outparam should not crash.
  get_unsigned_int_buff_sized_after_decoding_outparam(context, nullptr);

  destroy_context_sized_after_decoding(context);
}

TEST(Test, NullContextsNullReturns) {
  EXPECT_EQ(get_buff_inline(nullptr), nullptr);
  EXPECT_EQ(get_buff_null_terminated(nullptr), nullptr);
  EXPECT_EQ(get_buff_sized_by_params(nullptr), nullptr);
  EXPECT_EQ(get_buff_sized_after_decoding(nullptr), nullptr);

  char data_to_decode_too_short[] = {1};
  ContextWithSizedAfterDecoding* context_too_short =
      create_context_sized_after_decoding(data_to_decode_too_short,
                                          sizeof(data_to_decode_too_short));
  EXPECT_EQ(context_too_short, nullptr);

  Dimensions dimensions;
  get_dimensions_sized_after_decoding(context_too_short, &dimensions);
  EXPECT_EQ(dimensions.width, 0);
  EXPECT_EQ(dimensions.height, 0);

  destroy_context_sized_after_decoding(context_too_short);
}

TEST(Test, MultipleContextsLive) {
  char data_to_decode1[] = {2, 3, 'a', 'b', 'c', 'd', 'e', 'f'};
  ContextWithSizedAfterDecoding* context1 = create_context_sized_after_decoding(
      data_to_decode1, sizeof(data_to_decode1));
  char data_to_decode2[] = {3, 3, 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i'};
  ContextWithSizedAfterDecoding* context2 = create_context_sized_after_decoding(
      data_to_decode2, sizeof(data_to_decode2));

  Dimensions dimensions;
  get_dimensions_sized_after_decoding(context1, &dimensions);
  EXPECT_EQ(dimensions.width, 2);
  EXPECT_EQ(dimensions.height, 3);

  EXPECT_EQ(std::string(get_buff_sized_after_decoding(context1),
                        dimensions.width * dimensions.height),
            "abcdef");

  get_dimensions_sized_after_decoding(context2, &dimensions);
  EXPECT_EQ(dimensions.width, 3);
  EXPECT_EQ(dimensions.height, 3);

  EXPECT_EQ(std::string(get_buff_sized_after_decoding(context2),
                        dimensions.width * dimensions.height),
            "abcdefghi");

  destroy_context_sized_after_decoding(context1);
  destroy_context_sized_after_decoding(context2);
}

}  // namespace
