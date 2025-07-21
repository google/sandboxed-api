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

#include "sandboxed_api/tools/clang_generator/types.h"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/string_view.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/QualTypeNames.h"
#include "clang/AST/Type.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/Support/Casting.h"

namespace sapi {
namespace {

// Checks if a record declaration is a google::protobuf::Message.
bool IsProtoBuf(const clang::RecordDecl* decl) {
  if (const auto* cxxdecl = llvm::dyn_cast<const clang::CXXRecordDecl>(decl)) {
    // Skip anything that has no body (i.e. forward declarations)
    if (!cxxdecl->hasDefinition()) {
      return false;
    }
    // Lookup the base classes and check if google::protobuf::Messages is one of it.
    for (const clang::CXXBaseSpecifier& base : cxxdecl->bases()) {
      if (base.getType()->getAsCXXRecordDecl()->getQualifiedNameAsString() ==
          "google::protobuf::Message") {
        return true;
      }
    }
  }
  return false;
}

// Returns the fully qualified name of a QualType.
//
// This function handles some special cases, such as function pointers and
// enums. In case of enums it preserves the enum keyword in the name.
std::string GetFullyQualifiedName(const clang::ASTContext& context,
                                  clang::QualType qual,
                                  absl::string_view ns_to_strip = "",
                                  bool suppress_enum_keyword = true) {
  // Remove any "const", "volatile", etc. except for those added via typedefs.
  clang::QualType unqual = qual.getLocalUnqualifiedType();

  // This is to get to the actual name of function pointers.
  if (unqual->isFunctionPointerType() || unqual->isFunctionReferenceType() ||
      unqual->isMemberFunctionPointerType()) {
    unqual = unqual->getPointeeType();
  }

  clang::PrintingPolicy policy = context.getPrintingPolicy();
  if (!suppress_enum_keyword && unqual->isEnumeralType() &&
      unqual->getAsTagDecl() != nullptr) {
    // Keep the "enum" keyword in the type name.
    policy.SuppressTagKeyword = false;
  }

  // Get the fully qualified name without the "struct" or "class" keyword.
  std::string qual_name =
      clang::TypeName::getFullyQualifiedName(unqual, context, policy);

  if (!ns_to_strip.empty()) {
    // Remove the namespace prefix if requested. This is using a textual
    // replacement for ease of implementation. A fully generic solution would
    // require to implement a custom printer for the QualType.
    absl::StrReplaceAll({{absl::StrCat(ns_to_strip, "::"), ""}}, &qual_name);
  }
  return qual_name;
}

// Returns the namespace components of a declaration's qualified name.
std::vector<std::string> GetNamespacePath(const clang::TypeDecl* decl) {
  std::vector<std::string> comps;
  for (const auto* ctx = decl->getDeclContext(); ctx; ctx = ctx->getParent()) {
    if (const auto* nd = llvm::dyn_cast<clang::NamespaceDecl>(ctx)) {
      comps.push_back(nd->getName().str());
    }
  }
  std::reverse(comps.begin(), comps.end());
  return comps;
}

// Removes "const" from a qualified type if it denotes a pointer or reference
// type. Keeps top-level typedef types intact.
clang::QualType MaybeRemoveConst(const clang::ASTContext& context,
                                 clang::QualType qual) {
  if (!qual->isTypedefNameType() && IsPointerOrReference(qual)) {
    clang::QualType pointee_qual = qual->getPointeeType();
    pointee_qual.removeLocalConst();
    qual = context.getPointerType(pointee_qual);
  }
  return qual;
}

}  // namespace

void TypeCollector::RecordOrderedTypeDeclarations(clang::TypeDecl* type_decl) {
  // This implicitly assigns a number (its source order) to each declaration.
  ordered_decls_.push_back(type_decl);
}

void TypeCollector::CollectRelatedTypes(clang::QualType qual) {
  // Skip if we have already processed the QualType.
  if (!seen_.insert(qual)) {
    return;
  }

  // For RecordType (struct, class, union); Recursively call CollectRelatedTypes
  // to collect the types of all fields.
  //
  // - ProtoBuf types are skipped.
  // - Nested types are skipped and the enclosing type is collected which is
  //   enough to reconstruct the AST when emitting the SAPI header.
  if (const auto* record_type = qual->getAs<clang::RecordType>()) {
    const clang::RecordDecl* decl = record_type->getDecl();
    if (!IsProtoBuf(decl)) {
      for (const clang::FieldDecl* field : decl->fields()) {
        CollectRelatedTypes(field->getType());
      }
    }
    const clang::RecordDecl* outer = decl->getOuterLexicalRecordContext();
    decl = outer ? outer : decl;
    collected_.insert(clang::QualType(decl->getTypeForDecl(), /*Quals=*/0));
  }

  // For TypedefType; Collect the underlying type.
  //
  // - For anonymous typedefs, recursively call CollectRelatedTypes.
  if (const auto* typedef_type = qual->getAs<clang::TypedefType>()) {
    clang::TypedefNameDecl* typedef_decl = typedef_type->getDecl();
    // Skip collection of anonymous types (e.g. anonymous enums) as those
    // are handled when emitting them via their parent typedef/using
    // declaration.
    if (!typedef_decl->getAnonDeclWithTypedefName()) {
      CollectRelatedTypes(typedef_decl->getUnderlyingType());
    }
    collected_.insert(qual);
    return;
  }

  // For FunctionPointer; Collect all related types.
  //
  // - For FunctionProtoTypes, recursively call CollectRelatedTypes to collect
  //   the ReturnType and ParamTypes.
  if (qual->isFunctionPointerType() || qual->isFunctionReferenceType() ||
      qual->isMemberFunctionPointerType()) {
    if (const auto* function_type = qual->getPointeeOrArrayElementType()
                                        ->getAs<clang::FunctionProtoType>()) {
      CollectRelatedTypes(function_type->getReturnType());
      for (const clang::QualType& param : function_type->getParamTypes()) {
        CollectRelatedTypes(param);
      }
      // Collect the function pointer itself.
      collected_.insert(qual);
      return;
    }
  }

  // For PointerType or ReferenceTypes, recursively call CollectRelatedTypes to
  // collect the underlying type of the pointer.
  if (IsPointerOrReference(qual)) {
    CollectRelatedTypes(qual->getPointeeType());
    return;
  }

  // For Arraytype, recursively call CollectRelatedTypes to collect the
  // element's type.
  if (const clang::ArrayType* array_type = qual->getAsArrayTypeUnsafe()) {
    CollectRelatedTypes(array_type->getElementType());
    return;
  }

  // For Enumtype, recursively call CollectRelatedTypes to collect the
  // underlying integer type of enum classes as well, as it may be a typedef.
  if (qual->isEnumeralType()) {
    if (const clang::EnumType* enum_type = qual->getAs<clang::EnumType>()) {
      if (const clang::EnumDecl* decl = enum_type->getDecl(); decl->isFixed()) {
        CollectRelatedTypes(decl->getIntegerType());
      }
    }
  }

  // Lastly, collect type if it's an ArithmeticType, or VoidType. We do this
  // last to ensure all other type relationships in the clang AST are resolved.
  if (IsSimple(qual)) {
    collected_.insert(qual);
    return;
  }
}

std::vector<NamespacedTypeDecl> TypeCollector::GetTypeDeclarations() {
  if (ordered_decls_.empty()) {
    return {};
  }

  // The AST context is the same for all declarations in this translation unit,
  // so use a reference here.
  clang::ASTContext& context = ordered_decls_.front()->getASTContext();

  // Set of fully qualified names of the collected types that will be used to
  // only emit type declarations of required types.
  absl::flat_hash_set<std::string> collected_names;
  for (clang::QualType qual : collected_) {
    const std::string qual_name = GetFullyQualifiedName(
        context, qual, /*ns_to_strip=*/"", /*suppress_enum_keyword=*/false);
    collected_names.insert(qual_name);
  }

  std::vector<NamespacedTypeDecl> result;
  for (clang::TypeDecl* type_decl : ordered_decls_) {
    clang::QualType type_decl_type = context.getTypeDeclType(type_decl);

    // Filter out types defined in system headers.
    // TODO: b/402658788 - Instead of this and the hard-coded entities below, we
    //                     should map types and add the correct (system) headers
    //                     to the generated output.
    if (context.getSourceManager().isInSystemHeader(type_decl->getBeginLoc())) {
      continue;
    }

    // Filter out problematic dependent types that we cannot emit properly.
    // CollectRelatedTypes() cannot skip those, as it runs before this
    // information is available.
    if (type_decl_type->isMemberFunctionPointerType() &&
        type_decl_type->isDependentType()) {
      continue;
    }

    // Ideally, collected_.contains() on the underlying QualType of the TypeDecl
    // would work here. However, QualTypes obtained from a TypeDecl contain
    // different Type pointers, even when referring to one of the same types
    // from the set and thus will not be found. Instead, work around the issue
    // by always using the fully qualified name of the type.
    const std::string qual_name =
        GetFullyQualifiedName(context, type_decl_type, /*ns_to_strip=*/"",
                              /*suppress_enum_keyword=*/false);
    if (!collected_names.contains(qual_name)) {
      continue;
    }

    // Skip anonymous declarations that are typedef-ed. For example skip things
    // like "typedef enum { A } SomeName". In this case, the enum is unnamed and
    // the emitter will instead work with the complete typedef, so nothing is
    // lost.
    if (auto* tag_decl = llvm::dyn_cast<clang::TagDecl>(type_decl);
        tag_decl && tag_decl->getTypedefNameForAnonDecl()) {
      continue;
    }

    // Filter out types based on certain namespace conditions.
    const std::vector<std::string> ns_path = GetNamespacePath(type_decl);
    std::string ns_name;
    if (!ns_path.empty()) {
      const auto& ns_root = ns_path.front();
      // Skip if type is declared in the SAPI namespace.
      if (ns_root == "sapi") {
        continue;
      }
      // Skip if type is declared in the C++ standard library
      if (ns_root == "std" || ns_root == "__gnu_cxx") {
        continue;
      }
      // Skip if type is declared in certain Abseil namespaces.
      if (ns_root == "absl") {
        // Skip Abseil internal namespaces
        if (ns_path.size() > 1 && absl::EndsWith(ns_path[1], "_internal")) {
          continue;
        }
        // Skip types from Abseil that will already be included in the generated
        // header.
        if (absl::string_view name(type_decl->getName().data(),
                                   type_decl->getName().size());
            name == "CordMemoryAccounting" || name == "Duration" ||
            name == "LogEntry" || name == "LogSeverity" || name == "Span" ||
            name == "StatusCode" || name == "StatusToStringMode" ||
            name == "SynchLocksHeld" || name == "SynchWaitParams" ||
            name == "Time" || name == "string_view" || name == "tid_t") {
          continue;
        }
      }
      // Skip if type is declared in protobuf namespaces
      if (ns_root == "google" && ns_path.size() > 1 &&
          ns_path[1] == "protobuf") {
        continue;
      }

      ns_name = absl::StrJoin(ns_path, "::");
    }

    result.push_back({ns_name, type_decl});
  }
  return result;
}

std::string TypeMapper::MapQualType(clang::QualType qual) const {
  if (const auto* builtin = qual->getAs<clang::BuiltinType>()) {
    switch (builtin->getKind()) {
      case clang::BuiltinType::Void:
      case clang::BuiltinType::NullPtr:
        return "::sapi::v::Void";

      /*
       * Unsigned types
       */
      case clang::BuiltinType::Bool:
        return "::sapi::v::Bool";
      // Unsigned character types
      case clang::BuiltinType::Char_U:
      case clang::BuiltinType::UChar:
        return "::sapi::v::UChar";
      case clang::BuiltinType::WChar_U:
        return "::sapi::v::ULong";  // 32-bit, correct for Linux and UTF-32
      // Added in C++20
      case clang::BuiltinType::Char8:  // Underlying type: unsigned char
        return "::sapi::v::UChar";
      case clang::BuiltinType::Char16:  // Underlying type: uint_least16_t
        return "::sapi::v::UShort";
      case clang::BuiltinType::Char32:  // Underlying type: uint_least32_t
        return "::sapi::v::ULong";
      // Standard unsigned types
      case clang::BuiltinType::UShort:
        return "::sapi::v::UShort";
      case clang::BuiltinType::UInt:
        return "::sapi::v::UInt";
      case clang::BuiltinType::ULong:
        return "::sapi::v::ULong";
      case clang::BuiltinType::ULongLong:
        return "::sapi::v::ULLong";
      // TODO(cblichmann): Add 128-bit integer support
      // case clang::BuiltinType::UInt128:
      //   return "::sapi::v::UInt128";

      /*
       * Signed types
       */
      // Signed character types
      case clang::BuiltinType::Char_S:
      case clang::BuiltinType::SChar:
        return "::sapi::v::Char";
      case clang::BuiltinType::WChar_S:
        return "::sapi::v::Long";  // 32-bit, correct for Linux and UTF-32

      // Standard signed types
      case clang::BuiltinType::Short:
        return "::sapi::v::Short";
      case clang::BuiltinType::Int:
        return "::sapi::v::Int";
      case clang::BuiltinType::Long:
        return "::sapi::v::Long";
      case clang::BuiltinType::LongLong:
        return "::sapi::v::LLong";
      // TODO(cblichmann): Add 128-bit integer support
      // case clang::BuiltinType::Int128:
      //   return "::sapi::v::Int128";

      /*
       * Floating-point types
       */
      // TODO(cblichmann): Map half/__fp16, _Float16 and __float128 types
      case clang::BuiltinType::Float:
        return "::sapi::v::Reg<float>";
      case clang::BuiltinType::Double:
        return "::sapi::v::Reg<double>";
      case clang::BuiltinType::LongDouble:
        return "::sapi::v::Reg<long double>";

      default:
        break;
    }
  } else if (const auto* enum_type = qual->getAs<clang::EnumType>()) {
    clang::EnumDecl* enum_decl = enum_type->getDecl();
    if (auto* typedef_decl = enum_decl->getTypedefNameForAnonDecl()) {
      qual = typedef_decl->getUnderlyingType().getDesugaredType(context_);
    }
    return absl::StrCat("::sapi::v::IntBase<",
                        GetFullyQualifiedName(context_, qual, ns_to_strip_),
                        ">");
  } else if (IsPointerOrReference(qual)) {
    // Remove "const" qualifier from a pointer or reference type's pointee, as
    // e.g. const pointers do not work well with SAPI.
    return absl::StrCat(
        "::sapi::v::Reg<",
        GetFullyQualifiedName(context_, MaybeRemoveConst(context_, qual),
                              ns_to_strip_),
        ">");
  }

  // Best-effort mapping to "int", leave a comment.
  return absl::StrCat(
      "::sapi::v::Int /* aka '",
      GetFullyQualifiedName(context_, MaybeRemoveConst(context_, qual),
                            ns_to_strip_),
      "' */");
}

std::string TypeMapper::MapQualTypeParameterForCxx(clang::QualType qual) const {
  if (const auto* builtin = qual->getAs<clang::BuiltinType>()) {
    if (builtin->getKind() == clang::BuiltinType::Bool) {
      return "bool";  // _Bool -> bool
    }
    // We may decide to add more mappings later, depending on data model:
    // - long long -> uint64_t
    // - ...
  }
  return GetFullyQualifiedName(context_, qual, ns_to_strip_);
}

std::string TypeMapper::MapQualTypeParameter(clang::QualType qual) const {
  return IsPointerOrReference(qual) ? "::sapi::v::Ptr*"
                                    : MapQualTypeParameterForCxx(qual);
}

std::string TypeMapper::MapQualTypeReturn(clang::QualType qual) const {
  if (qual->isVoidType()) {
    return "::absl::Status";
  }
  // Remove const qualifier like in MapQualType().
  // TODO(cblichmann): We should return pointers differently, as they point to
  //                   the sandboxee's address space.
  return absl::StrCat(
      "::absl::StatusOr<",
      MapQualTypeParameterForCxx(MaybeRemoveConst(context_, qual)), ">");
}

}  // namespace sapi
