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

#ifndef TESTS_CALLBACKS_H
#define TESTS_CALLBACKS_H

#include <curl/curl.h>

// Append contents to the string stored by userp, which is a sapi::LenValStruct*
extern "C" size_t WriteToMemory(char* contents, size_t size, size_t num_bytes,
                                void* userp);

#endif  // TESTS_CALLBACKS_H
