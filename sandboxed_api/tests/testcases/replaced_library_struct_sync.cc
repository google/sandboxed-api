#include "sandboxed_api/tests/testcases/replaced_library_struct_sync.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "absl/log/check.h"

int kMixedStructSandboxData = 0x12345678;

void InitMixedStruct(MixedStruct* mixed) {
  mixed->host_data = 123;
  mixed->sandbox_opaque_data = &kMixedStructSandboxData;
}

int MungeMixedStruct(MixedStruct* mixed) {
  int result =
      mixed->host_data + *static_cast<int*>(mixed->sandbox_opaque_data);
  mixed->host_data = result;
  return result;
}

void RepeatStream(InOutStream* stream) {
  if (stream->prev_count == nullptr) {
    stream->prev_count = static_cast<int*>(malloc(sizeof(int)));
    *stream->prev_count = 0;
  }
  CHECK(*stream->prev_count == 0 ||
        *stream->prev_count == (stream->count->count - 1));
  *stream->prev_count = stream->count->count;
  stream->count->count++;

  size_t out_pos = 0;
  for (size_t i = 0; i < stream->in_size; ++i) {
    for (int j = 0; j < 2; ++j) {
      if (out_pos >= stream->out_size) {
        stream->did_truncate_out = true;
        fprintf(stderr, "truncated: %s\n", stream->trunc_error_msg);
        return;
      }
      stream->output[out_pos++] = stream->input[i];
    }
  }
}

void ClearStream(InOutStream* stream) { free(stream->prev_count); }

Image* CreateImage(const Span* span, size_t width, size_t height) {
  if (span->len != width * height) {
    return nullptr;
  }
  Image* image = static_cast<Image*>(malloc(sizeof(Image)));
  image->data = span->data;
  image->width = width;
  image->height = height;
  return image;
}

const uint8_t* GetRow(Image* image, size_t row) {
  if (row >= image->height) {
    return nullptr;
  }
  return &image->data[row * image->width];
}

void DeleteImage(Image* image) {
  image->data = nullptr;
  free(image);
}
