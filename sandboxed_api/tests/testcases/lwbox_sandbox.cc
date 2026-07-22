#include "sandboxed_api/annotations.h"

// Since we can't use both SANDBOX_IGNORE_FUNCS and SANDBOX_FUNCS in the
// same file, we need a separate annotation file for SANDBOX_FUNCS.
SANDBOX_FUNCS(mylib_is_sandboxed, mylib_scalar_types, mylib_add,
              mylib_take_enum, mylib_nullable_outparam,
              mylib_take_host_opaque_ptr, mylib_copy, mylib_copy_raw,
              mylib_strlen, mylib_get_const_c_str, mylib_get_other_c_str,
              mylib_get_inoutparam_c_str, mylib_get_outparam_c_str,
              mylib_fill_outbuffer_returning_alias,
              mylib_struct_returning_alias, mylib_get_in_outparam_c_str,
              mylib_in_prim_struct_pointer, mylib_out_prim_struct_pointer,
              mylib_inout_prim_struct_pointer, mylib_in_prim_struct_array,
              mylib_out_prim_struct_array, mylib_inout_prim_struct_array,
              mylib_expected_syscall1, mylib_expected_syscall2,
              mylib_unexpected_syscall1, mylib_unexpected_syscall2,
              mylib_func_with_todo);
