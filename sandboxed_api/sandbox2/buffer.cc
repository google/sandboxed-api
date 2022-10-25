// Copyright 2019 Google LLC
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

#include "sandboxed_api/sandbox2/buffer.h"

#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <memory>

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "sandboxed_api/sandbox2/util.h"

namespace sandbox2 {

// Creates a new Buffer that is backed by the specified file descriptor.
absl::StatusOr<std::unique_ptr<Buffer>> Buffer::CreateFromFd(int fd) {
  // Using `new` to access a non-public constructor.
  auto buffer = absl::WrapUnique(new Buffer());

  struct stat stat_buf;
  if (fstat(fd, &stat_buf) != 0) {
    return absl::ErrnoToStatus(errno, "Could not stat buffer fd");
  }
  size_t size = stat_buf.st_size;
  int prot = PROT_READ | PROT_WRITE;
  int flags = MAP_SHARED;
  off_t offset = 0;
  buffer->buf_ =
      reinterpret_cast<uint8_t*>(mmap(nullptr, size, prot, flags, fd, offset));
  if (buffer->buf_ == MAP_FAILED) {
    return absl::ErrnoToStatus(errno, "Could not map buffer fd");
  }
  buffer->fd_ = fd;
  buffer->size_ = size;
  return std::move(buffer);  // GCC 7 needs the move (C++ DR #1579)
}

// Creates a new Buffer of the specified size, backed by a temporary file that
// will be immediately deleted.
absl::StatusOr<std::unique_ptr<Buffer>> Buffer::CreateWithSize(size_t size) {
  int fd;
  if (!util::CreateMemFd(&fd)) {
    return absl::InternalError("Could not create buffer temp file");
  }
  if (ftruncate(fd, size) != 0) {
    return absl::ErrnoToStatus(errno, "Could not extend buffer fd");
  }
  return CreateFromFd(fd);
}

Buffer::~Buffer() {
  if (buf_ != nullptr) {
    munmap(buf_, size_);
  }
  if (fd_ != -1) {
    close(fd_);
  }
}

}  // namespace sandbox2
