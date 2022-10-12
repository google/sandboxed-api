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

#include "contrib/libzip/utils/utils_zip.h"

#include <fstream>
#include <iostream>
#include <string>

#include "absl/cleanup/cleanup.h"
#include "contrib/libzip/sandboxed.h"

constexpr uint64_t kFileMaxSize = 1024 * 1024 * 1024;  // 1GB

#define ZIP_FL_ENC_GUESS 0
#define ZIP_FL_OVERWRITE 8192u

LibZip::~LibZip() {
  if (IsOpen()) {
    api_.zip_close(zip_.get()).IgnoreError();
  }
  api_.zip_source_free(&(*zipsource_)).IgnoreError();
}

bool LibZip::IsOpen() { return zip_ != nullptr; }

bool LibZip::IsOpenLocal() { return rfd_.GetValue() >= 0; }

absl::Status LibZip::CheckOpen() {
  if (!IsOpen()) {
    return absl::UnavailableError("Modification stage finished");
  }

  return absl::OkStatus();
}

absl::Status LibZip::CheckFinished() {
  if (IsOpen()) {
    return absl::UnavailableError("Still in modification stage");
  }

  return absl::OkStatus();
}

absl::Status LibZip::OpenRemote() {
  if (!IsOpenLocal()) {
    return absl::UnavailableError("Zip file is not open");
  }

  SAPI_RETURN_IF_ERROR(sandbox_->TransferToSandboxee(&rfd_));

  SAPI_ASSIGN_OR_RETURN(void* zipsource, CreateSourceFromFd(rfd_));
  zipsource_ = std::make_unique<sapi::v::RemotePtr>(zipsource);

  sapi::v::NullPtr null_ptr;
  absl::StatusOr<zip_t*> status_or_zip =
      api_.zip_open_from_source(&(*zipsource_), flags_, &null_ptr);
  if (!status_or_zip.ok() || *status_or_zip == nullptr) {
    api_.zip_source_free(&(*zipsource_)).IgnoreError();
    zipsource_ = nullptr;
    return absl::UnavailableError("Unable to open remote");
  }

  SAPI_RETURN_IF_ERROR(api_.zip_source_keep(&(*zipsource_)));

  zip_ = std::make_unique<sapi::v::RemotePtr>(*status_or_zip);

  return absl::OkStatus();
}

absl::Status LibZip::Finish() {
  SAPI_ASSIGN_OR_RETURN(int ret, api_.zip_close(zip_.get()));
  if (ret < 0) {
    return absl::UnavailableError("Unable to close remote");
  }
  zip_ = nullptr;

  return absl::OkStatus();
}

absl::Status LibZip::Save(int fd) {
  SAPI_RETURN_IF_ERROR(CheckFinished());
  sapi::v::Fd rfd(fd);
  SAPI_RETURN_IF_ERROR(sandbox_->TransferToSandboxee(&rfd));
  return Save(rfd);
}

absl::Status LibZip::Save() {
  SAPI_RETURN_IF_ERROR(CheckFinished());
  return Save(rfd_);
}

absl::Status LibZip::Save(sapi::v::Fd& rfd) {
  SAPI_RETURN_IF_ERROR(CheckFinished());
  SAPI_ASSIGN_OR_RETURN(
      bool ret, api_.zip_source_to_fd(&(*zipsource_), rfd.GetRemoteFd()));

  if (!ret) {
    return absl::UnavailableError("Unable to store data");
  }

  return absl::OkStatus();
}

absl::StatusOr<std::string> LibZip::GetName(uint64_t index) {
  SAPI_RETURN_IF_ERROR(CheckOpen());
  SAPI_ASSIGN_OR_RETURN(const char* name,
                        api_.zip_get_name(zip_.get(), index, ZIP_FL_ENC_GUESS));
  if (name == nullptr) {
    return absl::UnavailableError("Unable to find name under index");
  }

  return sandbox_->GetCString(sapi::v::RemotePtr(const_cast<char*>(name)));
}

absl::StatusOr<uint64_t> LibZip::GetNumberEntries() {
  SAPI_RETURN_IF_ERROR(CheckOpen());
  SAPI_ASSIGN_OR_RETURN(int64_t num, api_.zip_get_num_entries(zip_.get(), 0));
  if (num < 0) {
    /* Imposible as zip != nullptr */
    return absl::UnavailableError("Internal error");
  }
  return num;
}

