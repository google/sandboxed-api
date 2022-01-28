// Copyright 2020 Google LLC
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

#include "callbacks.h"  // NOLINT(build/include)

#include <cstdlib>
#include <cstring>

#include "sandboxed_api/vars.h"

size_t WriteToMemory(char* contents, size_t size, size_t num_bytes,
                     void* userp) {
  size_t real_size = size * num_bytes;
  auto* mem = static_cast<sapi::LenValStruct*>(userp);

  char* ptr = static_cast<char*>(realloc(mem->data, mem->size + real_size + 1));
  if (ptr == nullptr) return 0;

  mem->data = ptr;
  auto data = static_cast<char*>(mem->data);
  memcpy(&(data[mem->size]), contents, real_size);
  mem->size += real_size;
  data[mem->size] = 0;

  return real_size;
}
