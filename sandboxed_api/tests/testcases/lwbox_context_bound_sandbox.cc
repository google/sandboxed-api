#include <cstddef>
#include <cstdio>

#include "sandboxed_api/annotations.h"
#include "sandboxed_api/tests/testcases/replaced_library_context_bound_struct.h"

extern "C" {

SANDBOX_FUNCS(create_context_with_sb_buffer, to_upper_context_sb_buffers,
              get_buff_inline, get_buff_null_terminated,
              destroy_context_with_sb_buffer, create_context_sized_by_params,
              fill_context_sized_by_params, get_buff_sized_by_params,
              destroy_context_sized_by_params,
              create_context_sized_after_decoding,
              get_dimensions_sized_after_decoding,
              get_buff_sized_after_decoding,
              increment_buff_sized_after_decoding,
              destroy_context_sized_after_decoding);

// Constant-sized and Null-terminated buffers.

SANDBOX_OPAQUE_PTR
ContextWithSandboxOwnedBuffer* create_context_with_sb_buffer();

void to_upper_context_sb_buffers(
    ContextWithSandboxOwnedBuffer* context SANDBOX_OPAQUE_PTR);

SANDBOX_COPY_FROM_AND_BIND_OUT_PTR(
    context, SANDBOX_BIND_SIZED_BY_EXPR(sizeof(context->buff_inline)))
const char* get_buff_inline(
    ContextWithSandboxOwnedBuffer* context SANDBOX_OPAQUE_PTR);

SANDBOX_COPY_FROM_AND_BIND_OUT_PTR(context,
                                   SANDBOX_BIND_SIZED_BY_NULL_TERMINATED)
const char* get_buff_null_terminated(
    ContextWithSandboxOwnedBuffer* context SANDBOX_OPAQUE_PTR);

void destroy_context_with_sb_buffer(
    ContextWithSandboxOwnedBuffer* context SANDBOX_OPAQUE_PTR
        SANDBOX_CLEAR_BINDINGS);

// Buffer sized by params during "create".

SANDBOX_OPAQUE_PTR
SANDBOX_BIND_SIZE(SANDBOX_RETURN, "size", width* height)
ContextWithSizedByParams* create_context_sized_by_params(size_t width,
                                                         size_t height);

// TODO(b/491828958): have a sync annotation, in case the caller already did a
// get_buff_sized_by_params, that copy should be updated too.
void fill_context_sized_by_params(
    ContextWithSizedByParams* context SANDBOX_OPAQUE_PTR, char value);

SANDBOX_COPY_FROM_AND_BIND_OUT_PTR(context,
                                   SANDBOX_BIND_SIZED_BY_GET_DATA(context,
                                                                  "size"))
const char* get_buff_sized_by_params(
    ContextWithSizedByParams* context SANDBOX_OPAQUE_PTR);

void destroy_context_sized_by_params(
    ContextWithSizedByParams* context SANDBOX_OPAQUE_PTR
        SANDBOX_CLEAR_BINDINGS);

// Buffer sized after the "create" runs (e.g., decodes part of image).

SANDBOX_OPAQUE_PTR
ContextWithSizedAfterDecoding* create_context_sized_after_decoding(
    const char* data_to_decode SANDBOX_IN_PTR SANDBOX_ELEM_SIZED_BY(data_size),
    size_t data_size);

SANDBOX_BIND_SIZE(context, "size", dimensions->width * dimensions->height)
void get_dimensions_sized_after_decoding(
    ContextWithSizedAfterDecoding* context SANDBOX_OPAQUE_PTR,
    Dimensions* dimensions SANDBOX_OUT_PTR);

SANDBOX_COPY_FROM_AND_BIND_OUT_PTR(context,
                                   SANDBOX_BIND_SIZED_BY_GET_DATA(context,
                                                                  "size"))
const char* get_buff_sized_after_decoding(
    ContextWithSizedAfterDecoding* context SANDBOX_OPAQUE_PTR);

void increment_buff_sized_after_decoding(
    ContextWithSizedAfterDecoding* context SANDBOX_OPAQUE_PTR);

void destroy_context_sized_after_decoding(
    ContextWithSizedAfterDecoding* context SANDBOX_OPAQUE_PTR
        SANDBOX_CLEAR_BINDINGS);

}  // extern "C"
