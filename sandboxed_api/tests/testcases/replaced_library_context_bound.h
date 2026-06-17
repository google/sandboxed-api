#ifndef SANDBOXED_API_TESTS_TESTCASES_REPLACED_LIBRARY_CONTEXT_BOUND_H_
#define SANDBOXED_API_TESTS_TESTCASES_REPLACED_LIBRARY_CONTEXT_BOUND_H_

#include <cstddef>

#include "sandboxed_api/tests/testcases/replaced_library_context_bound_struct.h"

extern "C" {

////////////////////////////////////////////////////////////////////////////////
// Constant-sized and Null-terminated buffers.

// Creates a context with sandbox owned buffers.
ContextWithSandboxOwnedBuffer* create_context_with_sb_buffer();

// Modifies the sandbox-owned buffers in the context (upper case ascii,
// assuming they hold lower case ascii before).
void to_upper_context_sb_buffers(ContextWithSandboxOwnedBuffer* context);

// Returns the inline buffer from the context.
// The returned pointer is valid until `destroy_context_with_sb_buffer.
const char* get_buff_inline(ContextWithSandboxOwnedBuffer* context);

// Returns the null-terminated buffer from the context.
// The returned pointer is valid until `destroy_context_with_sb_buffer.
const char* get_buff_null_terminated(ContextWithSandboxOwnedBuffer* context);

// Destroys the context and frees the sandbox owned buffers.
void destroy_context_with_sb_buffer(ContextWithSandboxOwnedBuffer* context);

////////////////////////////////////////////////////////////////////////////////
// Buffer sized by params during "create".

ContextWithSizedByParams* create_context_sized_by_params(size_t width,
                                                         size_t height);

void fill_context_sized_by_params(ContextWithSizedByParams* context,
                                  char value);

const char* get_buff_sized_by_params(ContextWithSizedByParams* context);

void destroy_context_sized_by_params(ContextWithSizedByParams* context);

////////////////////////////////////////////////////////////////////////////////
// Buffer sized after the "create" runs (e.g., decodes part of image).
// The host can get the dimensions after "create", and we hook into that getter.

ContextWithSizedAfterDecoding* create_context_sized_after_decoding(
    const char* data_to_decode, size_t data_size);

void get_dimensions_sized_after_decoding(ContextWithSizedAfterDecoding* context,
                                         Dimensions* dimensions);

void increment_buff_sized_after_decoding(
    ContextWithSizedAfterDecoding* context);

const char* get_buff_sized_after_decoding(
    ContextWithSizedAfterDecoding* context);

void destroy_context_sized_after_decoding(
    ContextWithSizedAfterDecoding* context);

// TODO(b/491828958): a version where a buffer could be host-owned or not.

}  // extern "C"

#endif  // SANDBOXED_API_TESTS_TESTCASES_REPLACED_LIBRARY_CONTEXT_BOUND_H_
