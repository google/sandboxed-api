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

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "sandboxed_api/sandbox2/util.h"
#include "sandboxed_api/util/fileops.h"

namespace sandbox2 {

using ::sapi::file_util::fileops::FDCloser;

// Creates a new Buffer that is backed by the specified file descriptor.
absl::StatusOr<std::unique_ptr<Buffer>> Buffer::CreateFromFd(FDCloser fd) {
  struct stat stat_buf;
  if (fstat(fd.get(), &stat_buf) != 0) {
    return absl::ErrnoToStatus(errno, "Could not stat buffer fd");
  }
  return CreateFromFd(std::move(fd), stat_buf.st_size);
}

absl::StatusOr<std::unique_ptr<Buffer>> Buffer::CreateFromFd(FDCloser fd,
                                                             size_t size) {
  int prot = PROT_READ | PROT_WRITE;
  int flags = MAP_SHARED;
  off_t offset = 0;
  uint8_t* buf = reinterpret_cast<uint8_t*>(
      mmap(nullptr, size, prot, flags, fd.get(), offset));
  if (buf == MAP_FAILED) {
    return absl::ErrnoToStatus(errno, "Could not map buffer fd");
  }
  // Using `new` to access a non-public constructor.
  return absl::WrapUnique(new Buffer(std::move(fd), buf, size));
}

absl::StatusOr<std::unique_ptr<Buffer>> Buffer::Expand(
    std::unique_ptr<Buffer> other, size_t size) {
  if (!other) {
    return absl::InvalidArgumentError("Buffer is null");
  }
  if (other->buf_ == nullptr) {
    return absl::FailedPreconditionError("Buffer is not initialized");
  }
  if (other->fd_.get() < 0) {
    return absl::FailedPreconditionError("Buffer is not backed by a valid fd");
  }
  if (size < other->size_) {
    return absl::InvalidArgumentError("Buffer size cannot be reduced");
  }
  if (other->size_ == size) {
    return other;
  }
  if (fallocate(other->fd_.get(), 0, 0, size) != 0) {
    return absl::ErrnoToStatus(errno, "Could not extend buffer fd");
  }
  uint8_t* new_buf_ = reinterpret_cast<uint8_t*>(
      mremap(other->buf_, other->size_, size, MREMAP_MAYMOVE));
  if (new_buf_ == MAP_FAILED) {
    // At this point fallocate succeeded, and remapping failed.
    // The incoming buffer is in an undefined state. It will be destroyed
    // when this function returns.
    return absl::ErrnoToStatus(errno, "Could not map buffer fd");
  }
  other->buf_ = new_buf_;
  other->size_ = size;
  return other;
}

// Creates a new Buffer of the specified size, backed by a temporary file that
// will be immediately deleted.
absl::StatusOr<std::unique_ptr<Buffer>> Buffer::CreateWithSize(
    size_t size, const char* name) {
  absl::StatusOr<FDCloser> fd = util::CreateMemFd(name);
  if (!fd.ok()) {
    return fd.status();
  }
  if (ftruncate(fd->get(), size) != 0) {
    return absl::ErrnoToStatus(errno, "Could not extend buffer fd");
  }
  return CreateFromFd(*std::move(fd), size);
}

std::string Buffer::GetName() const {
  return sapi::file_util::fileops::ReadLink(
      absl::StrCat("/proc/self/fd/", fd_.get()));
}

Buffer::~Buffer() {
  if (buf_ != nullptr) {
    munmap(buf_, size_);
  }
}

}  // namespace sandbox2
