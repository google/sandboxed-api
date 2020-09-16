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
#include <iostream>

#include "sandboxed_api/vars.h"

// Function taken from curl's getinmemory.c
size_t WriteMemoryCallback(void* contents, size_t size, size_t nmemb,
                           void* userp) {
  size_t real_size = size * nmemb;
  auto* mem = static_cast<sapi::LenValStruct*>(userp);

  char* ptr = static_cast<char*>(realloc(mem->data, mem->size + real_size + 1));
  if (ptr == nullptr) {  // Out of memory
    std::cout << "not enough memory (realloc returned NULL)\n";
    return 0;
  }

  mem->data = ptr;
  auto data = static_cast<char*>(mem->data);
  memcpy(&(data[mem->size]), contents, real_size);
  mem->size += real_size;
  data[mem->size] = 0;

  return real_size;
}
