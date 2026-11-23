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
#include <string.h>

#include "absl/base/attributes.h"

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

}  // extern "C"
