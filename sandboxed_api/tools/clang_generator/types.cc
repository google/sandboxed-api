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

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
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

void TypeCollector::CollectRelatedTypes(clang::QualType qual) {
  if (!seen_.insert(qual)) {
    return;
  }

  if (const auto* typedef_type = qual->getAs<clang::TypedefType>()) {
    auto* typedef_decl = typedef_type->getDecl();
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
      // Note: Do not add the function type itself, as this will always be a
      //       pointer argument. We only need to collect all its related types.
      CollectRelatedTypes(function_type->getReturnType());
      for (const clang::QualType& param : function_type->getParamTypes()) {
        CollectRelatedTypes(param);
      }
      return;
    }
  }

  if (IsPointerOrReference(qual)) {
    clang::QualType pointee = qual;
    do {
      pointee = pointee->getPointeeType();
    } while (IsPointerOrReference(pointee));
    CollectRelatedTypes(pointee);
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

// Removes "const" from a qualified type if it denotes a pointer or reference
// type.
clang::QualType MaybeRemoveConst(const clang::ASTContext& context,
                                 clang::QualType qual) {
  if (IsPointerOrReference(qual)) {
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
                        MaybeRemoveConst(context, qual).getAsString(), ">");
  }
  // Best-effort mapping to "int", leave a comment.
  return absl::StrCat("::sapi::v::Int /* aka '", qual.getAsString(), "' */");
}

std::string MapQualTypeParameterForCxx(const clang::ASTContext& /*context*/,
                                       clang::QualType qual) {
  if (const auto* builtin = qual->getAs<clang::BuiltinType>()) {
    if (builtin->getKind() == clang::BuiltinType::Bool) {
      return "bool";  // _Bool -> bool
    }
    // We may decide to add more mappings later, depending on data model:
    // - long long -> uint64_t
    // - ...
  }
  return qual.getAsString();
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
  return absl::StrCat(
      "::absl::StatusOr<",
      MapQualTypeParameterForCxx(context, MaybeRemoveConst(context, qual)),
      ">");
}

}  // namespace sapi
