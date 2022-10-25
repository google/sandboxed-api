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

#include "absl/status/statusor.h"

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

  // Creates a new Buffer that is backed by the specified file descriptor.
  // The Buffer takes ownership of the descriptor and will close it when
  // destroyed.
  static absl::StatusOr<std::unique_ptr<Buffer>> CreateFromFd(int fd);

  // Creates a new Buffer of the specified size, backed by a temporary file that
  // will be immediately deleted.
  static absl::StatusOr<std::unique_ptr<Buffer>> CreateWithSize(size_t size);

  // Returns a pointer to the buffer, which is read/write.
  uint8_t* data() const { return buf_; }

  // Gets the size of the buffer in bytes.
  size_t size() const { return size_; }

  // Gets the file descriptor backing the buffer.
  int fd() const { return fd_; }

 private:
  Buffer() = default;

  uint8_t* buf_ = nullptr;
  int fd_ = -1;
  size_t size_ = 0;
};

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_BUFFER_H_
