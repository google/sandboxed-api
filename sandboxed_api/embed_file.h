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

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/synchronization/mutex.h"
#include "sandboxed_api/embed_toc.h"
#include "sandboxed_api/util/fileops.h"

namespace sapi {

class EmbedFileTestPeer;

// EmbedFile provides primitives for converting embedded binary payloads into
// sealed executable file descriptors (memfds) with runtime caching.
//
// When a file descriptor is requested for an embedded binary, EmbedFile creates
// an anonymous executable memfd, copies the embedded data payload into it,
// seals the memfd (F_ADD_SEALS) to prevent tampering, and caches the descriptor
// keyed by the payload identity for subsequent reuse.
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

  // Materializes an executable memfd for a SAPI EmbedToc entry.
  static int CreateFdForFileToc(const EmbedToc& toc);

  EmbedFile() = default;

  // Cache mapping embedded file descriptors to their materialized, sealed
  // memfds.
  absl::flat_hash_map<EmbedToc, file_util::fileops::FDCloser> file_tocs_
      ABSL_GUARDED_BY(file_tocs_mutex_);
  absl::Mutex file_tocs_mutex_;
};

}  // namespace sapi

#endif  // SANDBOXED_API_EMBED_FILE_H_
