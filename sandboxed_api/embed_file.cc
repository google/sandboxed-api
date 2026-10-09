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
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

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

namespace embed_file_internal {

absl::Status FallbackChunkedCopy(int in_fd, uint64_t in_offset, size_t size,
                                 int out_fd, uint64_t out_offset) {
  constexpr size_t kChunkSize = 32 * 1024;
  // Heap-allocated on purpose: this may run deep in the call stack of threads
  // with small stacks (e.g. 64 KiB fibers).
  std::string buf(kChunkSize, '\0');
  while (size > 0) {
    const size_t to_read = std::min(size, kChunkSize);
    const ssize_t read_bytes =
        TEMP_FAILURE_RETRY(pread(in_fd, buf.data(), to_read, in_offset));
    if (read_bytes < 0) {
      return absl::ErrnoToStatus(errno, "pread failed");
    }
    if (read_bytes == 0) {
      return absl::DataLossError("Unexpected EOF during pread");
    }
    for (size_t written = 0; written < static_cast<size_t>(read_bytes);) {
      const ssize_t n = TEMP_FAILURE_RETRY(pwrite(out_fd, buf.data() + written,
                                                  read_bytes - written,
                                                  out_offset + written));
      if (n < 0) {
        return absl::ErrnoToStatus(errno, "pwrite failed");
      }
      if (n == 0) {
        return absl::InternalError("pwrite wrote 0 bytes");
      }
      written += n;
    }
    in_offset += read_bytes;
    out_offset += read_bytes;
    size -= read_bytes;
  }
  return absl::OkStatus();
}

absl::Status CopyFileToFd(int in_fd, uint64_t in_offset, size_t size,
                          int out_fd, SendfileFn sendfile_fn) {
  // Why sendfile(2) and not copy_file_range(2):
  // The destination is a memfd. memfds live on the kernel-internal shmem mount,
  // which is a superblock of its own, distinct from every mounted filesystem
  // (including tmpfs mounts such as /tmp). For filesystems that do not
  // implement ->copy_file_range, which includes shmem, copy_file_range(2)
  // requires both files to be on the same superblock and fails with EXDEV
  // otherwise (generic_copy_file_checks() in fs/read_write.c; this restriction
  // was reinstated in Linux 5.19). The source binary/DSO is never on that
  // internal mount, so copy_file_range(2) always fails with EXDEV here.
  // sendfile(2) has no such restriction: since Linux 2.6.33, `out_fd` can be
  // any file. It copies through the page cache in the kernel, without a
  // userspace buffer.
  //
  // sendfile(2) writes at the file position of `out_fd` (it has no output
  // offset argument), so we set that position explicitly and track the output
  // offset ourselves. The pread/pwrite fallback uses the tracked offsets and
  // does not depend on the file position.
  if (lseek(out_fd, 0, SEEK_SET) < 0) {
    return absl::ErrnoToStatus(errno, "lseek failed");
  }
  // Linux transfers at most 0x7ffff000 bytes per sendfile(2) call.
  constexpr size_t kMaxSendfileBytes = 0x7ffff000;
  off_t current_in_offset = in_offset;
  uint64_t current_out_offset = 0;
  size_t remaining = size;
  while (remaining > 0) {
    const ssize_t copied =
        TEMP_FAILURE_RETRY(sendfile_fn(out_fd, in_fd, &current_in_offset,
                                       std::min(remaining, kMaxSendfileBytes)));
    if (copied < 0) {
      if (errno == EINVAL || errno == ENOSYS || errno == EOPNOTSUPP) {
        // sendfile(2) is not supported for these file descriptors (e.g. no
        // mmap-like read support for `in_fd`) or is blocked (e.g. by seccomp).
        // Copy the remaining bytes in userspace.
        return FallbackChunkedCopy(in_fd, current_in_offset, remaining, out_fd,
                                   current_out_offset);
      }
      return absl::ErrnoToStatus(errno, "sendfile failed");
    }
    if (copied == 0) {
      return absl::DataLossError("Unexpected EOF during sendfile");
    }
    // sendfile(2) advanced `current_in_offset` and the file position of
    // `out_fd` by `copied` bytes.
    current_out_offset += copied;
    remaining -= copied;
  }
  return absl::OkStatus();
}

}  // namespace embed_file_internal

