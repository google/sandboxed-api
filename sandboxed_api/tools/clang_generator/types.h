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

#ifndef SANDBOXED_API_TOOLS_CLANG_GENERATOR_TYPES_H_
#define SANDBOXED_API_TOOLS_CLANG_GENERATOR_TYPES_H_

#include <string>
#include <vector>

#include "clang/AST/ASTContext.h"
#include "clang/AST/Type.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"

namespace sapi {

using QualTypeSet =
    llvm::SetVector<clang::QualType, std::vector<clang::QualType>,
                    llvm::SmallPtrSet<clang::QualType, 8>>;

// Returns whether a type is "simple". Simple types are arithemtic types,
// i.e. signed and unsigned integer, character and bool types, as well as
// "void".
inline bool IsSimple(clang::QualType qual) {
  return qual->isArithmeticType() || qual->isVoidType();
}

inline bool IsPointerOrReference(clang::QualType qual) {
  return qual->isPointerType() || qual->isMemberPointerType() ||
         qual->isLValueReferenceType() || qual->isRValueReferenceType();
}

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
//   SubStruct
//   bool
void GatherRelatedTypes(clang::QualType qual, QualTypeSet* types);

// Maps a qualified type to a fully qualified SAPI-compatible type name. This
// is used for the generated code that invokes the actual function call RPC.
// If no mapping can be found, "int" is assumed.
std::string MapQualType(const clang::ASTContext& context, clang::QualType qual);

// Maps a qualified type used as a function parameter to a type name compatible
// with the generated Sandboxed API.
std::string MapQualTypeParameter(const clang::ASTContext& context,
                                 clang::QualType qual);

// Maps a qualified type used as a function return type to a type name
// compatible with the generated Sandboxed API. Uses MapQualTypeParameter() and
// wraps the type in a absl::StatusOr<> if qual is non-void. Otherwise returns
// absl::Status.
std::string MapQualTypeReturn(const clang::ASTContext& context,
                              clang::QualType qual);

}  // namespace sapi

#endif  // SANDBOXED_API_TOOLS_CLANG_GENERATOR_TYPES_H_
