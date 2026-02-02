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

#ifndef SANDBOXED_API_SANDBOX2_BUFFER_H_
#define SANDBOXED_API_SANDBOX2_BUFFER_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

#include "absl/base/macros.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "sandboxed_api/util/fileops.h"

namespace sandbox2 {

// Buffer provides a way for executor and sandboxee to share data.
// It is useful to share large buffers instead of communicating and copying.
// The executor must distrust the content of this buffer, like everything
// else that comes under control of the sandboxee.
class Buffer final {
 public:
  ~Buffer();

  Buffer(const Buffer&) = delete;
  Buffer& operator=(const Buffer&) = delete;

  // Creates a new Buffer that is backed by the specified file descriptor, size
  // is determined by the size of the file.
  static absl::StatusOr<std::unique_ptr<Buffer>> CreateFromFd(
      sapi::file_util::fileops::FDCloser fd);
  // Creates a new Buffer that is backed by the specified file descriptor with
  // given size.
  static absl::StatusOr<std::unique_ptr<Buffer>> CreateFromFd(
      sapi::file_util::fileops::FDCloser fd, size_t size);
  ABSL_DEPRECATE_AND_INLINE()
  static absl::StatusOr<std::unique_ptr<Buffer>> CreateFromFd(int fd) {
    return CreateFromFd(sapi::file_util::fileops::FDCloser(fd));
  }

  // Creates a new Buffer of the specified size, backed by a temporary file
  // (using memfd_create) that will be immediately deleted.
  static absl::StatusOr<std::unique_ptr<Buffer>> CreateWithSize(
      size_t size, const char* name = "buffer_file");

  // Expands the input buffer to the specified size.
  // Unlike CreateWithSize, this function will pre-allocate the memory.
  // If size is smaller than the current mapped size, the function will fail.
  static absl::StatusOr<std::unique_ptr<Buffer>> Expand(
      std::unique_ptr<Buffer> other, size_t size);

  // Returns a pointer to the buffer, which is read/write.
  uint8_t* data() const { return buf_; }

  // Gets the size of the buffer in bytes.
  size_t size() const { return size_; }

  // Gets the file descriptor backing the buffer.
  int fd() const { return fd_.get(); }

 private:
  Buffer(sapi::file_util::fileops::FDCloser fd, uint8_t* buf, size_t size)
      : buf_(buf), fd_(std::move(fd)), size_(size) {}

  uint8_t* buf_ = nullptr;
  sapi::file_util::fileops::FDCloser fd_;
  size_t size_ = 0;
};

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_BUFFER_H_
