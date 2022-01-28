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

#ifndef SANDBOXED_API_UTIL_RUNFILES_H_
#define SANDBOXED_API_UTIL_RUNFILES_H_

#include <string>

#include "absl/strings/string_view.h"

namespace sapi {

// Returns the file path pointing to a resource file. The relative_path argument
// should be relative to the runfiles directory.
std::string GetDataDependencyFilePath(absl::string_view relative_path);

namespace internal {

// Like ::sapi::GetDataDependencyFilePath(), but prepends the location of the
// Sandboxed API root runfiles path.
std::string GetSapiDataDependencyFilePath(absl::string_view relative_path);

}  // namespace internal
}  // namespace sapi

#endif  // SANDBOXED_API_UTIL_RUNFILES_H_
