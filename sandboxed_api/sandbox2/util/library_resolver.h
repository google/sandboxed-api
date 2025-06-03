// Copyright 2025 Google LLC
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

#ifndef SANDBOXED_API_SANDBOX2_UTIL_LIBRARY_RESOLVER_H_
#define SANDBOXED_API_SANDBOX2_UTIL_LIBRARY_RESOLVER_H_

#include <memory>
#include <string>

#include "absl/functional/function_ref.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "sandboxed_api/sandbox2/util/elf_parser.h"

namespace sandbox2 {

// Reads the list of library dependencies from the given binary,
// and resolves them to actual file system paths.
// The ld_library_path argument can be used to specify additional library
// search paths (similar to the LD_LIBRARY_PATH environment variable).
absl::StatusOr<std::string> ResolveLibraryPaths(
    const std::string& path, absl::string_view ld_library_path,
    absl::FunctionRef<void(absl::string_view)> callback);

// A more flexible version of the above function.
// First, it takes an existing ElfParser for the binary.
// Second, it allows the caller to take ownership of each imported library's
// ElfParser to reuse it for other purposes.
// The mmap_libs argument switches library ElfParser's to mmap mode
// (see ElfParser::Create comment).
absl::StatusOr<std::string> ResolveLibraryPaths(
    ElfParser& file, bool mmap_libs, absl::string_view ld_library_path,
    absl::FunctionRef<void(absl::string_view, std::unique_ptr<ElfParser>&&)>
        callback);

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_UTIL_LIBRARY_RESOLVER_H_
