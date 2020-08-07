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

#include <cstdlib>
#include <iostream>

#include "custom_curl.h"

CURLcode curl_easy_setopt_ptr(CURL* handle, CURLoption option, 
                              void* parameter) {
  return curl_easy_setopt(handle, option, parameter);
}

CURLcode curl_easy_setopt_long(CURL* handle, CURLoption option, 
                               long parameter) {
  return curl_easy_setopt(handle, option, parameter);
}

CURLcode curl_easy_setopt_curl_off_t(CURL* handle, CURLoption option, 
                                     curl_off_t parameter) {
  return curl_easy_setopt(handle, option, parameter);
}

CURLcode curl_easy_getinfo_ptr(CURL* handle, CURLINFO option, 
                               void* parameter) {
  return curl_easy_getinfo(handle, option, parameter);
}
