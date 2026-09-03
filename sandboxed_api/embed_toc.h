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

// This file defines the structure of the table-of-contents elements
// produced by sapi_cc_embed_data rules.

#ifndef SANDBOXED_API_EMBED_TOC_H_
#define SANDBOXED_API_EMBED_TOC_H_

#include "absl/strings/string_view.h"

namespace sapi {

// EmbedToc defines the table-of-contents descriptor for embedded binary data in
// Sandboxed API.
//
// Note: This struct is deliberately named EmbedToc rather than FileToc.
// In Google3, the build rule cc_embed_data (powered by //tools/filewrapper)
// generates headers with unqualified `const struct FileToc* <name>_create();`
// declarations wrapped in the user's namespace (e.g. `namespace sapi::foo`).
// If SAPI defined a struct named `FileToc` directly inside `namespace sapi`,
// C++ enclosing-namespace lookup rules would cause any file in a `sapi::*`
// sub-namespace to resolve unqualified `FileToc` to `sapi::FileToc` instead of
// Google3's global `::FileToc` (defined in base/file_toc.h). Because the
// Itanium C++ ABI does not encode return types in mangled symbol names, this
// would lead to silent type confusion / struct slicing and memory corruption at
// runtime.
struct EmbedToc {
  absl::string_view name;
  absl::string_view data;

  static constexpr EmbedToc From(const EmbedToc& toc) { return toc; }

  template <typename H>
  friend H AbslHashValue(H h, const EmbedToc& toc) {
    return H::combine(std::move(h), toc.name.data(), toc.name.size(),
                      toc.data.data(), toc.data.size());
  }

  bool operator==(const EmbedToc& other) const {
    return name.data() == other.name.data() &&
           name.size() == other.name.size() &&
           data.data() == other.data.data() && data.size() == other.data.size();
  }
};

}  // namespace sapi

#endif  // SANDBOXED_API_EMBED_TOC_H_
