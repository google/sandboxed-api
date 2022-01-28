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

#include "curl_wrapper.h"  // NOLINT(build/include)

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

CURLcode curl_easy_getinfo_ptr(CURL* handle, CURLINFO option, void* parameter) {
  return curl_easy_getinfo(handle, option, parameter);
}

time_t_sapi curl_getdate_sapi(char* datestring, time_t_sapi* now) {
  return curl_getdate(datestring, now);
}

CURLMcode curl_multi_fdset_sapi(CURLM* multi_handle, fd_set_sapi* read_fd_set,
                                fd_set_sapi* write_fd_set,
                                fd_set_sapi* exc_fd_set, int* max_fd) {
  return curl_multi_fdset(multi_handle, read_fd_set, write_fd_set, exc_fd_set,
                          max_fd);
}

CURLMcode curl_multi_setopt_ptr(CURLM* handle, CURLMoption option,
                                void* parameter) {
  return curl_multi_setopt(handle, option, parameter);
}

CURLMcode curl_multi_setopt_long(CURLM* handle, CURLMoption option,
                                 long parameter) {
  return curl_multi_setopt(handle, option, parameter);
}

CURLMcode curl_multi_setopt_curl_off_t(CURLM* handle, CURLMoption option,
                                       curl_off_t parameter) {
  return curl_multi_setopt(handle, option, parameter);
}

CURLMcode curl_multi_poll_sapi(CURLM* multi_handle,
                               struct curl_waitfd* extra_fds,
                               unsigned int extra_nfds, int timeout_ms,
                               int* numfds) {
  return curl_multi_poll(multi_handle, extra_fds, extra_nfds, timeout_ms,
                         numfds);
}

CURLMcode curl_multi_wait_sapi(CURLM* multi_handle,
                               struct curl_waitfd* extra_fds,
                               unsigned int extra_nfds, int timeout_ms,
                               int* numfds) {
  return curl_multi_wait(multi_handle, extra_fds, extra_nfds, timeout_ms,
                         numfds);
}

CURLSHcode curl_share_setopt_ptr(CURLSH* handle, CURLSHoption option,
                                 void* parameter) {
  return curl_share_setopt(handle, option, parameter);
}

CURLSHcode curl_share_setopt_long(CURLSH* handle, CURLSHoption option,
                                  long parameter) {
  return curl_share_setopt(handle, option, parameter);
}
