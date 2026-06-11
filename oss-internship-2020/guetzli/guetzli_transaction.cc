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

#include "guetzli_transaction.h"  // NOLINT(build/include)

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <iostream>
#include <memory>

#include "absl/status/statusor.h"

namespace guetzli::sandbox {

absl::Status GuetzliTransaction::Main() {
  sapi::v::Fd in_fd(open(params_.in_file, O_RDONLY));

  if (in_fd.GetValue() < 0) {
    return absl::FailedPreconditionError("Error opening input file");
  }

  SAPI_ASSIGN_OR_RETURN(image_type_, GetImageTypeFromFd(in_fd.GetValue()));
  SAPI_RETURN_IF_ERROR(sandbox()->TransferToSandboxee(&in_fd));

  if (in_fd.GetRemoteFd() < 0) {
    return absl::FailedPreconditionError(
        "Error receiving remote FD: remote input fd is set to -1");
  }

  GuetzliApi api(sandbox());
  sapi::v::LenVal output(0);

  sapi::v::Struct<ProcessingParams> processing_params;
  *processing_params.mutable_data() = {in_fd.GetRemoteFd(), params_.verbose,
                                       params_.quality, params_.memlimit_mb};

  auto result =
      image_type_ == ImageType::kJpeg
          ? api.ProcessJpeg(processing_params.PtrBefore(), output.PtrBefore())
          : api.ProcessRgb(processing_params.PtrBefore(), output.PtrBefore());

  if (!result.value_or(false)) {
    return absl::FailedPreconditionError(absl::StrCat(
        "Error processing ", (image_type_ == ImageType::kJpeg ? "jpeg" : "rgb"),
        " data"));
  }

  sapi::v::Fd out_fd(open(".", O_TMPFILE | O_RDWR, S_IRUSR | S_IWUSR));
  if (out_fd.GetValue() < 0) {
    return absl::FailedPreconditionError("Error creating temp output file");
  }

  SAPI_RETURN_IF_ERROR(sandbox()->TransferToSandboxee(&out_fd));

  if (out_fd.GetRemoteFd() < 0) {
    return absl::FailedPreconditionError(
        "Error receiving remote FD: remote output fd is set to -1");
  }

  auto write_result = api.WriteDataToFd(out_fd.GetRemoteFd(), output.PtrNone());

  if (!write_result.value_or(false)) {
    return absl::FailedPreconditionError("Error writing file inside sandbox");
  }

  SAPI_RETURN_IF_ERROR(LinkOutFile(out_fd.GetValue()));

  return absl::OkStatus();
}

absl::Status GuetzliTransaction::LinkOutFile(int out_fd) const {
  if (access(params_.out_file, F_OK) != -1) {
    if (remove(params_.out_file) < 0) {
      return absl::FailedPreconditionError(absl::StrCat(
          "Error deleting existing output file: ", params_.out_file));
    }
  }

  std::string path = absl::StrCat("/proc/self/fd/", out_fd);

  if (linkat(AT_FDCWD, path.c_str(), AT_FDCWD, params_.out_file,
             AT_SYMLINK_FOLLOW) < 0) {
    return absl::FailedPreconditionError(
        absl::StrCat("Error linking: ", params_.out_file));
  }

  return absl::OkStatus();
}

absl::StatusOr<ImageType> GuetzliTransaction::GetImageTypeFromFd(int fd) const {
  static const unsigned char kPNGMagicBytes[] = {
      0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n',
  };
  char read_buf[sizeof(kPNGMagicBytes)];

  if (read(fd, read_buf, sizeof(kPNGMagicBytes)) != sizeof(kPNGMagicBytes)) {
    return absl::FailedPreconditionError(
        "Error determining type of the input file");
  }

  if (lseek(fd, 0, SEEK_SET) != 0) {
    return absl::FailedPreconditionError(
        "Error returnig cursor to the beginning");
  }

  return memcmp(read_buf, kPNGMagicBytes, sizeof(kPNGMagicBytes)) == 0
             ? ImageType::kPng
             : ImageType::kJpeg;
}

}  // namespace guetzli::sandbox