namespace {

// An opened ELF container and the location of an embedded section within it.
struct ElfContainer {
  FDCloser fd;
  sandbox2::ElfSectionLocation section;
};

// Opens the main executable or DSO that contains the section `section_name`.
//
// Candidates are tried in order, and the first one that contains the section
// is used:
// 1. /proc/self/exe, i.e. the main executable. This does not depend on argv[0],
//    which is chosen by whoever executes the binary and may name an unrelated
//    file.
// 2. The object containing `section_name` as reported by dladdr(3). This is
//    the path of the DSO if the section is embedded in a shared library. For
//    the main program, glibc reports argv[0] instead, which covers cases where
//    /proc/self/exe is unusable, e.g. /proc is not mounted or the binary was
//    started as `ld.so ./binary` (in which case /proc/self/exe is the loader).
// Section names are unique per embed target, so if both the main executable
// and a DSO contain the section, both contain the same embedded file.
absl::StatusOr<ElfContainer> OpenElfContainer(absl::string_view section_name) {
  std::vector<std::string> candidates = {"/proc/self/exe"};
  Dl_info dlinfo;
  if (dladdr(section_name.data(), &dlinfo) != 0 &&
      dlinfo.dli_fname != nullptr && dlinfo.dli_fname[0] != '\0') {
    candidates.push_back(dlinfo.dli_fname);
  }

  absl::Status status;
  for (const std::string& path : candidates) {
    int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
      status = absl::ErrnoToStatus(
          errno, absl::StrCat("Failed to open ELF container '", path, "'"));
      continue;
    }
    FDCloser fd_closer(fd);
    absl::StatusOr<sandbox2::ElfSectionLocation> section =
        sandbox2::ElfFile::GetSectionLocation(fd, section_name);
    if (section.ok()) {
      return ElfContainer{std::move(fd_closer), *section};
    }
    status = section.status();
  }
  return status;
}

// Copies an unmapped ELF section directly from the container binary/DSO on disk
// into an executable memfd.
//
// 1. Container Discovery:
//    Uses OpenElfContainer() to find the main binary or shared object (.so /
//    DSO) that contains the section.
// 2. Section Parsing:
//    Parses the ELF section headers using sandbox2::ElfFile::GetSectionLocation
//    to obtain the file offset and size of the section without mapping the
//    data.
// 3. Copy:
//    Copies the section into the memfd with
//    embed_file_internal::CopyFileToFd(), which uses sendfile(2) to copy in the
//    kernel, falling back to pread/pwrite.
absl::Status CopySectionToMemfd(absl::string_view section_name,
                                absl::string_view toc_name, int memfd) {
  ABSL_ASSIGN_OR_RETURN(ElfContainer container, OpenElfContainer(section_name));
  const sandbox2::ElfSectionLocation& loc = container.section;
  absl::Status status = embed_file_internal::CopyFileToFd(
      container.fd.get(), loc.offset, loc.size, memfd);
  if (!status.ok()) {
    return absl::Status(
        status.code(),
        absl::StrCat("Copying section '", section_name, "' for '", toc_name,
                     "' failed: ", status.message()));
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
        CopySectionToMemfd(toc.section_name, toc.name, embed_fd.get());
    if (!copy_status.ok()) {
      LOG(WARNING) << "CopySectionToMemfd failed for '" << toc.name
                   << "': " << copy_status;
      if (toc.data.empty()) {
        return -1;
      }
      // The failed copy may have left partial data in the memfd. Discard it
      // before writing the in-binary data.
      if (ftruncate(embed_fd.get(), 0) == -1 ||
          lseek(embed_fd.get(), 0, SEEK_SET) == -1) {
        PLOG(ERROR) << "Couldn't reset memfd for '" << toc.name << "'";
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
