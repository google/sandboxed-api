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

#ifndef SANDBOXED_API_TOOLS_CLANG_GENERATOR_ARG_CONVERTER_H_
#define SANDBOXED_API_TOOLS_CLANG_GENERATOR_ARG_CONVERTER_H_

#include <memory>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Type.h"
#include "sandboxed_api/tools/clang_generator/annotations.h"
#include "sandboxed_api/tools/clang_generator/arg.h"

namespace sapi {

// Converts a parameter or function return type into an Arg instance with
// parsed annotations.
absl::StatusOr<ArgPtr> ConvertArg(
    absl::string_view name, clang::QualType type,
    const clang::ParmVarDecl* param, const clang::FunctionDecl* funcDecl,
    const absl::flat_hash_map<std::string, RecordAnnotations>&
        record_annotations);

}  // namespace sapi

#endif  // SANDBOXED_API_TOOLS_CLANG_GENERATOR_ARG_CONVERTER_H_
