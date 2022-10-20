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

#include "sandboxed_api/embed_file.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include "absl/strings/string_view.h"
#include "sandboxed_api/sandbox2/util.h"
#include "sandboxed_api/util/fileops.h"
#include "sandboxed_api/util/raw_logging.h"
#include "sandboxed_api/util/strerror.h"

namespace sapi {

EmbedFile* EmbedFile::instance() {
  static auto* embed_file_instance = new EmbedFile();
  return embed_file_instance;
}

int EmbedFile::CreateFdForFileToc(const FileToc* toc) {
  // Create a memfd/temp file and write contents of the SAPI library to it.
  int fd = -1;
  if (!sandbox2::util::CreateMemFd(&fd, toc->name)) {
    SAPI_RAW_LOG(ERROR, "Couldn't create a temporary file for TOC name '%s'",
                 toc->name);
    return -1;
  }
  file_util::fileops::FDCloser embed_fd(fd);

  if (!file_util::fileops::WriteToFD(embed_fd.get(), toc->data, toc->size)) {
    SAPI_RAW_PLOG(ERROR, "Couldn't write SAPI embed file '%s' to memfd file",
                  toc->name);
    return -1;
  }

  // Make the underlying file non-writeable.
  if (fchmod(embed_fd.get(),
             S_IRUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) == -1) {
    SAPI_RAW_PLOG(ERROR, "Could't make FD=%d RX-only", embed_fd.get());
    return -1;
  }

  // Ideally, we'd seal the file here using fcntl(). However, in rare cases,
  // adding the file seals F_SEAL_SEAL | F_SEAL_SHRINK | F_SEAL_GROW |
  // F_SEAL_WRITE results in EBUSY errors.
  // This is likely because of an interaction of SEAL_WRITE with pending writes
  // to the mapped memory region (see memfd_wait_for_pins() in Linux'
  // mm/memfd.c). Since fsync() is a no-op on memfds, it doesn't help to
  // ameliorate the problem.

  return embed_fd.Release();
}

int EmbedFile::GetFdForFileToc(const FileToc* toc) {
  // Access to file_tocs_ must be guarded.
  absl::MutexLock lock{&file_tocs_mutex_};

  // If a file-descriptor for this toc already exists, just return it.
  auto entry = file_tocs_.find(toc);
  if (entry != file_tocs_.end()) {
    SAPI_RAW_VLOG(3,
                  "Returning pre-existing embed file entry for '%s', fd: %d "
                  "(orig name: '%s')",
                  toc->name, entry->second, entry->first->name);
    return entry->second;
  }

  int embed_fd = CreateFdForFileToc(toc);
  if (embed_fd == -1) {
    SAPI_RAW_LOG(ERROR, "Cannot create a file for FileTOC: '%s'", toc->name);
    return -1;
  }

  SAPI_RAW_VLOG(1, "Created new embed file entry for '%s' with fd: %d",
                toc->name, embed_fd);

  file_tocs_[toc] = embed_fd;
  return embed_fd;
}

int EmbedFile::GetDupFdForFileToc(const FileToc* toc) {
  int fd = GetFdForFileToc(toc);
  if (fd == -1) {
    return -1;
  }
  fd = dup(fd);
  if (fd == -1) {
    SAPI_RAW_PLOG(ERROR, "dup failed");
  }
  return fd;
}

}  // namespace sapi
