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

#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/QualTypeNames.h"
#include "clang/AST/Type.h"

namespace sapi {
namespace {

bool IsFunctionReferenceType(clang::QualType qual) {
#if LLVM_VERSION_MAJOR >= 9
  return qual->isFunctionReferenceType();
#else
  const auto* ref = qual->getAs<clang::ReferenceType>();
  return ref && ref->getPointeeType()->isFunctionType();
#endif
}

}  // namespace

void TypeCollector::RecordOrderedDecl(clang::TypeDecl* type_decl) {
  // This implicitly assigns a number (its source order) to each declaration.
  ordered_decls_.push_back(type_decl);
}

void TypeCollector::CollectRelatedTypes(clang::QualType qual) {
  if (!seen_.insert(qual)) {
    return;
  }

  if (const auto* typedef_type = qual->getAs<clang::TypedefType>()) {
    clang::TypedefNameDecl* typedef_decl = typedef_type->getDecl();
    if (!typedef_decl->getAnonDeclWithTypedefName()) {
      // Do not collect anonymous enums/structs as those are handled when
      // emitting them via their parent typedef/using declaration.
      CollectRelatedTypes(typedef_decl->getUnderlyingType());
    }
    collected_.insert(qual);
    return;
  }

  if (qual->isFunctionPointerType() || IsFunctionReferenceType(qual) ||
      qual->isMemberFunctionPointerType()) {
    if (const auto* function_type = qual->getPointeeOrArrayElementType()
                                        ->getAs<clang::FunctionProtoType>()) {
      // Collect the return type, the parameter types as well as the function
      // pointer type itself.
      CollectRelatedTypes(function_type->getReturnType());
      for (const clang::QualType& param : function_type->getParamTypes()) {
        CollectRelatedTypes(param);
      }
      collected_.insert(qual);
      return;
    }
  }

  if (IsPointerOrReference(qual)) {
    CollectRelatedTypes(qual->getPointeeType());
    return;
  }

  // C array with specified constant size (i.e. int a[42])?
  if (const clang::ArrayType* array_type = qual->getAsArrayTypeUnsafe()) {
    CollectRelatedTypes(array_type->getElementType());
    return;
  }

  if (IsSimple(qual) || qual->isEnumeralType()) {
    if (const clang::EnumType* enum_type = qual->getAs<clang::EnumType>()) {
      // Collect the underlying integer type of enum classes as well, as it may
      // be a typedef.
      if (const clang::EnumDecl* decl = enum_type->getDecl(); decl->isFixed()) {
        CollectRelatedTypes(decl->getIntegerType());
      }
    }
    collected_.insert(qual);
    return;
  }

  if (const auto* record_type = qual->getAs<clang::RecordType>()) {
    const clang::RecordDecl* decl = record_type->getDecl();
    for (const clang::FieldDecl* field : decl->fields()) {
      CollectRelatedTypes(field->getType());
    }
    // Do not collect structs/unions if they are declared within another
    // record. The enclosing type is enough to reconstruct the AST when
    // writing the header.
    const clang::RecordDecl* outer = decl->getOuterLexicalRecordContext();
    decl = outer ? outer : decl;
    collected_.insert(clang::QualType(decl->getTypeForDecl(), 0));
    return;
  }
}

namespace {

std::string GetQualTypeName(const clang::ASTContext& context,
                            clang::QualType qual) {
  // Remove any "const", "volatile", etc. except for those added via typedefs.
  clang::QualType unqual = qual.getLocalUnqualifiedType();

  // This is to get to the actual name of function pointers.
  if (unqual->isFunctionPointerType() || IsFunctionReferenceType(unqual) ||
      unqual->isMemberFunctionPointerType()) {
    unqual = unqual->getPointeeType();
  }
  return clang::TypeName::getFullyQualifiedName(unqual, context,
                                                context.getPrintingPolicy());
}

}  // namespace

std::vector<clang::TypeDecl*> TypeCollector::GetTypeDeclarations() {
  if (ordered_decls_.empty()) {
    return {};
  }

  // The AST context is the same for all declarations in this translation unit,
  // so use a reference here.
  clang::ASTContext& context = ordered_decls_.front()->getASTContext();

  absl::flat_hash_set<std::string> collected_names;
  for (clang::QualType qual : collected_) {
    const std::string qual_name = GetQualTypeName(context, qual);
    collected_names.insert(qual_name);
  }

  std::vector<clang::TypeDecl*> result;
  for (clang::TypeDecl* type_decl : ordered_decls_) {
    clang::QualType type_decl_type = context.getTypeDeclType(type_decl);

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
    const std::string qual_name = GetQualTypeName(context, type_decl_type);
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

    result.push_back(type_decl);
  }
  return result;
}

namespace {

// Removes "const" from a qualified type if it denotes a pointer or reference
// type. Keeps top-level typedef types intact.
clang::QualType MaybeRemoveConst(const clang::ASTContext& context,
                                 clang::QualType qual) {
  if (
#if LLVM_VERSION_MAJOR < 13
      qual->getAs<clang::TypedefType>() == nullptr
#else
      !qual->isTypedefNameType()
#endif
      && IsPointerOrReference(qual)) {
    clang::QualType pointee_qual = qual->getPointeeType();
    pointee_qual.removeLocalConst();
    qual = context.getPointerType(pointee_qual);
  }
  return qual;
}

}  // namespace

std::string MapQualType(const clang::ASTContext& context,
                        clang::QualType qual) {
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
    std::string name;
    if (auto* typedef_name = enum_decl->getTypedefNameForAnonDecl()) {
      name = typedef_name->getQualifiedNameAsString();
    } else {
      name = enum_decl->getQualifiedNameAsString();
    }
    return absl::StrCat("::sapi::v::IntBase<", name, ">");
  } else if (IsPointerOrReference(qual)) {
    // Remove "const" qualifier from a pointer or reference type's pointee, as
    // e.g. const pointers do not work well with SAPI.
    return absl::StrCat("::sapi::v::Reg<",
                        clang::TypeName::getFullyQualifiedName(
                            MaybeRemoveConst(context, qual), context,
                            context.getPrintingPolicy()),
                        ">");
  }
  // Best-effort mapping to "int", leave a comment.
  return absl::StrCat("::sapi::v::Int /* aka '",
                      clang::TypeName::getFullyQualifiedName(
                          MaybeRemoveConst(context, qual), context,
                          context.getPrintingPolicy()),
                      "' */");
}