absl::StatusOr<std::vector<uint8_t>> LibZip::ReadFile(
    sapi::v::RemotePtr& rzipfile, uint32_t size) {
  SAPI_RETURN_IF_ERROR(CheckOpen());
  if (size > kFileMaxSize) {
    return absl::UnavailableError("File is to large");
  }

  std::vector<uint8_t> buf(size);
  sapi::v::Array<uint8_t> arr(buf.data(), size);

  SAPI_ASSIGN_OR_RETURN(uint64_t ret,
                        api_.zip_fread(&rzipfile, arr.PtrAfter(), size));
  if (ret != size) {
    return absl::UnavailableError("Unable to read file");
  }

  return buf;
}

absl::StatusOr<std::vector<uint8_t>> LibZip::ReadFile(
    const std::string& filename) {
  SAPI_RETURN_IF_ERROR(CheckOpen());
  sapi::v::Struct<zip_stat_t> zipstat;
  sapi::v::ConstCStr cfilename(filename.c_str());

  SAPI_ASSIGN_OR_RETURN(
      int err,
      api_.zip_stat(zip_.get(), cfilename.PtrBefore(), 0, zipstat.PtrAfter()));
  if (err < 0) {
    return absl::UnavailableError("Unable to get file stat");
  }

  SAPI_ASSIGN_OR_RETURN(zip_file_t * zipfile,
                        api_.zip_fopen(zip_.get(), cfilename.PtrBefore(), 0));
  if (zipfile == nullptr) {
    return absl::UnavailableError("Unable to open file in archaive");
  }

  sapi::v::RemotePtr rzipfile(zipfile);
  absl::Cleanup rzipfile_cleanup = [this, &rzipfile] {
    api_.zip_fclose(&rzipfile).IgnoreError();
  };
  return ReadFile(rzipfile, zipstat.mutable_data()->size);
}

absl::StatusOr<std::vector<uint8_t>> LibZip::ReadFile(uint64_t index) {
  SAPI_RETURN_IF_ERROR(CheckOpen());
  sapi::v::Struct<zip_stat_t> zipstat;

  SAPI_ASSIGN_OR_RETURN(
      int err, api_.zip_stat_index(zip_.get(), index, 0, zipstat.PtrAfter()));
  if (err < 0) {
    return absl::UnavailableError("Unable to get file stat");
  }

  SAPI_ASSIGN_OR_RETURN(zip_file_t * zipfile,
                        api_.zip_fopen_index(zip_.get(), index, 0));
  if (zipfile == nullptr) {
    return absl::UnavailableError("Unable to open file in archaive");
  }
  sapi::v::RemotePtr rzipfile(zipfile);
  absl::Cleanup rzipfile_cleanup = [this, &rzipfile] {
    api_.zip_fclose(&rzipfile).IgnoreError();
  };

  return ReadFile(rzipfile, zipstat.mutable_data()->size);
}

absl::StatusOr<uint64_t> LibZip::AddFile(const std::string& filename,
                                         sapi::v::RemotePtr& rzipsource) {
  SAPI_RETURN_IF_ERROR(CheckOpen());
  sapi::v::ConstCStr cfilename(filename.c_str());

  SAPI_ASSIGN_OR_RETURN(int64_t index,
                        api_.zip_file_add(zip_.get(), cfilename.PtrBefore(),
                                          &rzipsource, ZIP_FL_OVERWRITE));
  if (index < 0) {
    SAPI_ASSIGN_OR_RETURN(std::string errs, GetError());
    api_.zip_source_free(&rzipsource).IgnoreError();
    return absl::UnavailableError("Unable to add file");
  }

  return index;
}

absl::StatusOr<void*> LibZip::CreateSourceFromFd(sapi::v::Fd& rfd) {
  sapi::v::NullPtr null_ptr;

  SAPI_ASSIGN_OR_RETURN(void* zipsource, api_.zip_read_fd_to_source(
                                             rfd.GetRemoteFd(), &null_ptr));
  if (zipsource == nullptr) {
    return absl::UnavailableError("Unable to create buffer");
  }

  return zipsource;
}

