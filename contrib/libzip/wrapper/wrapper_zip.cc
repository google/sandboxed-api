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

#include "contrib/libzip/wrapper/wrapper_zip.h"

#include <fcntl.h>
#include <unistd.h>

#include <iostream>

#include "absl/cleanup/cleanup.h"

constexpr size_t kFileMaxSize = 1024 * 1024 * 1024;  // 1GB

void* zip_source_filefd(zip_t* archive, int fd, const char* mode,
                        zip_uint64_t start, zip_int64_t len) {
  FILE* pfile;

  // The file stream is closed when the source is being freed,
  // usually by zip_close(3).
  pfile = fdopen(fd, mode);
  if (pfile == nullptr) {
    return nullptr;
  }

  return zip_source_filep(archive, pfile, start, len);
}

void* zip_source_filefd_create(int fd, const char* mode, zip_uint64_t start,
                               zip_int64_t len, zip_error_t* ze) {
  FILE* pfile;

  pfile = fdopen(fd, mode);
  if (pfile == nullptr) {
    return nullptr;
  }

  return zip_source_filep_create(pfile, start, len, ze);
}

off_t FDGetSize(int fd) {
  off_t size = lseek(fd, 0, SEEK_END);
  if (size < 0) {
    return -1;
  }
  if (lseek(fd, 0, SEEK_SET) < 0) {
    return -1;
  }

  return size;
}

void* zip_read_fd_to_source(int fd, zip_error_t* ze) {
  off_t sizein = FDGetSize(fd);
  if (sizein > kFileMaxSize) {
    return nullptr;
  }

  int8_t* buf = reinterpret_cast<int8_t*>(malloc(sizein));
  if (buf == nullptr) {
    return nullptr;
  }

  if (read(fd, buf, sizein) != sizein) {
    free(buf);
    return nullptr;
  }

  zip_source_t* src = zip_source_buffer_create(buf, sizein, 1, ze);
  if (src == nullptr) {
    free(buf);
    return nullptr;
  }

  return src;
}

// This function is not atomic. Maybe it should be?
bool zip_source_to_fd(zip_source_t* src, int fd) {
  if (lseek(fd, 0, SEEK_SET) < 0) {
    return false;
  }
  if (ftruncate(fd, 0) < 0) {
    return false;
  }

  if (zip_source_open(src) < 0) {
    return false;
  }
  absl::Cleanup src_cleanup = [&src] { zip_source_close(src); };

  if (zip_source_seek(src, 0, SEEK_SET) < 0) {
    return false;
  }

  int8_t buf[4096];
  size_t size;
  while (true) {
    size = zip_source_read(src, &buf, sizeof(buf));
    if (size <= 0) {
      break;
    }
    if (write(fd, &buf, size) != size) {
      return false;
    }
  }

  return size == 0;
}
