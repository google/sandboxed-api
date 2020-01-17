// Copyright 2019 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef SANDBOXED_API_SANDBOX2_UTIL_FILE_HELPERS_H_
#define SANDBOXED_API_SANDBOX2_UTIL_FILE_HELPERS_H_

#include <string>

#include "absl/strings/string_view.h"
#include "sandboxed_api/util/status.h"

namespace sandbox2 {
namespace file {

// Empty Options struct for compatibility with Google File.
struct Options {};

// Default constructed Options struct for compatiblity with Google File.
const Options& Defaults();

sapi::Status GetContents(absl::string_view path, std::string* output,
                         const file::Options& options);

sapi::Status SetContents(absl::string_view path, absl::string_view content,
                         const file::Options& options);

}  // namespace file
}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_UTIL_FILE_HELPERS_H_
