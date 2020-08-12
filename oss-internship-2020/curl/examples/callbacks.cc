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
 
// Function taken from curl's getinmemory.c
size_t WriteMemoryCallback(void* contents, size_t size, size_t nmemb, 
                           void* userp) {

  size_t realsize = size * nmemb;
  struct MemoryStruct* mem = (struct MemoryStruct*)userp;
 
  char* ptr = (char*)realloc(mem->memory, mem->size + realsize + 1);
  if(ptr == NULL) {  // Out of memory
    std::cout << "not enough memory (realloc returned NULL)\n";
    return 0;
  }

  mem->memory = ptr;
  memcpy(&(mem->memory[mem->size]), contents, realsize);
  mem->size += realsize;
  mem->memory[mem->size] = 0;
 
  return realsize;

}
