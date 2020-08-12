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

// Wrapper for curl library

#ifndef CUSTOM_CURL_H
#define CUSTOM_CURL_H

#include <curl/curl.h>

// The wrapper method is needed to make the variadic argument explicit
extern "C" CURLcode curl_easy_setopt_ptr(CURL* handle, CURLoption option, 
                                         void* parameter);

// The wrapper method is needed to make the variadic argument explicit
extern "C" CURLcode curl_easy_setopt_long(CURL* handle, CURLoption option, 
                                          long parameter);

// The wrapper method is needed to make the variadic argument explicit
extern "C" CURLcode curl_easy_setopt_curl_off_t(CURL* handle, CURLoption option,
                                                curl_off_t parameter);

// The wrapper method is needed to make the variadic argument explicit
extern "C" CURLcode curl_easy_getinfo_ptr(CURL* handle, CURLINFO option, 
                                          void* parameter);

// The typedef and wrapper method are needed because the original method has
// some conflicts in curl_sapi.sapi.h
extern "C" typedef time_t time_t_sapi;
extern "C" time_t_sapi curl_getdate_sapi(char *datestring, time_t_sapi *now );

// The typedef and wrapper method are needed because the original method has
// some conflicts in curl_sapi.sapi.h
extern "C" typedef fd_set fd_set_sapi;
extern "C" CURLMcode curl_multi_fdset_sapi(CURLM *multi_handle,
                                           fd_set_sapi *read_fd_set,
                                           fd_set_sapi *write_fd_set,
                                           fd_set_sapi *exc_fd_set,
                                           int *max_fd);

// The wrapper method is needed to make the variadic argument explicit
extern "C" CURLMcode curl_multi_setopt_ptr(CURLM* handle, CURLMoption option, 
                                           void* parameter);

// The wrapper method is needed to make the variadic argument explicit
extern "C" CURLMcode curl_multi_setopt_long(CURLM* handle, CURLMoption option, 
                                            long parameter);

// The wrapper method is needed to make the variadic argument explicit
extern "C" CURLMcode curl_multi_setopt_curl_off_t(CURLM* handle, 
                                                  CURLMoption option,
                                                  curl_off_t parameter);

// The wrapper method is needed because incomplete array type is not supported
extern "C" CURLMcode curl_multi_poll_sapi(CURLM *multi_handle,
                                          struct curl_waitfd* extra_fds,
                                          unsigned int extra_nfds,
                                          int timeout_ms,
                                          int *numfds);

// The wrapper method is needed because incomplete array type is not supported
extern "C" CURLMcode curl_multi_wait_sapi(CURLM *multi_handle,
                                          struct curl_waitfd* extra_fds,
                                          unsigned int extra_nfds,
                                          int timeout_ms,
                                          int *numfds);

// The wrapper method is needed to make the variadic argument explicit
extern "C" CURLSHcode curl_share_setopt_ptr(CURLSH* handle, CURLSHoption option, 
                                            void* parameter);

// The wrapper method is needed to make the variadic argument explicit
extern "C" CURLSHcode curl_share_setopt_long(CURLSH* handle, CURLSHoption option, 
                                             long parameter);

#endif // CUSTOM_CURL_H
