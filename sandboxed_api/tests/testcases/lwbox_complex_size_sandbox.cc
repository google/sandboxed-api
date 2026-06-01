#include <cstddef>

#include "sandboxed_api/annotations.h"
#include "sandboxed_api/tests/testcases/replaced_library_complex_size.h"

SANDBOX_FUNCS(mylib_copy_image, mylib_fill_bytes, mylib_copy_with_inptr_len,
              mylib_set_with_outptr_len_capacity,
              mylib_set_with_outptr_bytes_capacity,
              mylib_set_with_inoutptr_len);

void mylib_copy_image(
    const char* src SANDBOX_IN_PTR SANDBOX_ELEM_SIZED_BY(1 +
                                                         (width * height * 2)),
    char* dst SANDBOX_OUT_PTR SANDBOX_ELEM_SIZED_BY(1 + (width * height * 2)),
    size_t width, size_t height);

void mylib_fill_bytes(void* dst SANDBOX_OUT_PTR SANDBOX_BYTE_SIZED_BY(bytes),
                      char fill_value, size_t bytes);

void mylib_copy_with_inptr_len(
    int* dst SANDBOX_OUT_PTR SANDBOX_ELEM_SIZED_BY(*len),
    const int* src SANDBOX_IN_PTR SANDBOX_ELEM_SIZED_BY(*len),
    const size_t* len SANDBOX_IN_PTR);

// We don't have the capacity of `dst` as another param for
// `mylib_set_with_outptr_len`. So, we can only annotate and transparently
// sandbox the `mylib_set_with_outptr_len_capacity` variant, which does have
// that param.
void mylib_set_with_outptr_len_capacity(
    int buf_num,
    int* dst SANDBOX_OUT_PTR SANDBOX_ELEM_SIZED_BY_OUTPARAM(*len, capacity),
    size_t capacity, size_t* len SANDBOX_OUT_PTR);

void mylib_set_with_outptr_bytes_capacity(
    int buf_num,
    void* dst SANDBOX_OUT_PTR SANDBOX_BYTE_SIZED_BY_OUTPARAM(*num_bytes,
                                                             capacity),
    size_t capacity, size_t* num_bytes SANDBOX_OUT_PTR);

void mylib_set_with_inoutptr_len(
    int* dst SANDBOX_INOUT_PTR SANDBOX_ELEM_SIZED_BY_OUTPARAM(*len, capacity),
    size_t capacity, size_t* len SANDBOX_INOUT_PTR);
