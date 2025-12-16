// Copyright 2024 Google LLC
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

#ifndef SANDBOXED_API_TOOLS_CLANG_GENERATOR_EMITTER_BASE_H_
#define SANDBOXED_API_TOOLS_CLANG_GENERATOR_EMITTER_BASE_H_

#include <string>
#include <vector>

#include "absl/container/btree_map.h"
#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/node_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Type.h"
#include "sandboxed_api/tools/clang_generator/includes.h"
#include "sandboxed_api/tools/clang_generator/types.h"

namespace sapi {
namespace internal {

// Returns a string of the specified code reformatted to conform to the Google
// style.
// Ill-formed code will return an error status.
absl::StatusOr<std::string> ReformatGoogleStyle(const std::string& filename,
                                                const std::string& code,
                                                int column_limit = -1);

}  // namespace internal

// Text template arguments:
//   1. Namespace name
inline constexpr absl::string_view kNamespaceBeginTemplate =
    R"(
namespace %1$s {

)";

// Text template arguments:
//   1. Namespace name
inline constexpr absl::string_view kNamespaceEndTemplate =
    R"(
}  // namespace %1$s
)";

// Text template arguments:
//   1. Header guard
inline constexpr absl::string_view kHeaderProlog =
    R"(
#ifndef %1$s
#define %1$s

)";

// Text template arguments:
//   1. Header guard
inline constexpr absl::string_view kHeaderEpilog =
    R"(
#endif  // %1$s)";

// Forward declaration used by derived classes to avoid circular dependencies.
struct GeneratorOptions;

class EmitterBase {
 public:
  virtual ~EmitterBase() = default;

  // Adds the declarations of previously collected types to the emitter,
  // recording the spelling of each one. Types/declarations that are not
  // supported by the current generator settings or that are unwanted or
  // unnecessary are skipped. Other filtered types include C++ constructs or
  // well-known standard library elements. The latter can be replaced by
  // including the correct headers in the emitted header.
  void AddTypeDeclarations(const std::vector<NamespacedTypeDecl>& type_decls);

  // Adds the declarations of previously collected functions to the emitter.
  virtual absl::Status AddFunction(clang::FunctionDecl* decl) = 0;

  // Adds the declarations of previously collected variable declarations to the
  // emitter.
  virtual absl::Status AddVar(clang::VarDecl* decl) {
    return absl::OkStatus();
  };

  // Adds an include to the list of includes to be rendered.
  void AddIncludes(const IncludeInfo* include);

  const absl::btree_map<std::string, std::vector<IncludeInfo>>&
  collected_includes() const {
    return collected_includes_;
  }
  absl::btree_map<std::string, std::vector<IncludeInfo>>& collected_includes() {
    return collected_includes_;
  }

 protected:
  // Stores namespaces and a list of spellings for types. Keeps track of types
  // that have been rendered so far. Using a node_hash_set for pointer
  // stability.
  absl::node_hash_set<RenderedType> rendered_types_;

  // A vector to preserve the order of type declarations.
  std::vector<const RenderedType*> rendered_types_ordered_;

  // Fully qualified names of functions for the sandboxed API. Keeps track of
  // functions that have been rendered so far.
  absl::flat_hash_set<std::string> rendered_functions_;

  // A map of collected includes, keyed by the parse context (i.e. the input
  // file).
  absl::btree_map<std::string, std::vector<IncludeInfo>> collected_includes_;

  // A set of the actual include directives to be rendered. It is initialized
  // with standard includes that are commonly used in generated code.
  absl::btree_set<std::string> rendered_includes_ordered_ = {
      "#include <cstdint>",
      "#include <type_traits>",
  };

 private:
  void EmitType(absl::string_view ns_name, clang::TypeDecl* type_decl);
};

// Constructs an include guard for the given filename. The generated string
// conforms to the Google C++ style. For example,
//   sandboxed_api/examples/zlib/zlib-sapi.sapi.h
// will be mapped to
//   SANDBOXED_API_EXAMPLES_ZLIB_ZLIB_SAPI_SAPI_H_
std::string GetIncludeGuard(absl::string_view filename);

// Maps a C system header to its corresponding C++ system header.
// For example, "stdint.h" will be mapped to "cstdint".
absl::string_view MapCSystemHeaderToCpp(absl::string_view header);

}  // namespace sapi

#endif  // SANDBOXED_API_TOOLS_CLANG_GENERATOR_EMITTER_BASE_H_
