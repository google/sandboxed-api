#include "sandboxed_api/tests/testcases/replaced_library_context_bound.h"

#include <cstdlib>
#include <cstring>

#include "sandboxed_api/tests/testcases/replaced_library_context_bound_struct.h"
#include "sandboxed_api/tests/testcases/replaced_library_context_bound_struct_private.h"

extern "C" {

////////////////////////////////////////////////////////////////////////////////
// Constant-sized and null-terminated buffers.

ContextWithSandboxOwnedBuffer* create_context_with_sb_buffer() {
  ContextWithSandboxOwnedBuffer* ctx =
      static_cast<ContextWithSandboxOwnedBuffer*>(
          malloc(sizeof(ContextWithSandboxOwnedBuffer)));
  ctx->base.data = 10;
  memset(ctx->buff_inline, 0, sizeof(ctx->buff_inline));
  memcpy(ctx->buff_inline, "hello", 5);
  ctx->buff_null_terminated = (char*)calloc(6, sizeof(char));
  memcpy(ctx->buff_null_terminated, "world", 5);
  return ctx;
}

void to_upper_context_sb_buffers(ContextWithSandboxOwnedBuffer* context) {
  for (size_t i = 0; i < 5; ++i) {
    context->buff_inline[i] += ('A' - 'a');
  }
  for (size_t i = 0; i < 5; ++i) {
    context->buff_null_terminated[i] += ('A' - 'a');
  }
}

const char* get_buff_inline(ContextWithSandboxOwnedBuffer* context) {
  if (context == nullptr) return nullptr;
  return context->buff_inline;
}

const char* get_buff_null_terminated(ContextWithSandboxOwnedBuffer* context) {
  if (context == nullptr) return nullptr;
  return context->buff_null_terminated;
}

void destroy_context_with_sb_buffer(ContextWithSandboxOwnedBuffer* context) {
  free(context->buff_null_terminated);
  free(context);
}

////////////////////////////////////////////////////////////////////////////////
// Buffer sized by params during "create".

ContextWithSizedByParams* create_context_sized_by_params(size_t width,
                                                         size_t height) {
  ContextWithSizedByParams* context = static_cast<ContextWithSizedByParams*>(
      malloc(sizeof(ContextWithSizedByParams)));
  context->param_sizes.width = width;
  context->param_sizes.height = height;
  context->sized_by_params = (char*)calloc(width * height, sizeof(char));
  return context;
}

void fill_context_sized_by_params(ContextWithSizedByParams* context,
                                  char value) {
  if (context == nullptr) return;
  memset(context->sized_by_params, value,
         context->param_sizes.width * context->param_sizes.height);
}

const char* get_buff_sized_by_params(ContextWithSizedByParams* context) {
  if (context == nullptr) return nullptr;
  return context->sized_by_params;
}

void destroy_context_sized_by_params(ContextWithSizedByParams* context) {
  free(context->sized_by_params);
  free(context);
}

////////////////////////////////////////////////////////////////////////////////
// Buffer sized after the "create" runs (e.g., decodes part of image).

ContextWithSizedAfterDecoding* create_context_sized_after_decoding(
    const char* data_to_decode, size_t data_size) {
  if (data_size < 2) {
    return nullptr;
  }
  size_t decoded_width = data_to_decode[0];
  size_t decoded_height = data_to_decode[1];
  size_t decoded_size = decoded_width * decoded_height;
  if (data_size != decoded_size + 2) {
    return nullptr;
  }
  ContextWithSizedAfterDecoding* context =
      static_cast<ContextWithSizedAfterDecoding*>(
          malloc(sizeof(ContextWithSizedAfterDecoding)));
  context->decoded_sizes.width = decoded_width;
  context->decoded_sizes.height = decoded_height;

  context->char_buff = (char*)calloc(decoded_size, sizeof(char));
  memcpy(context->char_buff, data_to_decode + 2, decoded_size);

  context->unsigned_int_buff = nullptr;
  if (decoded_size % sizeof(unsigned int) == 0) {
    int num_ints = decoded_size / sizeof(unsigned int);
    context->unsigned_int_buff =
        (unsigned int*)calloc(num_ints, sizeof(unsigned int));
    for (size_t i = 0; i < num_ints; ++i) {
      context->unsigned_int_buff[i] = i + 0x80000000;
    }
  }
  return context;
}

void get_dimensions_sized_after_decoding(ContextWithSizedAfterDecoding* context,
                                         Dimensions* dimensions) {
  if (context == nullptr) {
    dimensions->width = 0;
    dimensions->height = 0;
    return;
  }
  dimensions->width = context->decoded_sizes.width;
  dimensions->height = context->decoded_sizes.height;
}

void increment_buff_sized_after_decoding(
    ContextWithSizedAfterDecoding* context) {
  if (context == nullptr) return;
  size_t decoded_size =
      context->decoded_sizes.width * context->decoded_sizes.height;
  for (size_t i = 0; i < decoded_size; ++i) {
    context->char_buff[i]++;
  }
}

const char* get_buff_sized_after_decoding(
    ContextWithSizedAfterDecoding* context) {
  if (context == nullptr) return nullptr;
  return context->char_buff;
}

void get_buff_sized_after_decoding_outparam(
    ContextWithSizedAfterDecoding* context, char** out_buffer) {
  if (out_buffer == nullptr) return;
  if (context == nullptr) {
    *out_buffer = nullptr;
    return;
  }
  *out_buffer = context->char_buff;
}

void get_unsigned_int_buff_sized_after_decoding_outparam(
    ContextWithSizedAfterDecoding* context, unsigned int** out_buffer) {
  if (out_buffer == nullptr) return;
  if (context == nullptr) {
    *out_buffer = nullptr;
    return;
  }
  *out_buffer = context->unsigned_int_buff;
}

void destroy_context_sized_after_decoding(
    ContextWithSizedAfterDecoding* context) {
  if (context == nullptr) return;
  free(context->unsigned_int_buff);
  free(context->char_buff);
  free(context);
}

}  // extern "C"
