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

#ifndef CALLBACKS_H
#define CALLBACKS_H

#include <curl/curl.h>

extern "C" struct MemoryStruct {
  char* memory;
  size_t size;
};

extern "C" size_t WriteMemoryCallback(void* contents, size_t size, 
                                      size_t nmemb, void* userp);

#endif // CALLBACKS_H
