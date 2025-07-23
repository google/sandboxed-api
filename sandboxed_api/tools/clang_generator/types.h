// Copyright 2020 Google LLC
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

#ifndef SANDBOXED_API_TOOLS_CLANG_GENERATOR_TYPES_H_
#define SANDBOXED_API_TOOLS_CLANG_GENERATOR_TYPES_H_

#include <string>
#include <utility>
#include <vector>

#include "absl/strings/string_view.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Type.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Config/llvm-config.h"

namespace sapi {

using QualTypeSet =
    llvm::SetVector<clang::QualType, std::vector<clang::QualType>,
                    llvm::SmallPtrSet<clang::QualType, 8>>;

// Returns whether a type is "simple". Simple types are arithmetic types,
// i.e. signed and unsigned integer, character and bool types, as well as
// "void".
inline bool IsSimple(clang::QualType qual) {
  return qual->isArithmeticType() || qual->isVoidType();
}

// Returns true if type is either a pointer or reference.
inline bool IsPointerOrReference(clang::QualType qual) {
#if LLVM_VERSION_MAJOR > 19
  return qual->isPointerOrReferenceType();
#else
  return qual->isPointerType() || qual->isReferenceType();
#endif
}

// Returns the final pointee type of a pointer or reference type.
// For example, for `int**` it returns `int`.
clang::QualType GetFinalPointeeType(clang::QualType qual);

// RenderedType objects are used to keep track of types that are going to be
// emitted in the generated header.
class RenderedType {
 public:
  RenderedType(absl::string_view ns_name, std::string spelling)
      : ns_name(ns_name), spelling(std::move(spelling)) {}

  bool operator==(const RenderedType& other) const {
    return ns_name == other.ns_name && spelling == other.spelling;
  }

  template <typename H>
  friend H AbslHashValue(H h, const RenderedType& rt) {
    return H::combine(std::move(h), rt.ns_name, rt.spelling);
  }

  std::string ns_name;
  std::string spelling;
};

// Struct to hold the information about a type that is going to be emitted in
// the generated header.
struct NamespacedTypeDecl {
  std::string ns_name;
  clang::TypeDecl* type_decl;
};

class TypeCollector {
 public:
  // Records the source order of the given type in the current translation unit.
  // This is different from collecting related types, as the emitter also needs
  // to know in which order to emit typedefs vs forward decls, etc. and
  // QualTypes only refer to complete definitions.
  void RecordOrderedTypeDeclarations(clang::TypeDecl* type_decl);

  // Computes the transitive closure of all types that a type depends on. Those
  // are types that need to be declared before a declaration of the type denoted
  // by the qual parameter is valid. For example, given
  //   struct SubStruct { bool truth_value; };
  //   struct AggregateStruct {
  //     int int_member;
  //     SubStruct struct_member;
  //   };
  //
  // calling this function on the type "AggregateStruct" yields these types:
  //   int
  //   bool
  //   SubStruct
  void CollectRelatedTypes(clang::QualType qual);

  // Returns a vector of type declarations that were collected, together with
  // their namespace. The vector is ordered according to the order the
  // declarations were seen in the source code.
  std::vector<NamespacedTypeDecl> GetTypeDeclarations();

 private:
  std::vector<clang::TypeDecl*> ordered_decls_;
  QualTypeSet collected_;
  QualTypeSet seen_;
};

class TypeMapper {
 public:
  explicit TypeMapper(const clang::ASTContext& context)
      : TypeMapper(context, "") {}

  TypeMapper(const clang::ASTContext& context, std::string ns_to_strip)
      : context_(context), ns_to_strip_(ns_to_strip) {}

  // Maps a qualified type to a fully qualified SAPI-compatible type name. This
  // is used for the generated code that invokes the actual function call IPC.
  // If no mapping can be found, "int" is assumed.
  std::string MapQualType(clang::QualType qual) const;

  // Maps a qualified type to a fully qualified C++ type name. Transforms C-only
  // constructs such as _Bool to bool.
  std::string MapQualTypeParameterForCxx(clang::QualType qual) const;

  // Maps a qualified type used as a function parameter to a type name
  // compatible with the generated Sandboxed API.
  std::string MapQualTypeParameter(clang::QualType qual) const;

  // Maps a qualified type used as a function return type to a type name
  // compatible with the generated Sandboxed API. Uses MapQualTypeParameter()
  // and wraps the type in an "absl::StatusOr<>" if qual is non-void. Otherwise
  // returns "absl::Status".
  std::string MapQualTypeReturn(clang::QualType qual) const;

 private:
  const clang::ASTContext& context_;  // Current AST context
  // Namespace to strip from the type name. Mainly used for cases where the
  // requested SAPI namespace is the same as the original namespace.
  std::string ns_to_strip_;
};

}  // namespace sapi

#endif  // SANDBOXED_API_TOOLS_CLANG_GENERATOR_TYPES_H_