absl::StatusOr<void*> LibZip::GetSource(std::vector<uint8_t>& buf) {
  sapi::v::Array<uint8_t> arr(buf.data(), buf.size());

  SAPI_ASSIGN_OR_RETURN(
      zip_source_t * zipsource,
      api_.zip_source_buffer(zip_.get(), arr.PtrBefore(), arr.GetSize(),
                             1 /* autofree */));
  if (zipsource == nullptr) {
    return absl::UnavailableError("Unable to create buffer");
  }
  arr.SetRemote(nullptr);  // We don't want to free automaticlt buffer
                           // leave memory menagment to the zip process
  return zipsource;
}

absl::StatusOr<void*> LibZip::GetSource(int fd, const std::string& mode) {
  sapi::v::Fd rfd(fd);
  SAPI_RETURN_IF_ERROR(sandbox_->TransferToSandboxee(&rfd));

  sapi::v::ConstCStr cmode(mode.c_str());
  SAPI_ASSIGN_OR_RETURN(void* zipsource,
                        api_.zip_source_filefd(zip_.get(), rfd.GetRemoteFd(),
                                               cmode.PtrBefore(), 0, 0));
  if (zipsource == nullptr) {
    return absl::UnavailableError("Unable to create buffer");
  }
  rfd.OwnRemoteFd(false);

  return zipsource;
}

absl::StatusOr<uint64_t> LibZip::AddFile(const std::string& filename,
                                         std::vector<uint8_t>& buf) {
  SAPI_RETURN_IF_ERROR(CheckOpen());
  SAPI_ASSIGN_OR_RETURN(void* zipsource, GetSource(buf));
  sapi::v::RemotePtr rzipsource(zipsource);
  return AddFile(filename, rzipsource);
}

absl::StatusOr<uint64_t> LibZip::AddFile(const std::string& filename, int fd) {
  SAPI_RETURN_IF_ERROR(CheckOpen());
  SAPI_ASSIGN_OR_RETURN(void* zipsource, GetSource(fd, "rb"));
  sapi::v::RemotePtr rzipsource(zipsource);
  return AddFile(filename, rzipsource);
}

absl::Status LibZip::ReplaceFile(uint64_t index,
                                 sapi::v::RemotePtr& rzipsource) {
  SAPI_RETURN_IF_ERROR(CheckOpen());
  SAPI_ASSIGN_OR_RETURN(
      int64_t ret, api_.zip_file_replace(zip_.get(), index, &rzipsource, 0));
  if (ret < 0) {
    api_.zip_source_free(&rzipsource).IgnoreError();
    return absl::UnavailableError("Unable to replace file");
  }

  return absl::OkStatus();
}

absl::Status LibZip::ReplaceFile(uint64_t index, int fd) {
  SAPI_RETURN_IF_ERROR(CheckOpen());
  SAPI_ASSIGN_OR_RETURN(void* zipsource, GetSource(fd, "rb"));
  sapi::v::RemotePtr rzipsource(zipsource);
  return ReplaceFile(index, rzipsource);
}

absl::Status LibZip::ReplaceFile(uint64_t index, std::vector<uint8_t>& buf) {
  SAPI_RETURN_IF_ERROR(CheckOpen());
  SAPI_ASSIGN_OR_RETURN(void* zipsource, GetSource(buf));
  sapi::v::RemotePtr rzipsource(zipsource);
  return ReplaceFile(index, rzipsource);
}

absl::Status LibZip::DeleteFile(uint64_t index) {
  SAPI_RETURN_IF_ERROR(CheckOpen());
  SAPI_ASSIGN_OR_RETURN(int ret, api_.zip_delete(zip_.get(), index));
  if (ret < 0) {
    return absl::UnavailableError("Unable to delete file");
  }

  return absl::OkStatus();
}

absl::StatusOr<std::string> LibZip::GetError() {
  SAPI_RETURN_IF_ERROR(CheckOpen());
  SAPI_ASSIGN_OR_RETURN(const char* err, api_.zip_strerror(zip_.get()));
  if (err == nullptr) {
    return absl::UnavailableError("No error");
  }
  return sandbox_->GetCString(sapi::v::RemotePtr(const_cast<char*>(err)));
}
