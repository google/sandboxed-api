#include <cstddef>

#include "sandboxed_api/annotations.h"
#include "sandboxed_api/tests/testcases/replaced_library_complex_size.h"

SANDBOX_FUNCS(mylib_copy_image, mylib_fill_bytes);

void mylib_copy_image(
    const char* src SANDBOX_IN_PTR SANDBOX_ELEM_SIZED_BY(1 +
                                                         (width * height * 2)),
    char* dst SANDBOX_OUT_PTR SANDBOX_ELEM_SIZED_BY(1 + (width * height * 2)),
    size_t width, size_t height);

void mylib_fill_bytes(void* dst SANDBOX_OUT_PTR SANDBOX_BYTE_SIZED_BY(bytes),
                      char fill_value, size_t bytes);
