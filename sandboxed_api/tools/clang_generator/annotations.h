// Copyright 2026 Google LLC
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

#ifndef SANDBOXED_API_TOOLS_CLANG_GENERATOR_ANNOTATIONS_H_
#define SANDBOXED_API_TOOLS_CLANG_GENERATOR_ANNOTATIONS_H_

#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "clang/AST/DeclBase.h"

namespace sapi {

// Strips the annotations from the input string.
std::string StripAnnotations(const std::string& input);

// Represents a parsed sandbox annotation attribute. For example:
//   [[clang::annotate("sandbox", "elem_sized_by", "size")]]
// is parsed into:
//   name = "elem_sized_by"
//   args = {"size"}
struct SandboxAnnotation {
  std::string name;
  std::vector<std::string> args;
};

// Returns the sandbox annotations for a given decl in their declaration order.
absl::StatusOr<std::vector<SandboxAnnotation>> GetSandboxAnnotations(
    const clang::Decl* decl);

std::vector<std::string> GetAnnotations(clang::Decl* decl);

}  // namespace sapi

#endif  // SANDBOXED_API_TOOLS_CLANG_GENERATOR_ANNOTATIONS_H_
