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

#include <dlfcn.h>
#include <fcntl.h>
#include <sys/auxv.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <string>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/status_macros.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "sandboxed_api/embed_toc.h"
#include "sandboxed_api/sandbox2/util.h"
#include "sandboxed_api/sandbox2/util/minielf.h"
#include "sandboxed_api/util/fileops.h"

namespace sapi {

namespace {

using ::sapi::file_util::fileops::FDCloser;

#ifndef F_ADD_SEALS
#define F_ADD_SEALS 1033
#define F_SEAL_SEAL 0x0001
#define F_SEAL_SHRINK 0x0002
#define F_SEAL_GROW 0x0004
#define F_SEAL_WRITE 0x0008
#endif

// Applies file sealing flags to a memfd, preventing any subsequent modification
// or truncation by the host or sandboxee.
bool SealFile(int fd) {
  constexpr int kMaxRetries = 10;
  for (int i = 0; i < kMaxRetries; ++i) {
    if (fcntl(fd, F_ADD_SEALS,
              F_SEAL_SEAL | F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE) == 0) {
      return true;
    }
  }
  return false;
}

}  // namespace

namespace internal {

// Fallback copy implementation using a 32 KB chunked buffer when
// copy_file_range(2) is not supported by the kernel, filesystem, or mount.
// Despite the fact that copy_file_range(2) should be available on all supported
// kernels, this fallback ensures a smooth transition to the new feature.
absl::Status FallbackChunkedCopy(int in_fd, uint64_t offset, size_t size,
                                 int out_fd) {
  constexpr size_t kChunkSize = 32 * 1024;
  char buf[kChunkSize];
  uint64_t current_offset = offset;
  size_t bytes_remaining = size;
  while (bytes_remaining > 0) {
    size_t to_read = std::min(bytes_remaining, kChunkSize);
    ssize_t read_bytes =
        TEMP_FAILURE_RETRY(pread(in_fd, buf, to_read, current_offset));
    if (read_bytes < 0) {
      return absl::ErrnoToStatus(errno, "pread failed during fallback copy");
    }
    if (read_bytes == 0) {
      return absl::DataLossError("Unexpected EOF during fallback copy");
    }
    if (!file_util::fileops::WriteToFD(out_fd, buf, read_bytes)) {
      return absl::ErrnoToStatus(errno,
                                 "WriteToFD failed during fallback copy");
    }
    current_offset += read_bytes;
    bytes_remaining -= read_bytes;
  }
  return absl::OkStatus();
}

}  // namespace internal

namespace {

// Attempts to open the container binary or DSO holding `toc`.
// First attempts to discover and open the specific shared object via dladdr(3),
// falling back to opening the main executable (/proc/self/exe).
absl::StatusOr<FDCloser> OpenElfContainer(const EmbedToc& toc) {
  Dl_info dlinfo;
  const void* symbol_addr =
      !toc.section_name.empty() ? toc.section_name.data() : toc.name.data();
  if (dladdr(symbol_addr, &dlinfo) != 0 && dlinfo.dli_fname != nullptr &&
      dlinfo.dli_fname[0] != '\0') {
    int dso_fd = open(dlinfo.dli_fname, O_RDONLY | O_CLOEXEC);
    if (dso_fd >= 0) {
      return FDCloser(dso_fd);
    }
  }

  int exe_fd = open("/proc/self/exe", O_RDONLY | O_CLOEXEC);
  if (exe_fd >= 0) {
    return FDCloser(exe_fd);
  }

  const char* execfn = reinterpret_cast<const char*>(getauxval(AT_EXECFN));
  if (execfn != nullptr && execfn[0] != '\0') {
    int execfn_fd = open(execfn, O_RDONLY | O_CLOEXEC);
    if (execfn_fd >= 0) {
      return FDCloser(execfn_fd);
    }
  }

  return absl::ErrnoToStatus(errno,
                             "Failed to open ELF container (/proc/self/exe)");
}

// Copies an unmapped ELF section directly from the container binary/DSO on disk
// into an executable memfd.
//
// 1. Container Discovery:
//    Uses dladdr(3) on the FileToc address to identify whether the TOC resides
//    in the main binary (/proc/self/exe) or a shared object (.so / DSO).
// 2. Section Parsing:
//    Parses the ELF section headers using sandbox2::ElfFile::GetSectionLocation
//    to obtain the file offset and size of the section without mapping the
//    data.
// 3. In-Kernel Streaming:
//    Transfers data from the container ELF into the memfd using
//    copy_file_range(2) to avoid userspace buffering and memory allocation
//    overhead.
absl::Status CopySectionToMemfd(absl::string_view section_name,
                                absl::string_view toc_name, int memfd,
                                const EmbedToc& toc) {
  ABSL_ASSIGN_OR_RETURN(FDCloser exe_fd, OpenElfContainer(toc));

  ABSL_ASSIGN_OR_RETURN(
      sandbox2::ElfSectionLocation loc,
      sandbox2::ElfFile::GetSectionLocation(exe_fd.get(), section_name));

  loff_t in_offset = loc.offset;
  size_t bytes_remaining = loc.size;
  while (bytes_remaining > 0) {
    ssize_t copied = TEMP_FAILURE_RETRY(copy_file_range(
        exe_fd.get(), &in_offset, memfd, nullptr, bytes_remaining, 0));
    if (copied < 0) {
      if (errno == ENOSYS || errno == EXDEV || errno == EINVAL ||
          errno == EOPNOTSUPP) {
        // Kernel or filesystem does not support copy_file_range; fall back to
        // chunked pread/write.
        return internal::FallbackChunkedCopy(exe_fd.get(), loc.offset, loc.size,
                                             memfd);
      }
      return absl::ErrnoToStatus(
          errno, absl::StrCat("copy_file_range failed for '", toc_name, "'"));
    }
    if (copied == 0) {
      return absl::DataLossError(absl::StrCat(
          "Unexpected EOF during copy_file_range for '", toc_name, "'"));
    }
    bytes_remaining -= copied;
  }
  return absl::OkStatus();
}

}  // namespace

EmbedFile* EmbedFile::instance() {
  static auto* embed_file_instance = new EmbedFile();
  return embed_file_instance;
}

int EmbedFile::CreateFdForFileToc(const EmbedToc& toc) {
  // Create a memfd/temp file and write contents of the SAPI library to it.
  int fd = -1;
  std::string name_str(toc.name);
  if (!sandbox2::util::CreateMemFd(&fd, name_str.c_str())) {
    LOG(ERROR) << "Couldn't create a temporary file for TOC name '" << toc.name
               << "'";
    return -1;
  }
  file_util::fileops::FDCloser embed_fd(fd);
  VLOG(3) << "Created memfd file '" << toc.name << "'";

  if (!toc.section_name.empty()) {
    absl::Status copy_status =
        CopySectionToMemfd(toc.section_name, toc.name, embed_fd.get(), toc);
    if (!copy_status.ok()) {
      LOG(WARNING) << "CopySectionToMemfd failed for '" << toc.name
                   << "': " << copy_status;
      if (toc.data.empty()) {
        return -1;
      }
      if (!file_util::fileops::WriteToFD(embed_fd.get(), toc.data.data(),
                                         toc.data.size())) {
        LOG(ERROR) << "Couldn't write SAPI embed fallback for '" << toc.name
                   << "'";
        return -1;
      }
    }
  } else if (!file_util::fileops::WriteToFD(embed_fd.get(), toc.data.data(),
                                            toc.data.size())) {
    LOG(ERROR) << "Couldn't write SAPI embed file '" << toc.name << "'";
    return -1;
  }
  VLOG(3) << "Populated SAPI embed file '" << toc.name << "'";

  // Make the underlying file non-writable.
  if (fchmod(embed_fd.get(),
             S_IRUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) == -1) {
    PLOG(ERROR) << "Couldn't make FD=" << embed_fd.get() << " RX-only";
    return -1;
  }

  // Seal the file
  if (!SealFile(embed_fd.get())) {
    PLOG(ERROR) << "Couldn't apply file seals to FD=" << embed_fd.get();
    return -1;
  }
  VLOG(3) << "Sealed FD=" << embed_fd.get();

  // Instead of working around problems with CRIU we reopen the file as
  // read-only.
  fd = open(absl::StrCat("/proc/", getpid(), "/fd/", embed_fd.get()).c_str(),
            O_RDONLY | O_CLOEXEC);
  if (fd == -1) {
    PLOG(ERROR) << "Couldn't reopen '" << embed_fd.get()
                << "' read-only through /proc";
    return -1;
  }
  return fd;
}

int EmbedFile::GetFdForFileToc(const EmbedToc& toc) {
  // Access to file_tocs_ must be guarded.
  absl::MutexLock lock(file_tocs_mutex_);

  // If a file-descriptor for this toc already exists, just return it.
  auto entry = file_tocs_.find(toc);
  if (entry != file_tocs_.end()) {
    VLOG(3) << "Returning pre-existing embed file entry for '" << toc.name
            << "', fd: " << entry->second.get();
    return entry->second.get();
  }

  int embed_fd = CreateFdForFileToc(toc);
  if (embed_fd == -1) {
    LOG(ERROR) << "Cannot create a file for FileTOC: '" << toc.name << "'";
    return -1;
  }

  VLOG(1) << "Created new embed file entry for '" << toc.name
          << "' with fd: " << embed_fd;

  file_tocs_[toc] = FDCloser(embed_fd);
  return embed_fd;
}

int EmbedFile::GetDupFdForFileToc(const EmbedToc& toc) {
  int fd = GetFdForFileToc(toc);
  if (fd == -1) {
    return -1;
  }
  fd = dup(fd);
  if (fd == -1) {
    PLOG(ERROR) << "dup failed";
  }
  return fd;
}

}  // namespace sapi
