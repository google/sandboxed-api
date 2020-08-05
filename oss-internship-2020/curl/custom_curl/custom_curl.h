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

// wrapper for curl library used to implement variadic functions explicitly

#ifndef CUSTOM_CURL_H
#define CUSTOM_CURL_H

#include <curl/curlver.h>
#include <curl/system.h>
#include <curl/curl.h>

extern "C" CURLcode curl_easy_setopt_ptr(CURL *handle, CURLoption option, void* parameter);

extern "C" CURLcode curl_easy_setopt_long(CURL *handle, CURLoption option, long parameter);

extern "C" CURLcode curl_easy_setopt_curl_off_t(CURL *handle, CURLoption option, curl_off_t parameter);

extern "C" CURLcode curl_easy_getinfo_ptr(CURL *handle, CURLINFO option, void* parameter);

#endif // CUSTOM_CURL_H