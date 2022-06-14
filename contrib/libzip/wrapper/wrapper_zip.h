// Copyright 2022 Google LLC
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

#ifndef CONTRIB_LIBZIP_WRAPPER_WRAPPER_ZIP_H_
#define CONTRIB_LIBZIP_WRAPPER_WRAPPER_ZIP_H_

#include <zip.h>
#include <zipconf.h>

extern "C" {

// TODO(cblichmann): zip_source_t used as return value is converted to int.
void* zip_source_filefd(zip_t* archive, int fd, const char* flags,
                        zip_uint64_t start, zip_int64_t len);
void* zip_source_filefd_create(int fd, const char* flags, zip_uint64_t start,
                               zip_int64_t len, zip_error_t* ze);
void* zip_read_fd_to_source(int fd, zip_error_t* ze);
bool zip_source_to_fd(zip_source_t* src, int fd);
}

#endif  // CONTRIB_ZIP_WRAPPER_WRAPPER_ZIP_H_
