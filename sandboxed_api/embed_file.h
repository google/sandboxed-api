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

#include <vector>

#include "sandboxed_api/file_toc.h"
#include "absl/container/flat_hash_map.h"
#include "absl/synchronization/mutex.h"

namespace sapi {

// The class provides primitives for converting FileToc structures into
// executable files.
class EmbedFile {
 public:
  EmbedFile(const EmbedFile&) = delete;
  EmbedFile& operator=(const EmbedFile&) = delete;

  // Returns the pointer to the per-process EmbedFile object.
  static EmbedFile* instance();

  // Returns a file-descriptor for a given FileToc.
  int GetFdForFileToc(const FileToc* toc);

  // Returns a duplicated file-descriptor for a given FileToc.
  int GetDupFdForFileToc(const FileToc* toc);

 private:
  // Creates an executable file for a given FileToc, and return its
  // file-descriptors (-1 in case of errors).
  static int CreateFdForFileToc(const FileToc* toc);

  EmbedFile() = default;

  // List of File TOCs and corresponding file-descriptors.
  absl::flat_hash_map<const FileToc*, int> file_tocs_
      ABSL_GUARDED_BY(file_tocs_mutex_);
  absl::Mutex file_tocs_mutex_;
};

}  // namespace sapi

#endif  // SANDBOXED_API_EMBED_FILE_H_
