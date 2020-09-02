// Copyright 2020 Google LLC
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

#ifndef SANDBOXED_API_TOOLS_CLANG_GENERATOR_EMITTER_H_
#define SANDBOXED_API_TOOLS_CLANG_GENERATOR_EMITTER_H_

#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Type.h"
#include "sandboxed_api/tools/clang_generator/generator.h"
#include "sandboxed_api/tools/clang_generator/types.h"

namespace sapi {

// Constructs an include guard name for the given filename. The name is of the
// same form as the include guards in this project.
// For example,
//   sandboxed_api/examples/zlib/zlib-sapi.sapi.h
// will be mapped to
//   SANDBOXED_API_EXAMPLES_ZLIB_ZLIB_SAPI_SAPI_H_
std::string GetIncludeGuard(absl::string_view filename);

// Outputs a formatted header for a list of functions and their related types.
absl::StatusOr<std::string> EmitHeader(
    std::vector<clang::FunctionDecl*> functions, const QualTypeSet& types,
    const GeneratorOptions& options);

}  // namespace sapi

#endif  // SANDBOXED_API_TOOLS_CLANG_GENERATOR_EMITTER_H_
