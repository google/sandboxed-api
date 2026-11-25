#include "sandboxed_api/annotations.h"

SANDBOX_UNSUPPORTED("Unions are not supported")
void mylib_func_with_unsupported_arg(union mylib_union* arg);
