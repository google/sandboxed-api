#include <cstddef>
#include <cstdint>

#include "sandboxed_api/annotations.h"
#include "sandboxed_api/tests/testcases/replaced_library_struct_sync.h"

extern "C" {

// By annotating the `sandbox_opaque_data` data member as opaque, we know
// it is okay to only shallowly copy the struct between host <-> sandbox.
// Since the field type is void*, we wouldn't know how to deeply copy it anyway.
SANDBOX_ANNOTATE_STRUCT(struct MixedStruct {
  int host_data;
  void* sandbox_opaque_data SANDBOX_OPAQUE_PTR;
};)

void InitMixedStruct(MixedStruct* mixed SANDBOX_OUT_PTR);

int MungeMixedStruct(MixedStruct* mixed SANDBOX_INOUT_PTR);

// Unretained and multiple access paths example.
SANDBOX_ANNOTATE_STRUCT(struct InOutStream {
  uint8_t* input SANDBOX_BYTE_SIZED_BY(in_size);
  size_t in_size;

  uint8_t* output SANDBOX_BYTE_SIZED_BY(out_size);
  size_t out_size;

  const char* trunc_error_msg SANDBOX_NULL_TERMINATED;
  int* prev_count SANDBOX_OPAQUE_PTR;
};)

void RepeatStream(InOutStream* stream SANDBOX_INOUT_PTR SANDBOX_STRUCT_SYNC(
    {stream->input, in_ptr}, {stream->output, out_ptr},
    {stream->trunc_error_msg, in_ptr}, {stream->count, inout_ptr}));

// Shallow sync example.
void ClearStream(InOutStream* stream SANDBOX_INOUT_PTR SANDBOX_SHALLOW_SYNC);

// Retain and bind example.
SANDBOX_ANNOTATE_STRUCT(struct Span {
  const uint8_t* data SANDBOX_BYTE_SIZED_BY(len);
  size_t len;
};)

SANDBOX_OPAQUE_PTR
SANDBOX_BIND_SIZE(SANDBOX_RETURN, "row_size", width)
Image* CreateImage(const Span* span SANDBOX_IN_PTR SANDBOX_STRUCT_SYNC(
                       {span->data, in_ptr, retain_and_bind(SANDBOX_RETURN)}),
                   size_t width, size_t height);

SANDBOX_COPY_FROM_AND_BIND_OUT_PTR(image)
SANDBOX_BYTE_SIZED_BY_BINDING(image, "$row_size")
const uint8_t* GetRow(Image* image SANDBOX_OPAQUE_PTR, size_t row);

void DeleteImage(Image* image SANDBOX_OPAQUE_PTR SANDBOX_CLEAR_BINDINGS);

}  // extern "C"
