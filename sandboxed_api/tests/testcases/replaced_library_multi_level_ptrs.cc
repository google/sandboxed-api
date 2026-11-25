// Copyright 2026 Google LLC
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

#include "sandboxed_api/tests/testcases/replaced_library_multi_level_ptrs.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>

#include "absl/base/attributes.h"

#if defined(__has_feature)
#define SAPI_TEST_HAS_SANITIZER                                           \
  (__has_feature(address_sanitizer) || __has_feature(memory_sanitizer) || \
   __has_feature(thread_sanitizer) || __has_feature(hwaddress_sanitizer))
#elif defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define SAPI_TEST_HAS_SANITIZER 1
#else
#define SAPI_TEST_HAS_SANITIZER 0
#endif

extern "C" {

struct DbHandle {
  int value;
  int header[2];
  int footer[2];
  int alt[2];
};

int open_db_handle(int initial_value, struct DbHandle** out_db) {
  if (out_db == nullptr) {
    return -1;
  }
  *out_db = new DbHandle{
      .value = initial_value,
      .header = {initial_value + 1, initial_value + 2},
      .footer = {initial_value + 10, initial_value + 20},
      .alt = {initial_value + 100, initial_value + 200},
  };
  return 0;
}

int read_db_handle(struct DbHandle* db) {
  if (db == nullptr) {
    return -1;
  }
  return db->value;
}

int update_or_close_db_handle(struct DbHandle** inout_db, int new_value) {
  if (inout_db == nullptr || *inout_db == nullptr) {
    return -1;
  }
  int old_value = (*inout_db)->value;
  if (new_value < 0) {
    delete *inout_db;
    *inout_db = nullptr;
  } else {
    (*inout_db)->value = new_value;
  }
  return old_value;
}

void close_db_handle(struct DbHandle* db) { delete db; }

int get_db_header_and_footer(struct DbHandle* db, const int** out_header,
                             const int** out_footer) {
  if (db == nullptr) {
    return -1;
  }
  if (out_header != nullptr) {
    *out_header = db->header;
  }
  if (out_footer != nullptr) {
    *out_footer = db->footer;
  }
  return 0;
}

int get_db_alt_slice(struct DbHandle* db, const int** out_alt) {
  if (db == nullptr) {
    return -1;
  }
  if (out_alt != nullptr) {
    *out_alt = db->alt;
  }
  return 0;
}

size_t sum_string_lengths(size_t count, const char* const* strings,
                          const char** out_status) {
  size_t total = 0;
  if (strings != nullptr) {
    for (size_t i = 0; i < count; ++i) {
      if (strings[i] != nullptr) {
        total += strlen(strings[i]);
      }
    }
  }
  if (out_status != nullptr) {
    *out_status = (total > 0) ? "ok" : "empty";
  }
  return total;
}

int match_or_replace_status(const char* const* expected_status,
                            const char** inout_status) {
  if (inout_status == nullptr) {
    return -1;
  }
  if (expected_status != nullptr && *expected_status != nullptr &&
      *inout_status != nullptr &&
      strcmp(*expected_status, *inout_status) == 0) {
    *inout_status = "matched";
    return 1;
  }
  *inout_status = "mismatch";
  return 0;
}

int scale_2d_grid(int** grid, size_t rows, size_t cols, int factor) {
  if (grid == nullptr) {
    return 0;
  }
  int sum = 0;
  for (size_t r = 0; r < rows; ++r) {
    if (grid[r] == nullptr) {
      continue;
    }
    for (size_t c = 0; c < cols; ++c) {
      grid[r][c] *= factor;
      sum += grid[r][c];
    }
  }
  return sum;
}

int add_to_matrix_via_3level_ptr(int*** matrix_ptr, size_t rows, size_t cols,
                                 int delta) {
  if (matrix_ptr == nullptr || *matrix_ptr == nullptr) {
    return 0;
  }
  int sum = 0;
  for (size_t r = 0; r < rows; ++r) {
    if ((*matrix_ptr)[r] == nullptr) {
      continue;
    }
    for (size_t c = 0; c < cols; ++c) {
      (*matrix_ptr)[r][c] += delta;
      sum += (*matrix_ptr)[r][c];
    }
  }
  return sum;
}

int increment_3d_tensor(int*** tensor, size_t layers, size_t rows, size_t cols,
                        int delta) {
  if (tensor == nullptr) {
    return 0;
  }
  int sum = 0;
  for (size_t l = 0; l < layers; ++l) {
    if (tensor[l] == nullptr) {
      continue;
    }
    for (size_t r = 0; r < rows; ++r) {
      if (tensor[l][r] == nullptr) {
        continue;
      }
      for (size_t c = 0; c < cols; ++c) {
        tensor[l][r][c] += delta;
        sum += tensor[l][r][c];
      }
    }
  }
  return sum;
}

int open_db_batch_via_3level_ptr(size_t count, int base_value,
                                 struct DbHandle*** handle_list_ptr) {
  if (handle_list_ptr == nullptr || *handle_list_ptr == nullptr) {
    return -1;
  }
  for (size_t i = 0; i < count; ++i) {
    (*handle_list_ptr)[i] =
        new DbHandle{.value = base_value + static_cast<int>(i)};
  }
  return static_cast<int>(count);
}

int check_option_from_list(const char* name, const char* def,
                           const char* const lst[]) {
  const char* target = (name != nullptr && name[0] != '\0') ? name : def;
  if (target == nullptr || lst == nullptr) {
    return -1;
  }
  for (int i = 0; lst[i] != nullptr; ++i) {
    if (strcmp(lst[i], target) == 0) {
      return i;
    }
  }
  return -1;
}

namespace {

int g_default_sax_attr_byte_sum = 0;

void default_sax_start_element(void* ctx, const sax_xml_char_t* name,
                               const sax_xml_char_t** atts) {
  (void)ctx;
  if (name != nullptr) {
    for (size_t i = 0; name[i] != 0; ++i) {
      g_default_sax_attr_byte_sum += static_cast<int>(name[i]);
    }
  }
  if (atts != nullptr) {
    for (size_t i = 0; atts[i] != nullptr; ++i) {
      for (size_t j = 0; atts[i][j] != 0; ++j) {
        g_default_sax_attr_byte_sum += static_cast<int>(atts[i][j]);
      }
    }
  }
}

}  // namespace

ABSL_ATTRIBUTE_NO_SANITIZE_CFI void dispatch_sax_start_element(
    void* ctx, const sax_xml_char_t* name, const sax_xml_char_t** atts,
    sax_start_element_cb cb) {
  if (cb != nullptr) {
    cb(ctx, name, atts);
  }
}

sax_start_element_cb get_default_sax_start_element_cb() {
  return &default_sax_start_element;
}

int get_default_sax_attr_byte_sum() { return g_default_sax_attr_byte_sum; }

void reset_default_sax_attr_byte_sum() { g_default_sax_attr_byte_sum = 0; }

size_t measure_non_const_c_strings(char* str, unsigned char* ustr) {
  size_t total = 0;
  if (str != nullptr) {
    total += strlen(str);
  }
  if (ustr != nullptr) {
    while (*ustr != 0) {
      ++total;
      ++ustr;
    }
  }
  return total;
}

void uppercase_and_clobber_terminator(char* str, int clobber_terminator) {
  if (str == nullptr) {
    return;
  }
  size_t len = strlen(str);
  for (size_t i = 0; i < len; ++i) {
    if (str[i] >= 'a' && str[i] <= 'z') {
      str[i] = static_cast<char>(str[i] - 'a' + 'A');
    }
  }
  if (clobber_terminator != 0) {
    str[len] = '!';
  }
}

int sum_and_scale_tagged_slice(struct TaggedIntSlice* slice, int factor) {
  if (slice == nullptr) {
    return -1;
  }
  int sum = 0;
  if (slice->label != nullptr) {
    sum += static_cast<int>(strlen(slice->label));
  }
  if (slice->elems != nullptr) {
    for (size_t i = 0; i < slice->count; ++i) {
      slice->elems[i] *= factor;
      sum += slice->elems[i];
    }
  }
  return sum;
}

int fill_up_to_inout_count(int* dst, size_t* inout_count, size_t actual_count,
                           int base_value) {
  if (dst == nullptr || inout_count == nullptr) {
    return -1;
  }
  size_t write_count = std::min(actual_count, *inout_count);
  for (size_t i = 0; i < write_count; ++i) {
    dst[i] = base_value + static_cast<int>(i);
  }
  *inout_count = actual_count;
  return (actual_count <= write_count) ? 0 : -2;
}

int retain_inout_db_buffer(struct DbHandle* db, int* buf, size_t* inout_count,
                           size_t actual_count, int base_value) {
  if (db == nullptr || buf == nullptr || inout_count == nullptr) {
    return -1;
  }
  size_t write_count = std::min(actual_count, *inout_count);
  for (size_t i = 0; i < write_count; ++i) {
    buf[i] += base_value + static_cast<int>(i);
  }
  *inout_count = actual_count;
  return (actual_count <= write_count) ? 0 : -2;
}

int fill_or_fail_outparam_sized(int* dst, size_t capacity_bytes,
                                size_t* out_count, int should_fail,
                                size_t write_count, int fill_value) {
  if (should_fail != 0) {
    return -1;
  }
  if (dst == nullptr || out_count == nullptr) {
    return -2;
  }
  size_t max_elems = capacity_bytes / sizeof(int);
  size_t n = std::min(write_count, max_elems);
  for (size_t i = 0; i < n; ++i) {
    dst[i] = fill_value + static_cast<int>(i);
  }
  *out_count = write_count;
  return 0;
}

int retain_signed_inout_db_buffer(struct DbHandle* db, int* buf,
                                  int* inout_count, int actual_count,
                                  int base_value) {
  if (db == nullptr || buf == nullptr || inout_count == nullptr) {
    return -1;
  }
  if (actual_count >= 0 && *inout_count > 0) {
    int write_count = std::min(actual_count, *inout_count);
    for (int i = 0; i < write_count; ++i) {
      buf[i] += base_value + i;
    }
  }
  *inout_count = actual_count;
  return 0;
}

int sum_in_ptr_with_signed_inout_count(const int* src, int* inout_count,
                                       int updated_count) {
  if (src == nullptr || inout_count == nullptr) {
    return -1;
  }
  int sum = 0;
  for (int i = 0; i < *inout_count; ++i) {
    sum += src[i];
  }
  *inout_count = updated_count;
  return sum;
}

ABSL_ATTRIBUTE_NO_SANITIZE_CFI int invoke_out_counted_callback(
    out_counted_fill_cb cb, int initial_out_count_garbage, int should_fail,
    int* observed_first_elem) {
  if (cb == nullptr || observed_first_elem == nullptr) {
    return -2;
  }
  int local_dst[4] = {777, 778, 779, 780};
  int local_out_count = initial_out_count_garbage;
  int rc = cb(local_dst, sizeof(local_dst), &local_out_count, should_fail);
  *observed_first_elem = local_dst[0];
  return rc;
}

ABSL_ATTRIBUTE_NO_SANITIZE_CFI int invoke_inout_counted_callback(
    inout_counted_xform_cb cb, int initial_inout_count, int should_fail,
    int* final_inout_count) {
  if (cb == nullptr || final_inout_count == nullptr) {
    return -2;
  }
  int local_buf[4] = {10, 20, 30, 40};
  int local_count = initial_inout_count;
  int rc = cb(local_buf, sizeof(local_buf), &local_count, should_fail);
  *final_inout_count = local_count;
  return rc;
}

int is_marshaled_copy(const int* probe, uintptr_t caller_addr) {
  return reinterpret_cast<uintptr_t>(probe) != caller_addr ? 1 : 0;
}

char* duplicate_and_reverse_malloc_string(const char* src) {
  if (src == nullptr || src[0] == '\0') {
    return nullptr;
  }
  size_t len = strlen(src);
  char* dst = static_cast<char*>(malloc(len + 1));
  if (dst == nullptr) {
    return nullptr;
  }
  for (size_t i = 0; i < len; ++i) {
    dst[i] = src[len - 1 - i];
  }
  dst[len] = '\0';
  return dst;
}

int* allocate_arithmetic_sequence(size_t count, int start, int step,
                                  size_t* out_count) {
  if (out_count != nullptr) {
    *out_count = count;
  }
  if (count == 0) {
    return nullptr;
  }
  int* arr = static_cast<int*>(malloc(count * sizeof(int)));
  if (arr == nullptr) {
    return nullptr;
  }
  for (size_t i = 0; i < count; ++i) {
    arr[i] = start + static_cast<int>(i) * step;
  }
  return arr;
}

int allocate_tag_and_buffer_outparams(const char* prefix, size_t count,
                                      int base, char** out_tag, int** out_buf,
                                      size_t* out_len) {
  if (base < -1000) {
    return -1;
  }
  if (out_len != nullptr) {
    *out_len = count;
  }
  if (out_tag != nullptr) {
    if (prefix == nullptr || prefix[0] == '\0') {
      *out_tag = nullptr;
    } else {
      size_t plen = strlen(prefix);
      char* tag = static_cast<char*>(malloc(plen + 4));
      memcpy(tag, prefix, plen);
      memcpy(tag + plen, ":ok", 4);
      *out_tag = tag;
    }
  }
  if (out_buf != nullptr) {
    if (count == 0) {
      *out_buf = nullptr;
    } else {
      int* buf = static_cast<int*>(malloc(count * sizeof(int)));
      for (size_t i = 0; i < count; ++i) {
        buf[i] = base + static_cast<int>(i);
      }
      *out_buf = buf;
    }
  }
  return 0;
}

int finalize_memio_buffer(size_t size, unsigned char fill_byte, int flags,
                          struct MemIoBuffer* out_memio) {
  if (out_memio == nullptr || flags < 0) {
    return -1;
  }
  out_memio->size = size;
  out_memio->flags = flags;
  if (size == 0) {
    out_memio->memory = nullptr;
    return 0;
  }
  unsigned char* mem = static_cast<unsigned char*>(malloc(size));
  if (mem == nullptr) {
    out_memio->memory = nullptr;
    return -2;
  }
  for (size_t i = 0; i < size; ++i) {
    mem[i] = static_cast<unsigned char>(fill_byte + (i & 0xff));
  }
  out_memio->memory = mem;
  return 0;
}

struct HyperslabEntry {
  int ncid;
  int varid;
  size_t elems;
  bool in_use;
};

static size_t g_hyperslab_elems = 0;
static struct HyperslabEntry g_hyperslab_table[16];

static size_t lookup_hyperslab_elems(int ncid, int varid) {
  for (size_t i = 0; i < 16; ++i) {
    if (g_hyperslab_table[i].in_use && g_hyperslab_table[i].ncid == ncid &&
        g_hyperslab_table[i].varid == varid) {
      return g_hyperslab_table[i].elems;
    }
  }
  return 0;
}

int register_hyperslab_var(int ncid, int varid, const size_t* counts,
                           size_t ndims) {
  if (counts == nullptr || ndims == 0) {
    return -1;
  }
  size_t prod = 1;
  for (size_t i = 0; i < ndims; ++i) {
    prod *= counts[i];
  }
  g_hyperslab_elems = prod;
  for (size_t i = 0; i < 16; ++i) {
    if (!g_hyperslab_table[i].in_use || (g_hyperslab_table[i].ncid == ncid &&
                                         g_hyperslab_table[i].varid == varid)) {
      g_hyperslab_table[i] = {ncid, varid, prod, true};
      break;
    }
  }
  return ncid + varid;
}

int read_hyperslab_var(int ncid, int varid, int* out_data, int base_val) {
  if (out_data == nullptr) {
    return -1;
  }
  for (size_t i = 0; i < g_hyperslab_elems; ++i) {
    out_data[i] = base_val + static_cast<int>(i) + ncid * 10 + varid;
  }
  return static_cast<int>(g_hyperslab_elems);
}

static size_t g_matrix_cols = 0;

int bind_matrix_var_cols(int ncid, int varid, size_t cols) {
  if (ncid < 0 || varid < 0 || cols == 0) {
    return -1;
  }
  g_matrix_cols = cols;
  return ncid * 100 + varid;
}

int compute_matrix_slice(struct MatrixSliceRequest* slice, int ncid,
                         size_t rows) {
  if (slice == nullptr || slice->row_weights == nullptr ||
      slice->out_cells == nullptr) {
    return -1;
  }
  for (size_t r = 0; r < slice->rows; ++r) {
    for (size_t c = 0; c < g_matrix_cols; ++c) {
      slice->out_cells[r * g_matrix_cols + c] +=
          static_cast<int>(r + 1) * slice->row_weights[c] + slice->ncid * 10 +
          slice->varid;
    }
  }
  if (slice->cols != 0) {
    slice->rows = slice->cols;
  } else {
    slice->cols = g_matrix_cols;
  }
  return ncid + static_cast<int>(rows) +
         static_cast<int>(slice->rows * g_matrix_cols);
}

ABSL_ATTRIBUTE_NO_SANITIZE_CFI int evaluate_callbacks_at_guarded_stack_boundary(
    int (*cb6)(int p1, int p2, int p3, int p4, int p5, int p6),
    int (*cb8)(int p1, int p2, int p3, int p4, int p5, int p6, int p7,
               int p8)) {
  if (cb6 == nullptr || cb8 == nullptr) {
    return -1;
  }
#if !SAPI_TEST_HAS_SANITIZER && (defined(__x86_64__) || defined(__aarch64__))
  const int64_t raw_page = sysconf(_SC_PAGESIZE);
  const size_t page_size =
      raw_page > 0 ? static_cast<size_t>(raw_page) : size_t{4096};
  const size_t stack_bytes = 16 * page_size;
  void* mapping = mmap(nullptr, stack_bytes + page_size, PROT_NONE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (mapping == MAP_FAILED) {
    return -2;
  }
  if (mprotect(mapping, stack_bytes, PROT_READ | PROT_WRITE) != 0) {
    munmap(mapping, stack_bytes + page_size);
    return -3;
  }
  uint8_t* stack_top = static_cast<uint8_t*>(mapping) + stack_bytes;
#if defined(__x86_64__)
  // Each asm block calls a callback, which can change any caller-saved
  // register (x86_64 SysV ABI). So `r10` is an in/out operand and the other
  // caller-saved registers are clobbers. `r12` and `r13` are callee-saved.
  int64_t r6 = 0;
  register uintptr_t top_reg asm("r10") =
      reinterpret_cast<uintptr_t>(stack_top);
  register auto fn6_reg asm("r12") = cb6;
  asm volatile(
      "movq %%rsp, %%r13\n\t"
      "movq %%r10, %%rsp\n\t"
      "movl $10, %%edi\n\t"
      "movl $20, %%esi\n\t"
      "movl $30, %%edx\n\t"
      "movl $40, %%ecx\n\t"
      "movl $50, %%r8d\n\t"
      "movl $60, %%r9d\n\t"
      "call *%%r12\n\t"
      "movq %%r13, %%rsp\n\t"
      : "=a"(r6), "+r"(top_reg)
      : "r"(fn6_reg)
      : "r13", "rdi", "rsi", "rdx", "rcx", "r8", "r9", "r11", "xmm0", "xmm1",
        "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7", "xmm8", "xmm9", "xmm10",
        "xmm11", "xmm12", "xmm13", "xmm14", "xmm15", "cc", "memory");

  // Place exactly two 8-byte stack arguments immediately below the `PROT_NONE`
  // guard page (`[rbp + 16]` and `[rbp + 24]`); reading `[rbp + 32]` faults.
  uint64_t* sp8 = reinterpret_cast<uint64_t*>(stack_top - 16);
  sp8[0] = 70;
  sp8[1] = 80;
  int64_t r8 = 0;
  register uintptr_t sp8_reg asm("r10") = reinterpret_cast<uintptr_t>(sp8);
  register auto fn8_reg asm("r12") = cb8;
  asm volatile(
      "movq %%rsp, %%r13\n\t"
      "movq %%r10, %%rsp\n\t"
      "movl $1, %%edi\n\t"
      "movl $2, %%esi\n\t"
      "movl $3, %%edx\n\t"
      "movl $4, %%ecx\n\t"
      "movl $5, %%r8d\n\t"
      "movl $6, %%r9d\n\t"
      "call *%%r12\n\t"
      "movq %%r13, %%rsp\n\t"
      : "=a"(r8), "+r"(sp8_reg)
      : "r"(fn8_reg)
      : "r13", "rdi", "rsi", "rdx", "rcx", "r8", "r9", "r11", "xmm0", "xmm1",
        "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7", "xmm8", "xmm9", "xmm10",
        "xmm11", "xmm12", "xmm13", "xmm14", "xmm15", "cc", "memory");
#elif defined(__aarch64__)
  // The callback can change any caller-saved register (AAPCS64), so `x10` and
  // `x11` are in/out operands and the other caller-saved registers are
  // clobbers. `x18` is not listed: it is the platform register, and some
  // configurations reserve it (`-ffixed-x18`).
  register uintptr_t top_reg asm("x10") =
      reinterpret_cast<uintptr_t>(stack_top);
  register auto fn6_reg asm("x11") = cb6;
  register int64_t r6_reg asm("x0");
  asm volatile(
      "mov x19, sp\n\t"
      "mov sp, x10\n\t"
      "mov x0, #10\n\t"
      "mov x1, #20\n\t"
      "mov x2, #30\n\t"
      "mov x3, #40\n\t"
      "mov x4, #50\n\t"
      "mov x5, #60\n\t"
      "blr x11\n\t"
      "mov sp, x19\n\t"
      : "=r"(r6_reg), "+r"(top_reg), "+r"(fn6_reg)
      :
      : "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9", "x12", "x13",
        "x14", "x15", "x16", "x17", "x19", "x30", "v0", "v1", "v2", "v3", "v4",
        "v5", "v6", "v7", "v16", "v17", "v18", "v19", "v20", "v21", "v22",
        "v23", "v24", "v25", "v26", "v27", "v28", "v29", "v30", "v31", "cc",
        "memory");
  const int64_t r6 = r6_reg;
  const int64_t r8 = cb8(1, 2, 3, 4, 5, 6, 70, 80);
#endif
  munmap(mapping, stack_bytes + page_size);
  return static_cast<int>(r6 + r8);
#else
  return cb6(10, 20, 30, 40, 50, 60) + cb8(1, 2, 3, 4, 5, 6, 70, 80);
#endif
}

ABSL_ATTRIBUTE_NO_SANITIZE_CFI int evaluate_ten_int_callback(
    int base, int (*cb)(int p1, int p2, int p3, int p4, int p5, int p6, int p7,
                        int p8, int p9, int p10)) {
  return cb(base + 1, base + 2, base + 3, base + 4, base + 5, base + 6,
            base + 7, base + 8, base + 9, base + 10);
}

ABSL_ATTRIBUTE_NO_SANITIZE_CFI double evaluate_ten_arg_float_callback(
    int a, int b, int c, float weight, double bias,
    double (*cb)(int p1, int p2, int p3, int p4, int p5, int p6, int p7, int p8,
                 float f1, double d1)) {
  if (cb == nullptr) {
    return -1.0;
  }
  return cb(a, b, c, a + 1, b + 1, c + 1, a + 2, b + 2, weight, bias);
}

ABSL_ATTRIBUTE_NO_SANITIZE_CFI float evaluate_nine_float_callback(
    float base,
    float (*cb)(float f1, float f2, float f3, float f4, float f5, float f6,
                float f7, float f8, float f9, int scale)) {
  if (cb == nullptr) {
    return -1.0f;
  }
  return cb(base + 1.0f, base + 2.0f, base + 3.0f, base + 4.0f, base + 5.0f,
            base + 6.0f, base + 7.0f, base + 8.0f, base + 9.0f, 2);
}

int filter_or_scale_outer_packets(struct OuterPacketRecord* packets,
                                  size_t count, size_t min_len, int scale) {
  if (packets == nullptr) {
    return -1;
  }
  int kept = 0;
  for (size_t i = 0; i < count; ++i) {
    if (packets[i].payload.values == nullptr ||
        packets[i].payload.len < min_len) {
      packets[i].payload.values = nullptr;
      packets[i].payload.len = 0;
      packets[i].checksum = -1;
      continue;
    }
    int sum = packets[i].tag != nullptr
                  ? static_cast<int>(strlen(packets[i].tag))
                  : 0;
    for (size_t j = 0; j < packets[i].payload.len; ++j) {
      packets[i].payload.values[j] *= scale;
      sum += packets[i].payload.values[j];
    }
    packets[i].checksum = sum;
    ++kept;
  }
  return kept;
}

int mutate_labeled_slices(struct MutableLabeledSlice* slices, size_t count,
                          int clobber_sentinel, int delta) {
  if (slices == nullptr) {
    return -1;
  }
  int mutated = 0;
  for (size_t i = 0; i < count; ++i) {
    if (slices[i].label != nullptr) {
      size_t n = strlen(slices[i].label);
      for (size_t j = 0; j < n; ++j) {
        if (slices[i].label[j] >= 'a' && slices[i].label[j] <= 'z') {
          slices[i].label[j] =
              static_cast<char>(slices[i].label[j] - 'a' + 'A');
        }
      }
      if (clobber_sentinel != 0) {
        slices[i].label[n] = '!';
      }
    }
    if (slices[i].values != nullptr) {
      for (size_t j = 0; j < slices[i].len; ++j) {
        slices[i].values[j] += delta;
      }
    }
    ++mutated;
  }
  return mutated;
}

struct RetainedSpanTable {
  struct RetainedSpanSpec* specs;
  size_t count;
};

struct RetainedSpanTable* create_retained_span_table(
    const struct RetainedSpanSpec* specs, size_t count) {
  if (specs == nullptr || count == 0) {
    return nullptr;
  }
  for (size_t i = 0; i < count; ++i) {
    if (specs[i].weight < 0) {
      return nullptr;
    }
  }
  auto* table = static_cast<struct RetainedSpanTable*>(
      calloc(1, sizeof(struct RetainedSpanTable)));
  if (table == nullptr) {
    return nullptr;
  }
  table->specs =
      static_cast<struct RetainedSpanSpec*>(calloc(count, sizeof(*specs)));
  if (table->specs == nullptr) {
    free(table);
    return nullptr;
  }
  for (size_t i = 0; i < count; ++i) {
    table->specs[i] = specs[i];
  }
  table->count = count;
  return table;
}

int eval_retained_span_table(struct RetainedSpanTable* table) {
  if (table == nullptr) {
    return -1;
  }
  int total = 0;
  for (size_t i = 0; i < table->count; ++i) {
    if (table->specs[i].data == nullptr) {
      continue;
    }
    for (size_t j = 0; j < table->specs[i].len; ++j) {
      total += table->specs[i].weight * table->specs[i].data[j];
    }
  }
  return total;
}

void destroy_retained_span_table(struct RetainedSpanTable* table) {
  if (table != nullptr) {
    free(table->specs);
    free(table);
  }
}

// Multiplies the values of `slices[0..count)` by `scale`, and returns the
// number of scaled values.
static size_t scale_payload_slices(struct InnerPayloadSlice* slices,
                                   size_t count, int scale) {
  size_t scaled = 0;
  for (size_t i = 0; i < count; ++i) {
    if (slices[i].values == nullptr) {
      continue;
    }
    for (size_t j = 0; j < slices[i].len; ++j) {
      slices[i].values[j] *= scale;
    }
    scaled += slices[i].len;
  }
  return scaled;
}

int scale_payload_slice_arrays(struct InnerPayloadSlice* byte_sized,
                               size_t byte_sized_size, int ncid, int varid,
                               struct InnerPayloadSlice* elem_bound,
                               struct InnerPayloadSlice* byte_bound,
                               int scale) {
  if (byte_sized == nullptr || elem_bound == nullptr || byte_bound == nullptr) {
    return -1;
  }
  const size_t bound_count = lookup_hyperslab_elems(ncid, varid);
  return static_cast<int>(
      scale_payload_slices(byte_sized, byte_sized_size / sizeof(*byte_sized),
                           scale) +
      scale_payload_slices(elem_bound, bound_count, scale) +
      scale_payload_slices(byte_bound, bound_count, scale));
}

void inflate_payload_slice_lens(struct InnerPayloadSlice* slices, size_t count,
                                size_t new_len) {
  for (size_t i = 0; slices != nullptr && i < count; ++i) {
    slices[i].len = new_len;
  }
}

int finalize_memio_buffers(struct MemIoBuffer* out_memios, size_t count,
                           unsigned char fill_byte) {
  if (out_memios == nullptr) {
    return -1;
  }
  for (size_t i = 0; i < count; ++i) {
    const int status = finalize_memio_buffer(
        2 * i, static_cast<unsigned char>(fill_byte + 16 * i),
        static_cast<int>(i), &out_memios[i]);
    if (status != 0) {
      return status;
    }
  }
  return 0;
}

int fill_nested_hyperslab_request(struct NestedHyperslabRequest* req, int ncid,
                                  size_t rows) {
  (void)ncid;
  (void)rows;
  if (req == nullptr || req->slice.out_data == nullptr) {
    return -1;
  }
  size_t elem_count = lookup_hyperslab_elems(req->slice.ncid, req->slice.varid);
  size_t total = req->slice.rows * elem_count;
  for (size_t i = 0; i < total; ++i) {
    req->slice.out_data[i] = req->tag_id * 100 + req->slice.ncid * 10 +
                             req->slice.varid + static_cast<int>(i);
  }
  return static_cast<int>(total);
}

int fill_hyperslab_slice_batch(struct BoundHyperslabSlice* slices, size_t count,
                               int ncid, size_t rows) {
  (void)ncid;
  (void)rows;
  if (slices == nullptr) {
    return -1;
  }
  size_t grand_total = 0;
  for (size_t s = 0; s < count; ++s) {
    if (slices[s].out_data == nullptr) {
      continue;
    }
    size_t elem_count = lookup_hyperslab_elems(slices[s].ncid, slices[s].varid);
    size_t total = slices[s].rows * elem_count;
    for (size_t i = 0; i < total; ++i) {
      slices[s].out_data[i] =
          slices[s].ncid * 10 + slices[s].varid + static_cast<int>(i);
    }
    grand_total += total;
  }
  return static_cast<int>(grand_total);
}

}  // extern "C"
