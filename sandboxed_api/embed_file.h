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

#ifndef SANDBOXED_API_EMBED_FILE_H_
#define SANDBOXED_API_EMBED_FILE_H_

#include <cstddef>
#include <cstdint>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/synchronization/mutex.h"
#include "sandboxed_api/embed_toc.h"
#include "sandboxed_api/util/fileops.h"

namespace sapi {

class EmbedFileTestPeer;

// EmbedFile provides primitives for converting embedded binary payloads into
// sealed executable file descriptors (memfds).
//
// Background & Memory Architecture:
// Rather than storing embedded binary payloads in mapped read-only data
// sections (.rodata / .lrodata with SHF_ALLOC), which increases process virtual
// address space (VSS) and resident set size (RSS) at startup, SAPI embeds
// payloads into unmapped ELF sections (omitting SHF_ALLOC).
//
// When a file descriptor is requested for an embedded binary, EmbedFile locates
// the corresponding ELF section within the container binary/DSO on disk,
// creates an executable memfd, and streams the section bytes directly from disk
// into the memfd using copy_file_range(2) (or chunked pread/write fallback).
// The memfd is sealed (F_ADD_SEALS) and cached for subsequent requests.
class EmbedFile {
 public:
  EmbedFile(const EmbedFile&) = delete;
  EmbedFile& operator=(const EmbedFile&) = delete;

  // Returns the pointer to the per-process EmbedFile singleton.
  static EmbedFile* instance();

  // Returns a cached read-only file descriptor for a given SAPI EmbedToc.
  // The returned FD is owned by the EmbedFile singleton and must NOT be closed
  // by the caller.
  int GetFdForFileToc(const EmbedToc& toc);

  int GetFdForFileToc(const EmbedToc* toc) {
    return toc ? GetFdForFileToc(*toc) : -1;
  }

  // Returns a newly duplicated file descriptor for a given SAPI EmbedToc.
  // The caller owns the returned FD and is responsible for closing it.
  int GetDupFdForFileToc(const EmbedToc& toc);

  int GetDupFdForFileToc(const EmbedToc* toc) {
    return toc ? GetDupFdForFileToc(*toc) : -1;
  }

 private:
  friend class EmbedFileTestPeer;  // For testing.

  // Materializes an executable memfd for an unmapped SAPI EmbedToc section.
  static int CreateFdForFileToc(const EmbedToc& toc);

  EmbedFile() = default;

  // Cache mapping embedded file descriptors to their materialized, sealed
  // memfds.
  absl::flat_hash_map<EmbedToc, file_util::fileops::FDCloser> file_tocs_
      ABSL_GUARDED_BY(file_tocs_mutex_);
  absl::Mutex file_tocs_mutex_;
};

namespace internal {

// Fallback copy implementation using a 32 KB chunked buffer when
// copy_file_range(2) is not supported by the kernel, filesystem, or mount.
// Despite the fact that copy_file_range(2) should be available on all supported
// kernels, this fallback ensures a smooth transition to the new feature.
absl::Status FallbackChunkedCopy(int in_fd, uint64_t offset, size_t size,
                                 int out_fd);

}  // namespace internal

}  // namespace sapi

#endif  // SANDBOXED_API_EMBED_FILE_H_
