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

#ifndef CONTRIB_LIBZIP_UTILS_UTILS_ZIP_H_
#define CONTRIB_LIBZIP_UTILS_UTILS_ZIP_H_

#include <fcntl.h>

#include "absl/log/die_if_null.h"
#include "contrib/libzip/sandboxed.h"
#include "sandboxed_api/util/status_macros.h"

class LibZip {
 public:
  explicit LibZip(ZipSandbox* sandbox, std::string filename, int flags)
      : sandbox_(ABSL_DIE_IF_NULL(sandbox)),
        api_(sandbox_),
        filename_(std::move(filename)),
        flags_(flags),
        rfd_(open(filename.c_str(), O_RDWR | O_CREAT)) {
    OpenRemote().IgnoreError();
  }

  ~LibZip();

  bool IsOpen();

  absl::StatusOr<std::string> GetName(uint64_t index);
  absl::StatusOr<uint64_t> GetNumberEntries();
  absl::StatusOr<std::vector<uint8_t>> ReadFile(uint64_t index);
  absl::StatusOr<std::vector<uint8_t>> ReadFile(const std::string& filename);
  absl::StatusOr<uint64_t> AddFile(const std::string& filename,
                                   std::vector<uint8_t>& buf);
  absl::StatusOr<uint64_t> AddFile(const std::string& filename, int fd);
  absl::Status ReplaceFile(uint64_t index, std::vector<uint8_t>& buf);
  absl::Status ReplaceFile(uint64_t index, int fd);
  absl::Status DeleteFile(uint64_t index);

  absl::StatusOr<std::string> GetError();

  absl::Status Finish();
  // Save a copy of file to another fd.
  absl::Status Save(int fd);
  // Save a copy to the same fd.
  absl::Status Save();

 protected:
  bool IsOpenLocal();
  absl::Status OpenRemote();
  absl::StatusOr<std::vector<uint8_t>> ReadFile(sapi::v::RemotePtr& zipfile,
                                                uint32_t size);
  absl::StatusOr<uint64_t> AddFile(const std::string& filename,
                                   sapi::v::RemotePtr& rzipsource);
  absl::Status ReplaceFile(uint64_t index, sapi::v::RemotePtr& rzipsource);

  absl::StatusOr<void*> GetSource(std::vector<uint8_t>& buf);
  absl::StatusOr<void*> GetSource(int fd, const std::string& mode);
  absl::StatusOr<void*> CreateSourceFromFd(sapi::v::Fd& rfd);

  absl::Status Save(sapi::v::Fd& fd);

  absl::Status CheckOpen();
  absl::Status CheckFinished();

 private:
  ZipSandbox* sandbox_;
  ZipApi api_;
  int flags_;
  std::unique_ptr<sapi::v::RemotePtr> zip_;
  std::unique_ptr<sapi::v::RemotePtr> zipsource_;
  sapi::v::Fd rfd_;
  std::string filename_;
};

#endif  // CONTRIB_LIBZIP_UTILS_UTILS_ZIP_H_
