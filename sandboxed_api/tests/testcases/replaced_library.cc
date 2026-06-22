// Copyright 2025 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "sandboxed_api/tests/testcases/replaced_library.h"

#include <err.h>
#include <errno.h>
#include <sys/epoll.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "absl/strings/string_view.h"
#include "sandboxed_api/tests/testcases/replaced_library_enum.h"
#include "sandboxed_api/tests/testcases/replaced_library_struct.h"

bool mylib_is_sandboxed() {
  // Magic sandbox2 syscall number.
  // Note: we don't use sandbox2::unit::IsRunningInSandbox2 b/c it pulls in
  // too many dependencies and disturbs the policy too much.
  return syscall(0x2f000fdb) == -1 && errno == 0xfdb;
}

void mylib_scalar_types(int a0, float a1, double a2, int64_t a3, char a4,
                        bool a5, size_t a6) {}

int mylib_add(int x, int y) { return x + y; }

MyLibEnum mylib_take_enum(MyLibEnum e) { return e; }

void mylib_take_host_opaque_ptr(void* ptr) {
  fprintf(stderr, "mylib_take_host_opaque_ptr: %p\n", ptr);
}

std::string mylib_copy(const std::string& src) { return src; }

void mylib_copy(absl::string_view src, std::string& dst) {
  dst.assign(src.data(), src.size());
}

void mylib_copy_raw(const char* src, char* dst, size_t size) {
  memcpy(dst, src, size);
}

size_t mylib_strlen(const char* str) { return strlen(str); }

const char* mylib_get_const_c_str(int i) {
  if (i == 0) {
    return "zero";
  } else if (i == 1) {
    return "one";
  } else if (i == 2) {
    return "two";
  } else {
    return "other";
  }
}

const char* mylib_get_other_c_str(int i) {
  if (i == 0) {
    return "zero";
  } else {
    return "nonzero";
  }
}

void mylib_get_inoutparam_c_str(const char** in_out) {
  if (in_out == nullptr || *in_out == nullptr) {
    return;
  }
  if (strcmp(*in_out, "odd") == 0) {
    *in_out = "even";
  } else {
    *in_out = "odd";
  }
}

void mylib_get_outparam_c_str(int i, const char** dst) {
  if (dst == nullptr) {
    return;
  }
  if (i == 0) {
    *dst = "zero";
  } else {
    *dst = "nonzero";
  }
}

void mylib_get_in_outparam_c_str(const char* src, const char** dst,
                                 const char** dst2) {
  if (src == nullptr || dst == nullptr || dst2 == nullptr) {
    return;
  }
  if (strcmp(src, "odd") == 0) {
    *dst = "even";
    *dst2 = "flipped_to_even";
  } else {
    *dst = "odd";
    *dst2 = "flipped_to_odd";
  }
}

char* mylib_fill_outbuffer_returning_alias(char* dst, int value, size_t size) {
  return static_cast<char*>(memset(dst, value, size));
}

PrimitiveStruct* mylib_struct_returning_alias(PrimitiveStruct* s, int value) {
  if (value < 0) return nullptr;
  return static_cast<PrimitiveStruct*>(memset(s, value, sizeof(*s)));
}

double mylib_in_prim_struct_pointer(const PrimitiveStruct* p) {
  return p->i8 + p->i16 + p->i32 + p->sz + p->f32 + p->f64 +
         (p->u_is_int ? p->u.i32 : p->u.f64) + p->non_trailing_array[0] +
         p->non_trailing_array[1] + p->nested.a + p->nested.b +
         static_cast<double>(p->enum_type) +
         static_cast<double>(p->enum_class_type);
}

void mylib_out_prim_struct_pointer(PrimitiveStruct* p) {
  memset(p, 0, sizeof(*p));
  p->i8 = 1;
  p->i16 = 2;
  p->i32 = 3;
  p->sz = 4;
  p->f32 = 5.0;
  p->f64 = 6.0;
  p->u_is_int = false;
  p->u.f64 = 7.0;
  p->non_trailing_array[0] = 1;
  p->non_trailing_array[1] = 0;
  p->nested.a = 8;
  p->nested.b = 9;
  p->enum_type = ENUM_A;
  p->enum_class_type = EnumClassType::EC_B;
}

void mylib_inout_prim_struct_pointer(PrimitiveStruct* p) {
  p->i8 *= 2;
  p->i16 *= 2;
  p->i32 *= 2;
  p->sz *= 2;
  p->f32 *= 2.0;
  p->f64 *= 2.0;
  if (p->u_is_int) {
    p->u.i32 *= 2;
  } else {
    p->u.f64 *= 2.0;
  }
  p->non_trailing_array[0] *= 2;
  p->non_trailing_array[1] *= 2;
  p->nested.a *= 2;
  p->nested.b *= 2;
  p->enum_type = (p->enum_type == ENUM_A) ? ENUM_B : ENUM_A;
  p->enum_class_type = (p->enum_class_type == EnumClassType::EC_A)
                           ? EnumClassType::EC_B
                           : EnumClassType::EC_A;
}

double mylib_in_prim_struct_array(const PrimitiveStruct* p, size_t num) {
  double sum = 0.0;
  for (size_t i = 0; i < num; ++i) {
    sum += mylib_in_prim_struct_pointer(&p[i]);
  }
  return sum;
}

void mylib_out_prim_struct_array(PrimitiveStruct* p, size_t num) {
  for (size_t i = 0; i < num; ++i) {
    mylib_out_prim_struct_pointer(&p[i]);
  }
}

void mylib_inout_prim_struct_array(PrimitiveStruct* p, size_t num) {
  for (size_t i = 0; i < num; ++i) {
    mylib_inout_prim_struct_pointer(&p[i]);
  }
}

// Sanitizer instrumentation may break argument value tracking.
// In particular, ASan emits a call to __asan_memset to zero ev.
static __attribute__((noinline, disable_sanitizer_instrumentation)) void
mylib_epoll_ctl(int cmd) {
  // Use epoll_ctl as test syscall b/c it's not used otherwise (e.g. by libc)
  // and has subcommands. Also uninline it to make allowed command tracking
  // a bit more difficult.
  epoll_event ev = {};
  int ret = syscall(SYS_epoll_ctl, -1, cmd, -1, &ev);
  if (ret == 0 || errno != EBADF)
    errx(1, "epoll_ctl did not fail as expected: ret=%d, errno=%d", ret, errno);
}

void mylib_expected_syscall1() { mylib_epoll_ctl(EPOLL_CTL_ADD); }

void mylib_expected_syscall2() { mylib_epoll_ctl(EPOLL_CTL_DEL); }

void mylib_unexpected_syscall1() {
  epoll_event ev = {};
  // Hide the syscall number via a volatile access, syscall extractor won't
  // discover it since it does not track memory accesses. So EPOLL_CTL_MOD
  // should end up being prohibited (while ADD/DEL should be allowed).
  static volatile int nr = SYS_epoll_ctl;
  int ret = syscall(nr, -1, EPOLL_CTL_MOD, -1, &ev);
  if (ret == 0 || errno != EBADF)
    errx(1, "epoll_ctl did not fail as expected: ret=%d, errno=%d", ret, errno);
}

void mylib_unexpected_syscall2() {
  // This syscall should be prohibited (nothing else in the binary should use
  // this esoteric syscall).
  static volatile int nr = SYS_ioprio_get;
  if (syscall(nr, 1, 0)) errx(1, "ioprio_get failed");
}