std::string MapQualTypeParameterForCxx(const clang::ASTContext& context,
                                       clang::QualType qual) {
  if (const auto* builtin = qual->getAs<clang::BuiltinType>()) {
    if (builtin->getKind() == clang::BuiltinType::Bool) {
      return "bool";  // _Bool -> bool
    }
    // We may decide to add more mappings later, depending on data model:
    // - long long -> uint64_t
    // - ...
  }
  return clang::TypeName::getFullyQualifiedName(qual, context,
                                                context.getPrintingPolicy());
}

std::string MapQualTypeParameter(const clang::ASTContext& context,
                                 clang::QualType qual) {
  return IsPointerOrReference(qual) ? "::sapi::v::Ptr*"
                                    : MapQualTypeParameterForCxx(context, qual);
}

std::string MapQualTypeReturn(const clang::ASTContext& context,
                              clang::QualType qual) {
  if (qual->isVoidType()) {
    return "::absl::Status";
  }
  // Remove const qualifier like in MapQualType().
  // TODO(cblichmann): We should return pointers differently, as they point to
  //                   the sandboxee's address space.
  return absl::StrCat(
      "::absl::StatusOr<",
      MapQualTypeParameterForCxx(context, MaybeRemoveConst(context, qual)),
      ">");
}

}  // namespace sapi
