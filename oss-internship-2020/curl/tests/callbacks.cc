// Copyright 2020 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "callbacks.h"

#include <cstdlib>
#include <cstring>

size_t WriteToMemory(char* contents, size_t size, size_t num_bytes,
                     void* userp) {
  size_t real_size = size * num_bytes;
  auto* mem = static_cast<MemoryStruct*>(userp);

  char* ptr =
      static_cast<char*>(realloc(mem->memory, mem->size + real_size + 1));
  if (ptr == nullptr) return 0;

  mem->memory = ptr;
  memcpy(&(mem->memory[mem->size]), contents, real_size);
  mem->size += real_size;
  mem->memory[mem->size] = 0;

  return real_size;
}
