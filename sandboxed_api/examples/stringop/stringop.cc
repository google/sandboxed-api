// Copyright 2019 Google LLC
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

#include <sys/ptrace.h>

#include <algorithm>
#include <iostream>

#include "absl/base/attributes.h"
#include "absl/base/optimization.h"
#include "sandboxed_api/examples/stringop/stringop_params.pb.h"
#include "sandboxed_api/lenval_core.h"

// Protobuf examples.
extern "C" int pb_reverse_string(stringop::StringReverse* pb) {
  if (pb->payload_case() == pb->kInput) {
    std::string output = pb->input();
    std::reverse(output.begin(), output.end());
    pb->set_output(output);
    return 1;
  }
  return 0;
}

extern "C" int pb_duplicate_string(stringop::StringDuplication* pb) {
  if (pb->payload_case() == pb->kInput) {
    auto output = pb->input();
    pb->set_output(output + output);
    return 1;
  }
  return 0;
}

// Examples on raw data - both allocate and replace the data pointer.
extern "C" int reverse_string(sapi::LenValStruct* input) {
  char* new_buf = static_cast<char*>(malloc(input->size));
  const char* src_buf = static_cast<const char*>(input->data);
  input->size = input->size;
  for (size_t i = 0; i < input->size; i++) {
    new_buf[i] = src_buf[input->size - i - 1];
  }
  // Free old value.
  free(input->data);
  // Replace pointer to our new string.
  input->data = new_buf;
  return 1;
}

extern "C" int duplicate_string(sapi::LenValStruct* input) {
  char* new_buf = static_cast<char*>(malloc(2 * input->size));
  const char* src_buf = static_cast<const char*>(input->data);

  for (size_t c = 0; c < 2; c++) {
    for (size_t i = 0; i < input->size; i++) {
      new_buf[i + input->size * c] = src_buf[i];
    }
  }

  // Free old value.
  free(input->data);
  // Update structure.
  input->size = 2 * input->size;
  input->data = new_buf;
  return 1;
}

extern "C" const void* get_raw_c_string() { return "Ten chars."; }

extern "C" void nop() {}

// The "no tail-call" annotation and the additional indirection ensure that
// either this function or its calling function shows up in the violation stack
// trace. Otherwise, depending on optimization level and optimizer
// aggressiveness, functions may be inlined, hoisted or omitted (in case of tail
// calls).
static ABSL_ATTRIBUTE_NOINLINE ABSL_ATTRIBUTE_NO_TAIL_CALL void
ViolateIndirect() {
  ptrace((__ptrace_request)990, 991, 992, 993);
}

// Using block syntax here, because SAPI's libclang based header generator won't
// parse the attribute annotations otherwise.
extern "C" {
ABSL_ATTRIBUTE_NOINLINE ABSL_ATTRIBUTE_NO_TAIL_CALL void violate() {
  ViolateIndirect();
}
}
