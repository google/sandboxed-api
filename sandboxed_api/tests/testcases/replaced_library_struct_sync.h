#ifndef SANDBOXED_API_TESTS_REPLACED_LIBRARY_STRUCT_SYNC_H_
#define SANDBOXED_API_TESTS_REPLACED_LIBRARY_STRUCT_SYNC_H_

#include <cstddef>
#include <cstdint>

extern "C" {

// Struct with a mixture of host and sandbox (opaque) data, and typically
// allocated on the host side, so cannot be purely opaque (needs some sync'ing).
struct MixedStruct {
  int host_data;
  void* sandbox_opaque_data;
};

void InitMixedStruct(MixedStruct* mixed);

int MungeMixedStruct(MixedStruct* mixed);

// Unretained and multiple access paths example

struct Count {
  int count;
};

// An input/output example, like zlib's z_stream.
struct InOutStream {
  uint8_t* input;
  size_t in_size;

  uint8_t* output;
  size_t out_size;

  // Additional fields for testing various sync capabilities:
  const char* trunc_error_msg;  // Null-terminated string
  bool did_truncate_out;        // Non-pointer field
  Count* count;                 // Host-allocated pointer
  int* prev_count;              // Sandbox-allocated opaque pointer
};

// Copies input and doubles chars (e.g. "ABC" to "AABBCC") up to `out_size`.
// Updates `did_truncate_out` on truncation.
// Returns 0 on success, -1 on nullptr input.
int RepeatStream(InOutStream* stream);

// Example of shallow struct syncing.
void ClearStream(InOutStream* stream);

// Example of retain and bind syncing.
struct Span {
  const uint8_t* data;
  size_t len;
};

// Context object representing an image.
struct Image {
  const uint8_t* data;
  size_t width;
  size_t height;
};

// Wraps the given `span` into an 8-bit-per-pixel `Image`, retaining a reference
// to the data until `DeleteImage` is called. `len` must equal `width * height`.
Image* CreateImage(const Span* span, size_t width, size_t height);

// Returns a pointer to a row in the `image` (slice of the original Span data),
// or nullptr if out of bounds. The returned slice must not be modified
// or freed.
const uint8_t* GetRow(Image* image, size_t row);

// Deletes the `image` and releases any retained references.
void DeleteImage(Image* image);

}  // extern "C"

#endif  // SANDBOXED_API_TESTS_REPLACED_LIBRARY_STRUCT_SYNC_H_
