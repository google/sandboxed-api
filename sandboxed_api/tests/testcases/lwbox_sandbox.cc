#include "sandboxed_api/annotations.h"

// Since we can't use both SANDBOX_IGNORE_FUNCS and SANDBOX_FUNCS in the
// same file, we need a separate annotation file for SANDBOX_FUNCS.
SANDBOX_FUNCS(mylib_is_sandboxed, mylib_scalar_types, mylib_add, mylib_copy,
              mylib_copy_raw, mylib_expected_syscall1, mylib_expected_syscall2,
              mylib_unexpected_syscall1, mylib_unexpected_syscall2);
