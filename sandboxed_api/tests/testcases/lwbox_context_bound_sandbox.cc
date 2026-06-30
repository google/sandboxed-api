#include <cstddef>
#include <cstdio>

#include "sandboxed_api/annotations.h"
#include "sandboxed_api/tests/testcases/replaced_library_context_bound.h"
#include "sandboxed_api/tests/testcases/replaced_library_context_bound_struct.h"

extern "C" {

SANDBOX_FUNCS(
    create_context_with_sb_buffer, to_upper_context_sb_buffers, get_buff_inline,
    get_buff_null_terminated, destroy_context_with_sb_buffer,
    create_context_sized_by_params, fill_context_sized_by_params,
    get_buff_sized_by_params, destroy_context_sized_by_params,
    create_context_sized_after_decoding, get_dimensions_sized_after_decoding,
    get_buff_sized_after_decoding, get_buff_sized_after_decoding_outparam,
    get_unsigned_int_buff_sized_after_decoding_outparam,
    increment_buff_sized_after_decoding, destroy_context_sized_after_decoding,
    create_context_stores_part_of_input_size,
    hash_input_buffer_with_part_of_size_in_context,
    destroy_context_stores_part_of_input_size,
    create_context_with_host_owned_in_buffer,
    create_context_with_host_owned_out_buffer,
    get_next_chunk_host_owned_out_buffer,
    destroy_context_with_host_owned_buffer);

// Constant-sized and Null-terminated buffers.

SANDBOX_OPAQUE_PTR
ContextWithSandboxOwnedBuffer* create_context_with_sb_buffer();

void to_upper_context_sb_buffers(
    ContextWithSandboxOwnedBuffer* context SANDBOX_OPAQUE_PTR);

SANDBOX_COPY_FROM_AND_BIND_OUT_PTR(context)
SANDBOX_BYTE_SIZED_BY(sizeof(context->buff_inline))
const char* get_buff_inline(
    ContextWithSandboxOwnedBuffer* context SANDBOX_OPAQUE_PTR);

SANDBOX_COPY_FROM_AND_BIND_OUT_PTR(context)
SANDBOX_NULL_TERMINATED
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

SANDBOX_COPY_FROM_AND_BIND_OUT_PTR(context)
SANDBOX_BYTE_SIZED_BY_BINDING(context, "$size")
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

SANDBOX_COPY_FROM_AND_BIND_OUT_PTR(context)
SANDBOX_BYTE_SIZED_BY_BINDING(context, "$size")
const char* get_buff_sized_after_decoding(
    ContextWithSizedAfterDecoding* context SANDBOX_OPAQUE_PTR);

void get_buff_sized_after_decoding_outparam(
    ContextWithSizedAfterDecoding* context SANDBOX_OPAQUE_PTR,
    // TODO(b/491828958): should the SANDBOX_COPY_FROM_AND_BIND_OUT_PTR
    // annotation be in between the "*" (switch to clang::annotate_type)?
    char** out_buffer SANDBOX_COPY_FROM_AND_BIND_OUT_PTR(context)
        SANDBOX_BYTE_SIZED_BY_BINDING(context, "$size"));

void get_unsigned_int_buff_sized_after_decoding_outparam(
    ContextWithSizedAfterDecoding* context SANDBOX_OPAQUE_PTR,
    unsigned int** out_buffer SANDBOX_COPY_FROM_AND_BIND_OUT_PTR(context)
        SANDBOX_BYTE_SIZED_BY_BINDING(context, "$size"));

void increment_buff_sized_after_decoding(
    ContextWithSizedAfterDecoding* context SANDBOX_OPAQUE_PTR);

void destroy_context_sized_after_decoding(
    ContextWithSizedAfterDecoding* context SANDBOX_OPAQUE_PTR
        SANDBOX_CLEAR_BINDINGS);

// Context stores part of input buffer size. Otherwise, the input buffer only
// needs to live until the end of the function call (we don't need to bind
// its lifetime to the context).

SANDBOX_OPAQUE_PTR
SANDBOX_BIND_SIZE(SANDBOX_RETURN, "channels", num_channels)
ContextStoresPartOfInputSize* create_context_stores_part_of_input_size(
    size_t num_channels);

size_t hash_input_buffer_with_part_of_size_in_context(
    ContextStoresPartOfInputSize* context SANDBOX_OPAQUE_PTR,
    const char* input_buffer SANDBOX_IN_PTR
        SANDBOX_BYTE_SIZED_BY_BINDING(context, "num_samples * $channels"),
    size_t num_samples);

void destroy_context_stores_part_of_input_size(
    ContextStoresPartOfInputSize* context SANDBOX_OPAQUE_PTR
        SANDBOX_CLEAR_BINDINGS);

// Buffer owned by host that is retained and bound to context.

SANDBOX_BIND_SIZE(SANDBOX_RETURN, "size", data_capacity)
SANDBOX_OPAQUE_PTR
ContextWithHostOwnedBuffer* create_context_with_host_owned_in_buffer(
    char* data SANDBOX_IN_PTR SANDBOX_RETAIN_AND_BIND(SANDBOX_RETURN)
        SANDBOX_BYTE_SIZED_BY(data_capacity),
    size_t data_capacity, size_t num_chunks);

SANDBOX_BIND_SIZE(SANDBOX_RETURN, "size", data_capacity)
SANDBOX_OPAQUE_PTR
ContextWithHostOwnedBuffer* create_context_with_host_owned_out_buffer(
    char* data SANDBOX_OUT_PTR SANDBOX_RETAIN_AND_BIND(SANDBOX_RETURN)
        SANDBOX_BYTE_SIZED_BY(data_capacity),
    size_t data_capacity, size_t num_chunks);

// Note: the returned buffer is a separate copy for the
// "COPY_FROM_AND_BIND_OUT_PTR" than the "SANDBOX_RETAIN_AND_BIND" in buffer.
// At the moment, we aren't using any binding label/key to indicate that they
// should be bound to the same buffer and alias.
SANDBOX_COPY_FROM_AND_BIND_OUT_PTR(context)
SANDBOX_BYTE_SIZED_BY_BINDING(context, "$size")
char* get_next_chunk_host_owned_out_buffer(
    ContextWithHostOwnedBuffer* context SANDBOX_OPAQUE_PTR);

void destroy_context_with_host_owned_buffer(
    ContextWithHostOwnedBuffer* context SANDBOX_OPAQUE_PTR
        SANDBOX_CLEAR_BINDINGS);

}  // extern "C"
