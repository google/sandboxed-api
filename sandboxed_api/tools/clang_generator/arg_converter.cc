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

#include "sandboxed_api/tools/clang_generator/arg_converter.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/functional/overload.h"
#include "absl/status/status.h"
#include "absl/status/status_macros.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/strings/substitute.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Mangle.h"
#include "clang/AST/Type.h"
#include "clang/AST/TypeLoc.h"
#include "clang/Basic/LLVM.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"
#include "sandboxed_api/tools/clang_generator/annotations.h"
#include "sandboxed_api/tools/clang_generator/ast_utils.h"
#include "sandboxed_api/tools/clang_generator/ir.h"

namespace sapi {
namespace {

// Describes a non-record type that is not deeply trivially copyable, as a
// phrase that reads after "unsupported pointee type: ".
std::string DescribeUncopyableType(clang::QualType type) {
  if (type->isFunctionPointerType()) {
    // Worth calling out separately: this is the case the reverse-callback work
    // is about, and the workaround is different from a plain pointer's.
    return absl::Substitute(
        "'$0' is a function pointer. Use a callback typedef on the field in "
        "SANDBOX_ANNOTATE_STRUCT, or SANDBOX_OPAQUE_PTR if the sandboxee "
        "alone ever calls it",
        type.getAsString());
  }
  if (type->isPointerType()) {
    return absl::Substitute("'$0' is a pointer", type.getAsString());
  }
  return absl::Substitute("'$0' is not trivially copyable", type.getAsString());
}

// Returns true if the type is trivially copyable and wouldn't involve complex
// lifetimes or aliasing (e.g., struct with pointer fields that are outputs).
//
// When it returns false and `reason` is non-null, `*reason` is set to a phrase
// naming the field that is actually at fault. `*reason` is left untouched when
// the answer is true.
bool IsDeeplyTriviallyCopyableType(const clang::ASTContext& context,
                                   clang::QualType type,
                                   const ConversionContext& conv_ctx,
                                   std::string* reason = nullptr,
                                   bool in_array = false,
                                   bool in_callback = false) {
  if (type.isNull()) {
    if (reason != nullptr) {
      *reason = "the type is not known here";
    }
    return false;
  }
  if (type->isArithmeticType() || type->isEnumeralType()) {
    return true;
  }

  // Allow constant arrays including constant array fields (when
  // IsDeeplyTriviallyCopyableType is recursively called on a field type), as
  // long as the element type is trivially copyable. A trailing array member in
  // a struct like int arr[1] could still be a considered a flexible array
  // member under some compiler extensions. However, that could be too
  // restrictive to disallow. If more needs to be copied then the developer
  // should add a thunk or customize the copying.
  if (auto* const_array_type = context.getAsConstantArrayType(type)) {
    return IsDeeplyTriviallyCopyableType(
        context, const_array_type->getElementType(), conv_ctx, reason,
        /*in_array=*/true, in_callback);
  }

  if (auto* record_decl = type->getAsRecordDecl(); record_decl != nullptr) {
    if (!type.isTriviallyCopyableType(context)) {
      if (reason != nullptr) {
        *reason = absl::Substitute("'$0' is not trivially copyable",
                                   type.getAsString());
      }
      return false;
    }
    if (record_decl->hasFlexibleArrayMember()) {
      if (reason != nullptr) {
        *reason = absl::Substitute("'$0' has a flexible array member",
                                   type.getAsString());
      }
      return false;
    }

    const RecordAnnotations* rec_ann = nullptr;
    auto it = conv_ctx.record_annotations.find(record_decl->getName());
    if (it != conv_ctx.record_annotations.end()) {
      rec_ann = &it->second;
    }

    for (const clang::FieldDecl* field : record_decl->fields()) {
      // If the field is annotated as sandbox_opaque_ptr or names a callback
      // typedef (outside an array or callback parameter), we'll treat it as
      // allowed in the struct copy.
      llvm::StringRef field_name = field->getName();
      bool is_opaque = false;
      bool has_callback_typedef = false;
      if (rec_ann != nullptr) {
        for (const auto& member_ann : rec_ann->member_annotations) {
          if (member_ann.name == field_name) {
            is_opaque = (member_ann.ptr_dir == PointerDir::kSandboxOpaque);
            has_callback_typedef = member_ann.callback_typedef.has_value();
            break;
          }
        }
      }
      if (is_opaque) {
        continue;
      }
      if (has_callback_typedef) {
        if (in_array || in_callback) {
          if (reason != nullptr) {
            *reason = absl::Substitute(
                in_callback ? "field $0 of '$1': callback fields in callback "
                              "struct arguments are not supported"
                            : "field $0 of '$1': callback fields inside an "
                              "array are not supported",
                std::string(field_name), type.getAsString());
          }
          return false;
        }
        continue;
      }

      std::string field_reason;
      if (!IsDeeplyTriviallyCopyableType(context, field->getType(), conv_ctx,
                                         &field_reason, in_array,
                                         in_callback)) {
        if (reason != nullptr) {
          *reason =
              absl::Substitute("field $0 of '$1': $2", std::string(field_name),
                               type.getAsString(), field_reason);
        }
        return false;
      }
    }
    return true;
  }
  if (reason != nullptr) {
    *reason = DescribeUncopyableType(type);
  }
  return false;
}

// Returns true if the type is a `const char*` supported for null-terminated
// global-lifetime strings (`GlobalStringRegistry::GetOrFetch`).
bool IsSupportedNullTerminatedGlobalStringType(clang::QualType type) {
  if (!type->isPointerType()) {
    return false;
  }
  clang::QualType pointee = type->getPointeeType();
  return pointee.isConstQualified() &&
         (pointee->isSpecificBuiltinType(clang::BuiltinType::Char_S) ||
          pointee->isSpecificBuiltinType(clang::BuiltinType::Char_U));
}

// Returns true if `type` is an integer or enumeration type (excluding
// floating-point and `_Complex` types, whose `-0.0` and padding-byte
// representations do not match zero-byte sentinel scanning).
bool IsIntegerOrEnumType(clang::QualType type) {
  return type->isIntegerType() || type->isEnumeralType();
}

// Returns true if the type is a supported NullTerminated input pointer type
// (integer, enum, or `const` struct pointee, e.g., `const char*`, `char*`,
// `const unsigned char*`, `unsigned char*`, `const char16_t*`,
// `const wchar_t*`, `const int*`, `const Enum*`, `const Struct*`).
bool IsSupportedNullTerminatedInputType(clang::QualType type) {
  if (!type->isPointerType()) {
    return false;
  }
  clang::QualType pointee = type->getPointeeType();
  if (pointee->isRecordType()) {
    return pointee.isConstQualified();
  }
  return IsIntegerOrEnumType(pointee);
}

// Returns true if the type is a mutable integer or enum pointer (e.g., `char*`,
// `unsigned char*`, `wchar_t*`, `int*`) supported for in-place
// `SANDBOX_INOUT_PTR SANDBOX_NULL_TERMINATED` mutation.
bool IsSupportedInPlaceNullTerminatedInOutType(clang::QualType type) {
  if (!type->isPointerType()) {
    return false;
  }
  clang::QualType pointee = type->getPointeeType();
  return !pointee.isConstQualified() && IsIntegerOrEnumType(pointee);
}

// Returns true if the type is a supported NullTerminated outparam type.
bool IsSupportedOutParamNullTerminatedType(clang::QualType type) {
  return type->isPointerType() &&
         IsSupportedNullTerminatedGlobalStringType(type->getPointeeType());
}

// Returns true if the type is a supported context-bound outparam type.
bool IsSupportedOutParamContextBoundType(const clang::ASTContext& context,
                                         clang::QualType type,
                                         const ConversionContext& conv_ctx) {
  if (!type->isPointerType() || !type->getPointeeType()->isPointerType())
    return false;
  auto pointee_pointee_type = type->getPointeeType()->getPointeeType();
  return IsDeeplyTriviallyCopyableType(context, pointee_pointee_type, conv_ctx);
}

bool IsSupportedArgByteSizedByType(clang::QualType type) {
  return type->isPointerType() && type->getPointeeType()->isVoidType();
}

bool HasSizedByOutparam(const ArraySizedByType& size_type) {
  return std::visit(absl::Overload{
                        [](const ElemSizedBy& e) {
                          return e.sized_by_outparam_data.has_value();
                        },
                        [](const ByteSizedBy& b) {
                          return b.sized_by_outparam_data.has_value();
                        },
                        [](const auto&) { return false; },
                    },
                    size_type);
}

// Looks up the `PointeeContract` referenced by `ann.pointee_contract` and
// verifies that the contract has been declared and matches the outer pointer's
// pointee type.
absl::StatusOr<const PointeeContract*> ResolvePointeeContract(
    absl::string_view name, clang::QualType type, const Annotations& ann,
    const ConversionContext& conv_ctx) {
  if (!ann.pointee_contract.has_value()) {
    return absl::InternalError(
        absl::StrCat("pointer ", name, " has no pointee_contract"));
  }
  const std::string& contract_name = *ann.pointee_contract;
  auto it = conv_ctx.pointee_contracts.find(contract_name);
  if (it == conv_ctx.pointee_contracts.end()) {
    return absl::InvalidArgumentError(
        absl::Substitute("pointer argument $0 names unknown pointee typedef $1",
                         name, contract_name));
  }
  const PointeeContract& contract = it->second;
  std::string actual_pointee_type = type->getPointeeType()
                                        .getCanonicalType()
                                        .getUnqualifiedType()
                                        .getAsString();
  if (actual_pointee_type != contract.canonical_type) {
    return absl::InvalidArgumentError(absl::Substitute(
        "pointer argument $0 has pointee type '$1', which does not match "
        "pointee typedef $2 of type '$3'",
        name, actual_pointee_type, contract_name, contract.canonical_type));
  }
  return &contract;
}
absl::Status CheckCallbackParamAnnotations(const clang::ASTContext& context,
                                           absl::string_view cb_name,
                                           absl::string_view cb_param_name,
                                           Annotations& annotations,
                                           clang::QualType cb_param_type,
                                           const ConversionContext& conv_ctx) {
  // The callback trampolines support integral, enumeration, float/double,
  // or pointer types as arguments.
  if (cb_param_type->isIntegralOrEnumerationType() ||
      (cb_param_type->isRealFloatingType() &&
       context.getTypeSize(cb_param_type) <= 64)) {
    if (!std::holds_alternative<std::monostate>(annotations.size_type)) {
      return absl::InvalidArgumentError(absl::Substitute(
          "callback $0 parameter $1: non-pointer types $2 don't support size "
          "annotations",
          cb_name, cb_param_name, cb_param_type.getAsString()));
    }
    if (annotations.ptr_dir.has_value()) {
      return absl::InvalidArgumentError(absl::Substitute(
          "callback $0 parameter $1: non-pointer types $2 don't support "
          "pointer direction annotations",
          cb_name, cb_param_name, cb_param_type.getAsString()));
    }
    if (!std::holds_alternative<std::monostate>(annotations.lifetime)) {
      return absl::InvalidArgumentError(absl::Substitute(
          "callback $0 parameter $1: non-pointer types $2 don't support "
          "lifetime annotations",
          cb_name, cb_param_name, cb_param_type.getAsString()));
    }
    return absl::OkStatus();
  }

  if (cb_param_type->isPointerType()) {
    std::optional<PointerDir> ptr_dir;
    if (annotations.ptr_dir) {
      ptr_dir = annotations.ptr_dir;
    }
    if (cb_param_type->getPointeeType().isConstQualified()) {
      // Infer "IN" for const pointers
      if (!ptr_dir) {
        ptr_dir = PointerDir::kIn;
      } else if (*ptr_dir != PointerDir::kIn) {
        return absl::InvalidArgumentError(absl::Substitute(
            "callback $0 parameter $1: output pointers cannot be const",
            cb_name, cb_param_name));
      }
    }
    if (!ptr_dir) {
      return absl::InvalidArgumentError(
          absl::Substitute("callback $0 parameter $1: unknown direction",
                           cb_name, cb_param_name));
    }
    // TODO(b/491762076): support sandbox opaque pointers for callbacks.
    if (*ptr_dir != PointerDir::kIn && *ptr_dir != PointerDir::kOut &&
        *ptr_dir != PointerDir::kInOut && *ptr_dir != PointerDir::kHostOpaque) {
      return absl::InvalidArgumentError(absl::Substitute(
          "callback $0 parameter $1: unsupported pointer direction", cb_name,
          cb_param_name));
    }
    annotations.ptr_dir = *ptr_dir;

    if (*ptr_dir == PointerDir::kHostOpaque) {
      if (!std::holds_alternative<std::monostate>(annotations.size_type)) {
        return absl::InvalidArgumentError(absl::Substitute(
            "callback $0 parameter $1: host opaque pointer should not be sized",
            cb_name, cb_param_name));
      }
      if (!std::holds_alternative<std::monostate>(annotations.lifetime) &&
          !std::holds_alternative<AliasHostPtrLifetime>(annotations.lifetime)) {
        return absl::InvalidArgumentError(absl::Substitute(
            "callback $0 parameter $1: host opaque pointer has unsupported "
            "lifetime annotation",
            cb_name, cb_param_name));
      }
      return absl::OkStatus();
    }

    if (!std::holds_alternative<std::monostate>(annotations.lifetime)) {
      return absl::InvalidArgumentError(absl::Substitute(
          "callback $0 parameter $1: input pointer does not support lifetime "
          "annotations",
          cb_name, cb_param_name));
    }
    if (!annotations.struct_sync.empty() || annotations.shallow_struct_sync) {
      return absl::InvalidArgumentError(absl::Substitute(
          "callback $0 parameter $1: struct_sync is not supported for "
          "callbacks",
          cb_name, cb_param_name));
    }

    if (annotations.pointee_contract.has_value()) {
      if (*ptr_dir != PointerDir::kIn) {
        return absl::InvalidArgumentError(absl::Substitute(
            "callback $0 parameter $1: only input multi-level pointers "
            "(SANDBOX_IN_PTR) are supported for callbacks",
            cb_name, cb_param_name));
      }
      ABSL_ASSIGN_OR_RETURN(const PointeeContract* contract,
                            ResolvePointeeContract(cb_param_name, cb_param_type,
                                                   annotations, conv_ctx));
      if (contract->annotations.ptr_dir != PointerDir::kIn &&
          contract->annotations.ptr_dir != PointerDir::kSandboxOpaque &&
          contract->annotations.ptr_dir != PointerDir::kHostOpaque) {
        return absl::InvalidArgumentError(absl::Substitute(
            "callback $0 parameter $1: pointee typedef $2 must be an input "
            "or opaque pointer for callbacks",
            cb_name, cb_param_name, contract->name));
      }
      bool has_nested_pointee_contract =
          contract->annotations.pointee_contract.has_value();
      if (!has_nested_pointee_contract && cb_param_type->isPointerType()) {
        if (const clang::TypedefNameDecl* inner_td =
                FindPointeeTypedef(cb_param_type->getPointeeType())) {
          has_nested_pointee_contract =
              conv_ctx.pointee_contracts.contains(inner_td->getNameAsString());
        }
      }
      if (has_nested_pointee_contract) {
        return absl::InvalidArgumentError(absl::Substitute(
            "callback $0 parameter $1: multi-level pointers deeper than two "
            "levels are not supported for callbacks",
            cb_name, cb_param_name));
      }
    }

    // Check supported size annotations. For now, this is different from the
    // non-callback case.
    std::string copyability_reason;
    bool is_copyable_pointee =
        annotations.pointee_contract.has_value() ||
        IsDeeplyTriviallyCopyableType(context, cb_param_type->getPointeeType(),
                                      conv_ctx, &copyability_reason,
                                      /*in_array=*/false, /*in_callback=*/true);
    absl::Status error_status = absl::InvalidArgumentError(absl::Substitute(
        "callback $0 pointer argument $1 has unsupported pointee type: $2",
        cb_name, cb_param_name,
        copyability_reason.empty()
            ? cb_param_type->getPointeeType().getAsString()
            : copyability_reason));
    return std::visit(
        absl::Overload{
            [&](const std::monostate&) {
              if (!is_copyable_pointee) return error_status;
              return absl::OkStatus();
            },
            [&](const ElemSizedBy& elem_sized_by) {
              if (!is_copyable_pointee) return error_status;
              return absl::OkStatus();
            },
            [&](const ByteSizedBy& byte_sized_by) {
              if (!is_copyable_pointee &&
                  !IsSupportedArgByteSizedByType(cb_param_type))
                return error_status;
              return absl::OkStatus();
            },
            [&](const NullTerminated&) {
              // TODO(b/491762076): for now we do not support output
              // null-terminated pointers. For non-callback parameters, we have
              // a heursitic to support the `**` outparam case, but not the `*`
              // case. See IsSupportedOutParamNullTerminatedType.
              if (*ptr_dir != PointerDir::kIn) {
                return absl::InvalidArgumentError(absl::Substitute(
                    "callback $0 parameter $1: only input null-terminated "
                    "pointers are supported for callbacks",
                    cb_name, cb_param_name));
              }
              if (!is_copyable_pointee ||
                  (!annotations.pointee_contract.has_value() &&
                   !IsSupportedNullTerminatedInputType(cb_param_type)))
                return error_status;
              return absl::OkStatus();
            },
            [&](const SizedByBinding&) {
              return absl::InvalidArgumentError(absl::Substitute(
                  "callback $0 parameter $1: sized_by_binding is not "
                  "supported for callbacks",
                  cb_name, cb_param_name));
            }},
        annotations.size_type);
  }

  return absl::InvalidArgumentError(absl::Substitute(
      "callback $0 param $1 has unsupported parameter type: $2", cb_name,
      cb_param_name, cb_param_type.getAsString()));
}

// Extracts the names and declarations of a callback's parameters from the
// type source info of `param`, which must be a function pointer or functor.
absl::Status ExtractCallbackParams(
    const clang::ParmVarDecl& param, std::vector<std::string>& param_names,
    std::vector<const clang::ParmVarDecl*>& param_decls) {
  // Details: the Clang Type does not include the parameter names (more of
  // a canonical type). However, the TypeSourceInfo and TypeLoc does let us
  // retrieve that information.
  auto* type_source_info = param.getTypeSourceInfo();
  if (!type_source_info) {
    return absl::InvalidArgumentError(absl::Substitute(
        "callback $0 does not have type source info", param.getName().str()));
  }
  std::optional<clang::FunctionProtoTypeLoc> ftl =
      ast::GetFunctionProtoTypeLoc(type_source_info);
  if (!ftl.has_value()) {
    return absl::InvalidArgumentError(
        absl::Substitute("callback $0 does not have a function proto type loc",
                         param.getName().str()));
  }
  param_names.reserve(ftl->getNumParams());
  param_decls.reserve(ftl->getNumParams());
  for (unsigned i = 0; i < ftl->getNumParams(); ++i) {
    clang::ParmVarDecl* cb_param = ftl->getParam(i);
    if (!cb_param) {
      return absl::InvalidArgumentError(absl::Substitute(
          "callback $0 does not have param $1", param.getName().str(), i));
    }
    // Check for the optional param names in a function pointer / function type.
    // If not present, falls back to generic names (cb_arg0, cb_arg1, ...).
    std::string param_name;
    if (!cb_param->getName().empty()) {
      param_name = cb_param->getNameAsString();
    } else {
      param_name = absl::StrFormat("cb_arg%u", i);
    }
    param_names.push_back(param_name);
    param_decls.push_back(cb_param);
  }
  return absl::OkStatus();
}

// Recovers the C++ string form from a canonical type name normalized by
// QualTypeToTypeInfo, which spells these as exactly "std::string",
// "std::string&", "const std::string&" or "std::string_view". Pointers to
// strings are classified as TypeKind::kPointer and handled by the caller.
ir::CppStringParam::Form CppStringForm(absl::string_view canonical_name) {
  if (canonical_name == "std::string_view") {
    return ir::CppStringParam::Form::kView;
  }
  if (canonical_name == "std::string&" ||
      canonical_name == "const std::string&") {
    return ir::CppStringParam::Form::kReference;
  }
  return ir::CppStringParam::Form::kValue;
}

// Translates a Clang QualType to a declarative IR TypeInfo.
// Note: Handles single-level pointers/references, scalars, records/structs,
// strings, and callbacks. Multi-level indirections (e.g., T**) are categorized
// as TypeKind::kPointer with a pointer pointee type, which requires custom
// thunks or serialization rather than flat buffer marshalling.
absl::StatusOr<ir::TypeInfo> QualTypeToTypeInfo(clang::QualType type) {
  ir::TypeInfo info;
  type = type.getCanonicalType();
  info.canonical_name = type.getAsString();
  info.is_const = type.isConstQualified() ||
                  (type->isReferenceType() &&
                   type.getNonReferenceType().isConstQualified());
  // Clang spells cv-qualifiers and the tag keyword into canonical type names,
  // so `const std::string` prints as "const class std::basic_string<char>" and
  // `const std::string*` has such a pointee. Matching the fully spelled name
  // would file those under TypeKind::kStruct, so the match runs on the
  // unqualified, non-reference type instead; the const-ness is already
  // recorded in `is_const` above and the reference-ness is recovered from the
  // type itself.
  const std::string unqualified_name =
      type.getNonReferenceType().getUnqualifiedType().getAsString();
  const bool is_std_string =
      unqualified_name == "std::string" ||
      unqualified_name == "class std::basic_string<char>";
  const bool is_std_string_view =
      unqualified_name == "std::string_view" ||
      unqualified_name == "class std::basic_string_view<char>";
  if (type->isVoidType()) {
    info.kind = ir::TypeKind::kVoid;
  } else if (is_std_string) {
    info.kind = ir::TypeKind::kString;
    info.canonical_name = !type->isReferenceType() ? "std::string"
                          : info.is_const          ? "const std::string&"
                                                   : "std::string&";
  } else if (is_std_string_view && !type->isReferenceType()) {
    // Note: a `std::string_view` reference has no marshalling of its own and
    // deliberately falls through to the unsupported reference type error.
    info.kind = ir::TypeKind::kString;
    info.canonical_name = "std::string_view";
  } else {
    std::string template_name;
    if (type->isReferenceType()) {
      clang::QualType pointee = type->getPointeeType();
      if (ast::GetFunctorUnderlyingFunctionType(pointee, template_name) !=
          nullptr) {
        info.kind = ir::TypeKind::kCallback;
        return info;
      }
      return absl::InvalidArgumentError(absl::Substitute(
          "unsupported reference type: $0", info.canonical_name));
    }

    if (type->isArithmeticType() || type->isEnumeralType()) {
      info.kind = ir::TypeKind::kScalar;
    } else if (type->isFunctionPointerType() ||
               ast::GetFunctorUnderlyingFunctionType(type, template_name) !=
                   nullptr) {
      info.kind = ir::TypeKind::kCallback;
    } else if (type->isPointerType() || type->isArrayType()) {
      info.kind = ir::TypeKind::kPointer;
      clang::QualType pointee =
          type->isPointerType()
              ? type->getPointeeType()
              : type->castAsArrayTypeUnsafe()->getElementType();
      ABSL_ASSIGN_OR_RETURN(ir::TypeInfo pointee_info,
                            QualTypeToTypeInfo(pointee));
      info.pointee = std::make_shared<ir::TypeInfo>(std::move(pointee_info));
    } else if (type->isRecordType()) {
      info.kind = ir::TypeKind::kStruct;
    } else {
      return absl::InvalidArgumentError(absl::Substitute(
          "Unsupported Clang type for IR: $0", info.canonical_name));
    }
  }
  return info;
}

ir::BufferBounds AnnotationsToBufferBounds(const Annotations& ann) {
  // Strips the optional leading '*' an outparam size is written with, so that
  // "*written_len" and "written_len" both name the sibling parameter.
  auto outparam_name = [](absl::string_view expr) {
    return std::string(absl::StripAsciiWhitespace(
        absl::StripPrefix(absl::StripAsciiWhitespace(expr), "*")));
  };

  ir::BufferBounds bounds = ir::bounds::Singleton{};
  std::visit(
      absl::Overload{
          [&](const std::monostate&) {},
          [&](const ElemSizedBy& elem) {
            if (elem.sized_by_outparam_data) {
              bounds = ir::bounds::ElemSizedByOutparam{
                  .outparam_name = outparam_name(elem.expr),
                  .capacity_expr = elem.sized_by_outparam_data->capacity_expr,
              };
            } else {
              bounds = ir::bounds::ElemCount{.size_expr = elem.expr};
            }
          },
          [&](const ByteSizedBy& byte) {
            if (byte.sized_by_outparam_data) {
              bounds = ir::bounds::ByteSizedByOutparam{
                  .outparam_name = outparam_name(byte.expr),
                  .capacity_expr = byte.sized_by_outparam_data->capacity_expr,
              };
            } else {
              bounds = ir::bounds::ByteCount{.size_expr = byte.expr};
            }
          },
          [&](const SizedByBinding& binding) {
            bounds = ir::bounds::SizedByBinding{
                .context_expr = binding.context,
                .binding_name = binding.binding_expr,
                .is_elem_sized = binding.is_elem_sized,
            };
          },
          [&](const NullTerminated&) { bounds = ir::bounds::NullTerminated{}; },
      },
      ann.size_type);
  return bounds;
}

ir::LifetimePolicy AnnotationsToLifetimePolicy(const Annotations& ann) {
  return std::visit(
      absl::Overload{
          [](const std::monostate&) -> ir::LifetimePolicy {
            return ir::lifetime::ScopedCall{};
          },
          [](const SandboxGlobalLifetime&) -> ir::LifetimePolicy {
            return ir::lifetime::SandboxGlobal{};
          },
          [](const MallocLifetime&) -> ir::LifetimePolicy {
            return ir::lifetime::Malloc{};
          },
          [](const AliasHostPtrLifetime& host) -> ir::LifetimePolicy {
            return ir::lifetime::AliasHostPtr{
                .host_param_name = host.param_name,
            };
          },
          [](const AliasCallbackReturnLifetime& cb) -> ir::LifetimePolicy {
            return ir::lifetime::AliasCallbackReturn{
                .callback_param_name = cb.callback_param_name,
            };
          },
          // The sandbox borrows the callback-returned buffer for the duration
          // of the outer call, which maps to `ScopedCall` in the IR.
          [](const OuterCallLifetime&) -> ir::LifetimePolicy {
            return ir::lifetime::ScopedCall{};
          },
      },
      ann.lifetime);
}

// Validates annotations against parameter type and semantics.
absl::Status ValidateParamAnnotations(absl::string_view name,
                                      clang::QualType type, Annotations& ann,
                                      bool is_return_value,
                                      bool is_callback_param,
                                      bool is_pointee_contract = false) {
  // Validate that raw buffer pointers have an explicit direction annotation.
  // Function pointers, pointer-to-pointers and callback parameters (which are
  // defaulted just below) are exempted.
  //
  // Return values are exempted because they do not take an explicit direction
  // annotation. For function return values, `ParseAnnotations` sets `kOut` when
  // a return-lifetime annotation (`alias_ptr`, `alias_callback_return`, or
  // `copy_from_and_bind_out_ptr`) is present. For callback return values,
  // `SANDBOX_LIFETIME_OUTER_CALL` is specified on the callback's `typedef`
  // declaration and passed to `ConvertParameterToIR`
  // via `CallbackReturnSource`, so `ann` is empty here. In both cases, returned
  // buffers flow out of the callee.
  //
  // Functors need no exemption: they are record types, so they never satisfy
  // the isPointerType() check below.
  if (type->isPointerType() && !is_return_value && !is_callback_param) {
    bool is_buffer_pointer = !type->getPointeeType()->isPointerType() &&
                             !type->isFunctionPointerType();
    if ((is_buffer_pointer || ann.pointee_contract.has_value()) &&
        !ann.ptr_dir.has_value()) {
      return absl::InvalidArgumentError(
          absl::Substitute("argument $0 with type $1: missing sandbox "
                           "annotation",
                           name, type.getAsString()));
    }
  }

  // TODO(b/561450349): Harmonize pointer direction defaulting for callback
  // parameters with regular function parameters (which require explicit
  // annotations).
  if (is_callback_param && type->isPointerType() &&
      !type->isFunctionPointerType() && !ann.ptr_dir.has_value()) {
    if (type->getPointeeType().isConstQualified()) {
      ann.ptr_dir = PointerDir::kIn;
    } else {
      return absl::InvalidArgumentError(
          absl::Substitute("callback parameter $0: unknown direction", name));
    }
  }

  if (ann.pointee_contract.has_value()) {
    if (ann.ptr_dir == PointerDir::kSandboxOpaque ||
        ann.ptr_dir == PointerDir::kHostOpaque) {
      return absl::InvalidArgumentError(absl::Substitute(
          "pointer argument $0 is opaque and cannot have a pointee contract",
          name));
    }
    if (HasSizedByOutparam(ann.size_type) ||
        std::holds_alternative<SizedByBinding>(ann.size_type)) {
      return absl::InvalidArgumentError(absl::Substitute(
          "pointer argument $0: sized_by_outparam and sized_by_binding cannot "
          "be combined with a pointee contract on the outer pointer",
          name));
    }
    if (std::holds_alternative<NullTerminated>(ann.size_type) &&
        ann.ptr_dir != PointerDir::kIn) {
      return absl::InvalidArgumentError(absl::Substitute(
          "pointer argument $0: null_terminated on a pointer with a pointee "
          "contract is only supported with SANDBOX_IN_PTR; to describe a "
          "null-terminated pointee, place SANDBOX_NULL_TERMINATED on the "
          "pointer typedef declaration instead",
          name));
    }
    if (ann.context_bound.copy_from_and_bind.has_value() ||
        ann.context_bound.retain_and_bind.has_value()) {
      return absl::InvalidArgumentError(absl::Substitute(
          "pointer argument $0: context-binding annotations cannot be "
          "combined with a pointee contract on the outer pointer; place them "
          "on the pointer typedef declaration instead",
          name));
    }
    if (!ann.context_bound.bind_data.empty() ||
        ann.context_bound.clear_bindings) {
      return absl::InvalidArgumentError(absl::Substitute(
          "pointer argument $0: bind_data and clear_bindings cannot be "
          "combined with a pointee contract on the outer pointer",
          name));
    }
    if (std::holds_alternative<MallocLifetime>(ann.lifetime)) {
      return absl::InvalidArgumentError(absl::Substitute(
          "pointer argument $0: lifetime_malloc cannot be combined with a "
          "pointee contract on the outer pointer; place it on the pointer "
          "typedef declaration instead",
          name));
    }
    if (is_return_value) {
      return absl::InvalidArgumentError(absl::Substitute(
          "return pointer $0 cannot have a pointee contract", name));
    }
    if (!std::holds_alternative<std::monostate>(ann.lifetime)) {
      return absl::InvalidArgumentError(absl::Substitute(
          "pointer argument $0: lifetime annotations cannot be combined with a "
          "pointee contract on the outer pointer; place them on the pointer "
          "typedef declaration instead",
          name));
    }
  }

  if (is_pointee_contract) {
    if (HasSizedByOutparam(ann.size_type) ||
        std::holds_alternative<SizedByBinding>(ann.size_type)) {
      return absl::InvalidArgumentError(absl::Substitute(
          "pointee typedef $0: sized_by_outparam and sized_by_binding are not "
          "supported on a pointee typedef",
          name));
    }
    if (std::holds_alternative<AliasHostPtrLifetime>(ann.lifetime) ||
        std::holds_alternative<AliasCallbackReturnLifetime>(ann.lifetime)) {
      return absl::InvalidArgumentError(absl::Substitute(
          "pointee typedef $0: alias lifetime annotations are not supported "
          "on a pointee typedef",
          name));
    }
    if (ann.context_bound.retain_and_bind.has_value() ||
        !ann.context_bound.bind_data.empty() ||
        ann.context_bound.clear_bindings) {
      return absl::InvalidArgumentError(absl::Substitute(
          "pointee typedef $0: only copy_from_and_bind_out_ptr context "
          "binding is supported on a pointee typedef",
          name));
    }
  }

  if (ann.ptr_dir == PointerDir::kSandboxOpaque ||
      ann.ptr_dir == PointerDir::kHostOpaque) {
    if (!std::holds_alternative<std::monostate>(ann.size_type)) {
      return absl::InvalidArgumentError(absl::Substitute(
          "pointer argument $0 is opaque and should not be sized (kind $1)",
          name, ann.size_type.index()));
    }
    // An opaque handle has no lifetime of its own, with one exception: a
    // callback parameter carrying alias_ptr names the outer parameter whose
    // handle it reuses (see LinkAliasParamToCallbackParam). On a regular
    // parameter there is no such outer scope, so the annotation is a mistake.
    const bool is_aliasing_callback_param =
        is_callback_param &&
        std::holds_alternative<AliasHostPtrLifetime>(ann.lifetime);
    if (!std::holds_alternative<std::monostate>(ann.lifetime) &&
        !is_aliasing_callback_param) {
      return absl::InvalidArgumentError(absl::Substitute(
          "pointer argument $0 is opaque and should not have a lifetime "
          "annotation",
          name));
    }
  }

  if (std::holds_alternative<MallocLifetime>(ann.lifetime)) {
    if (ann.ptr_dir != PointerDir::kOut) {
      return absl::InvalidArgumentError(absl::Substitute(
          "pointer argument $0: lifetime_malloc is only supported on out_ptr "
          "pointers",
          name));
    }
    if (ann.context_bound.copy_from_and_bind.has_value() ||
        ann.context_bound.retain_and_bind.has_value()) {
      return absl::InvalidArgumentError(absl::Substitute(
          "pointer argument $0: lifetime_malloc cannot be combined with "
          "context-binding annotations",
          name));
    }
    if (!is_return_value && !is_pointee_contract &&
        (!type->isPointerType() || !type->getPointeeType()->isPointerType() ||
         type->getPointeeType()->isFunctionPointerType())) {
      return absl::InvalidArgumentError(absl::Substitute(
          "pointer argument $0: lifetime_malloc on a parameter requires a "
          "pointer-to-pointer (T**) out-parameter or a pointee typedef",
          name));
    }
    if (const auto* elem = std::get_if<ElemSizedBy>(&ann.size_type);
        elem != nullptr && elem->sized_by_outparam_data.has_value()) {
      return absl::InvalidArgumentError(absl::Substitute(
          "pointer argument $0: lifetime_malloc allocates the host buffer "
          "after the call, so use elem_sized_by instead of "
          "elem_sized_by_outparam",
          name));
    }
    if (const auto* byte = std::get_if<ByteSizedBy>(&ann.size_type);
        byte != nullptr && byte->sized_by_outparam_data.has_value()) {
      return absl::InvalidArgumentError(absl::Substitute(
          "pointer argument $0: lifetime_malloc allocates the host buffer "
          "after the call, so use byte_sized_by instead of "
          "byte_sized_by_outparam",
          name));
    }
  }

  if (std::holds_alternative<NullTerminated>(ann.size_type)) {
    // Retaining a buffer records its extent so that a later call can size
    // itself from the binding. A null-terminated string has no extent the
    // caller can compute up front, so retaining one would allocate and copy a
    // single element and record that as the bound size. `copy_from_and_bind`
    // is unaffected: it reads the string back out of the sandbox and sizes the
    // host copy from the result.
    if (ann.context_bound.retain_and_bind.has_value()) {
      return absl::InvalidArgumentError(absl::Substitute(
          "pointer argument $0 cannot combine retain_and_bind with "
          "null_terminated because the retained buffer has no known size. "
          "Annotate it with an explicit element or byte size instead.",
          name));
    }
    if (ann.context_bound.copy_from_and_bind.has_value()) {
      clang::QualType bound_ptr =
          (is_return_value || is_pointee_contract) ? type
          : type->isPointerType()                  ? type->getPointeeType()
                                                   : clang::QualType();
      if (bound_ptr.isNull() || !bound_ptr->isPointerType() ||
          !IsIntegerOrEnumType(bound_ptr->getPointeeType())) {
        return absl::InvalidArgumentError(absl::Substitute(
            "$0 $1 with copy_from_and_bind_out_ptr and null_terminated must "
            "point to an integer or enum type",
            is_return_value ? "return pointer" : "pointer argument", name));
      }
    } else {
      if (is_return_value) {
        if (std::holds_alternative<MallocLifetime>(ann.lifetime)) {
          if (!type->isPointerType() ||
              !IsIntegerOrEnumType(type->getPointeeType())) {
            return absl::InvalidArgumentError(absl::Substitute(
                "return pointer $0 with lifetime_malloc and null_terminated "
                "must point to an integer or enum type",
                name));
          }
        } else {
          if (!IsSupportedNullTerminatedGlobalStringType(type)) {
            return absl::InvalidArgumentError(absl::Substitute(
                "return pointer $0 is null-terminated but not a const char*",
                name));
          }
          if (!std::holds_alternative<SandboxGlobalLifetime>(ann.lifetime)) {
            return absl::InvalidArgumentError(absl::Substitute(
                "function $0: null_terminated annotation for "
                "return values requires a lifetime annotation.",
                name));
          }
        }
      } else if (ann.ptr_dir == PointerDir::kIn) {
        if (!ann.pointee_contract.has_value() &&
            !IsSupportedNullTerminatedInputType(type)) {
          return absl::InvalidArgumentError(absl::Substitute(
              "pointer argument $0 is null-terminated but not an integer, "
              "enum, or const struct pointer",
              name));
        }
      } else if (ann.ptr_dir != PointerDir::kIn) {
        if (std::holds_alternative<SandboxGlobalLifetime>(ann.lifetime)) {
          if (!IsSupportedOutParamNullTerminatedType(type) &&
              !(is_pointee_contract &&
                IsSupportedNullTerminatedGlobalStringType(type))) {
            return absl::InvalidArgumentError(absl::Substitute(
                "pointer argument $0 with lifetime_sandbox_global must "
                "be a const char**",
                name));
          }
        } else if (std::holds_alternative<MallocLifetime>(ann.lifetime)) {
          clang::QualType alloc_ptr =
              is_pointee_contract ? type : type->getPointeeType();
          if (!alloc_ptr->isPointerType() ||
              !IsIntegerOrEnumType(alloc_ptr->getPointeeType())) {
            return absl::InvalidArgumentError(absl::Substitute(
                "pointer argument $0 with lifetime_malloc and null_terminated "
                "must point to an integer or enum type",
                name));
          }
        } else if (!(ann.ptr_dir == PointerDir::kInOut && !is_callback_param &&
                     !is_pointee_contract &&
                     !ann.pointee_contract.has_value() &&
                     std::holds_alternative<std::monostate>(ann.lifetime) &&
                     IsSupportedInPlaceNullTerminatedInOutType(type))) {
          return absl::InvalidArgumentError(absl::Substitute(
              "pointer argument $0: null_terminated annotation for "
              "output requires a lifetime annotation.",
              name));
        }
      }
    }
  }

  return absl::OkStatus();
}

// Provenance of a parameter being converted to IR. Each alternative carries
// the AST declaration (or enclosing callback annotations) that applies to that
// position.
struct FunctionParamSource {
  const clang::ParmVarDecl* decl;
};
struct CallbackParamSource {
  // Use-site parameter declaration, used to inspect AST types (such as nested
  // callback parameters).
  const clang::ParmVarDecl* decl;
  // Resolved annotations for this callback parameter, taken either from `decl`
  // or from the callback's typedef.
  const Annotations& annotations;
};
struct FunctionReturnSource {
  const clang::FunctionDecl* decl;
};
struct CallbackReturnSource {
  // Callback return values have no AST `Decl` of their own; buffer bounds and
  // lifetime annotations written on the enclosing callback parameter or its
  // typedef apply to the returned buffer.
  const Annotations& callback_annotations;
};
struct PointeeContractSource {
  const clang::ASTContext* ast_ctx;
  const Annotations& annotations;
};

using ParamSource =
    std::variant<FunctionParamSource, CallbackParamSource, FunctionReturnSource,
                 CallbackReturnSource, PointeeContractSource>;

const clang::ASTContext* GetASTContext(const ParamSource& source) {
  return std::visit(
      absl::Overload{
          [](const FunctionParamSource& s) -> const clang::ASTContext* {
            return &s.decl->getASTContext();
          },
          [](const CallbackParamSource& s) -> const clang::ASTContext* {
            return &s.decl->getASTContext();
          },
          [](const FunctionReturnSource& s) -> const clang::ASTContext* {
            return &s.decl->getASTContext();
          },
          [](const CallbackReturnSource&) -> const clang::ASTContext* {
            return nullptr;
          },
          [](const PointeeContractSource& s) -> const clang::ASTContext* {
            return s.ast_ctx;
          },
      },
      source);
}

// Maximum number of parameters a callback trampoline saves (up to 6 integer
// registers on x86-64 / 8 on AArch64, 8 floating-point registers, and 4 caller
// stack slots).
constexpr size_t kMaxCallbackParams = 10;

// Returns true if both supported ABIs pass a value of `type` in a single
// 64-bit general-purpose or floating-point argument register (or stack slot).
bool FitsInOneArgumentRegister(const clang::ASTContext& ast_ctx,
                               clang::QualType type) {
  if (type->isSpecificBuiltinType(clang::BuiltinType::Float) ||
      type->isSpecificBuiltinType(clang::BuiltinType::Double)) {
    return true;
  }
  if (!type->isIntegralOrEnumerationType() && !type->isPointerType()) {
    return false;
  }
  // Excludes 128-bit integer (`__int128`) types.
  return ast_ctx.getTypeSize(type) <= 64;
}

// Verifies that `proto` can be invoked through the fixed callback trampolines
// in `trampolines.S`.
//
// Each trampoline saves the general-purpose integer and 64-bit floating-point
// argument registers (plus stack overflow slots up to `kMaxCallbackParams`) and
// returns a single 64-bit integer or floating-point result register. Extended-
// precision floating-point (`long double`), 128-bit integer (`__int128`), and
// by-value aggregate types are not supported and must be rejected at code
// generation time.
absl::Status CheckCallbackSignatureIsCallable(
    absl::string_view name, const clang::ASTContext& ast_ctx,
    const clang::FunctionProtoType& proto) {
  if (proto.isVariadic()) {
    return absl::InvalidArgumentError(absl::Substitute(
        "callback $0 is variadic. The sandboxee reaches a callback through a "
        "trampoline that saves a fixed set of argument registers, so it "
        "cannot pass a variable number of arguments",
        name));
  }
  if (proto.getNumParams() > kMaxCallbackParams) {
    return absl::InvalidArgumentError(absl::Substitute(
        "callback $0 takes $1 parameters. The trampoline the sandboxee "
        "reaches it through saves $2 argument registers, so any further "
        "parameter would never arrive",
        name, proto.getNumParams(), kMaxCallbackParams));
  }
  for (unsigned i = 0; i < proto.getNumParams(); ++i) {
    clang::QualType param_type = proto.getParamType(i);
    if (!FitsInOneArgumentRegister(ast_ctx, param_type)) {
      return absl::InvalidArgumentError(absl::Substitute(
          "callback $0 parameter $1 has type '$2'. The trampoline the "
          "sandboxee reaches it through passes each argument in one 64-bit "
          "integer or floating-point slot, so an extended-precision "
          "floating-point, by-value struct or 128-bit parameter never arrives",
          name, i + 1, param_type.getAsString()));
    }
  }
  clang::QualType return_type = proto.getReturnType();
  if (!return_type->isVoidType() &&
      !FitsInOneArgumentRegister(ast_ctx, return_type)) {
    return absl::InvalidArgumentError(absl::Substitute(
        "callback $0 returns '$1'. The trampoline the sandboxee reaches it "
        "through returns one 64-bit integer or floating-point register, so an "
        "extended-precision floating-point, by-value struct or 128-bit result "
        "never arrives",
        name, return_type.getAsString()));
  }
  return absl::OkStatus();
}

// Looks up the `CallbackTypedef` for any callback `typedef` referenced by
// `type`, or returns `nullptr` if `type` is an inline callback or references a
// typedef from an unannotated header.
const CallbackTypedef* LookupCallbackTypedef(
    clang::QualType type, const ConversionContext& conv_ctx) {
  const clang::TypedefNameDecl* td = FindCallbackTypedef(type);
  if (td == nullptr) {
    return nullptr;
  }
  auto it = conv_ctx.callback_typedefs.find(td->getNameAsString());
  if (it == conv_ctx.callback_typedefs.end()) {
    return nullptr;
  }
  return &it->second;
}

// Collects the annotations for each parameter of a callback (from `cb_typedef`
// if non-null, or parsed from `cb_decls` otherwise) and validates them against
// the parameter types in `cb_decls`.
absl::StatusOr<std::vector<Annotations>> ResolveCallbackParamAnnotations(
    absl::string_view name, const std::vector<std::string>& cb_param_names,
    const std::vector<const clang::ParmVarDecl*>& cb_decls,
    const CallbackTypedef* cb_typedef, const ConversionContext& conv_ctx) {
  std::vector<Annotations> result;
  result.reserve(cb_decls.size());
  for (size_t i = 0; i < cb_decls.size(); ++i) {
    Annotations ann;
    if (cb_typedef != nullptr) {
      ann = cb_typedef->params[i].annotations;
    } else {
      ABSL_ASSIGN_OR_RETURN(ann,
                            ParseAnnotations(cb_param_names[i], cb_decls[i]));
    }
    if (!ann.pointee_contract.has_value()) {
      if (const clang::TypedefNameDecl* pt_td =
              FindPointeeTypedef(cb_decls[i]->getType())) {
        if (conv_ctx.pointee_contracts.contains(pt_td->getNameAsString())) {
          ann.pointee_contract = pt_td->getNameAsString();
        }
      }
    }
    ABSL_RETURN_IF_ERROR(CheckCallbackParamAnnotations(
        cb_decls[i]->getASTContext(), name, cb_param_names[i], ann,
        cb_decls[i]->getType(), conv_ctx));
    result.push_back(std::move(ann));
  }
  return result;
}

absl::StatusOr<ir::Parameter> ConvertParameterToIR(
    absl::string_view name, clang::QualType type, const ParamSource& source,
    const ConversionContext& conv_ctx) {
  const bool is_return_value =
      std::holds_alternative<FunctionReturnSource>(source) ||
      std::holds_alternative<CallbackReturnSource>(source);
  const bool is_callback_param =
      std::holds_alternative<CallbackParamSource>(source);
  const bool is_pointee_contract =
      std::holds_alternative<PointeeContractSource>(source);

  ABSL_ASSIGN_OR_RETURN(
      Annotations ann,
      std::visit(
          absl::Overload{
              [&](const FunctionParamSource& s) {
                return ParseAnnotations(name, s.decl);
              },
              [](const CallbackParamSource& s) -> absl::StatusOr<Annotations> {
                // Already resolved and validated by the caller (`cb_typedef` or
                // inline parameter annotations).
                return s.annotations;
              },
              [&](const FunctionReturnSource& s) {
                return ParseAnnotations(name, s.decl);
              },
              [](const CallbackReturnSource&) -> absl::StatusOr<Annotations> {
                // A callback return value has no declaration to parse
                // annotations from. Only the direction and bounds written on
                // the enclosing callback parameter describe the returned
                // buffer; they are read straight out of `CallbackReturnSource`
                // where the buffer payload is built below. The parameter's
                // other annotations (lifetime, context binding, struct sync)
                // describe the parameter itself and must not leak onto the
                // return value.
                return Annotations{};
              },
              [](const PointeeContractSource& s)
                  -> absl::StatusOr<Annotations> { return s.annotations; },
          },
          source));

  if (!is_return_value && !ann.pointee_contract.has_value()) {
    if (const clang::TypedefNameDecl* pt_td = FindPointeeTypedef(type)) {
      if (conv_ctx.pointee_contracts.contains(pt_td->getNameAsString())) {
        ann.pointee_contract = pt_td->getNameAsString();
      }
    }
  }

  ABSL_RETURN_IF_ERROR(
      ValidateParamAnnotations(name, type, ann, is_return_value,
                               is_callback_param, is_pointee_contract));

  ABSL_ASSIGN_OR_RETURN(ir::TypeInfo type_info, QualTypeToTypeInfo(type));

  ir::Parameter ir_param{
      .name = std::string(name),
      .type = std::move(type_info),
      .is_return_value = is_return_value,
  };

  if (const auto* fp = std::get_if<FunctionParamSource>(&source);
      fp != nullptr && ann.context_bound.copy_from_and_bind.has_value() &&
      !IsSupportedOutParamContextBoundType(fp->decl->getASTContext(), type,
                                           conv_ctx)) {
    return absl::InvalidArgumentError(absl::Substitute(
        "pointer argument $0 has unsupported pointee type", name));
  }

  // Note: functors need no exemption here; they are record types, so they
  // never satisfy isPointerType().
  if (type->isPointerType() && !ann.pointee_contract.has_value() &&
      (ann.ptr_dir.has_value() || !type->getPointeeType()->isPointerType()) &&
      !type->isFunctionPointerType() &&
      ann.ptr_dir != PointerDir::kSandboxOpaque &&
      ann.ptr_dir != PointerDir::kHostOpaque &&
      !(ir_param.type.kind == ir::TypeKind::kString ||
        (ir_param.type.is_pointer() && ir_param.type.is_pointee_string()))) {
    const clang::ASTContext* ast_ctx = GetASTContext(source);
    auto is_supported_malloc_ptr = [&](clang::QualType alloc_ptr) {
      if (ast_ctx == nullptr || !alloc_ptr->isPointerType() ||
          alloc_ptr->isFunctionPointerType()) {
        return false;
      }
      if ((std::holds_alternative<ByteSizedBy>(ann.size_type) ||
           (std::holds_alternative<SizedByBinding>(ann.size_type) &&
            !std::get<SizedByBinding>(ann.size_type).is_elem_sized)) &&
          IsSupportedArgByteSizedByType(alloc_ptr)) {
        return true;
      }
      return IsDeeplyTriviallyCopyableType(
          *ast_ctx, alloc_ptr->getPointeeType(), conv_ctx,
          /*reason=*/nullptr, /*in_array=*/true);
    };
    if ((std::holds_alternative<FunctionParamSource>(source) ||
         std::holds_alternative<CallbackParamSource>(source) ||
         is_pointee_contract) &&
        ast_ctx != nullptr) {
      const clang::ASTContext& context = *ast_ctx;
      std::string copyability_reason;
      const bool is_null_terminated_struct_array =
          ann.ptr_dir == PointerDir::kIn &&
          std::holds_alternative<NullTerminated>(ann.size_type);
      const bool is_single_struct_ptr =
          std::holds_alternative<std::monostate>(ann.size_type) &&
          (ann.ptr_dir == PointerDir::kIn || ann.ptr_dir == PointerDir::kOut ||
           ann.ptr_dir == PointerDir::kInOut);
      if (!is_callback_param && !is_pointee_contract &&
          (is_null_terminated_struct_array || is_single_struct_ptr) &&
          ann.struct_sync.empty() && !ann.shallow_struct_sync) {
        if (const auto* record_decl = type->getPointeeType()->getAsRecordDecl();
            record_decl != nullptr &&
            type->getPointeeType().isTriviallyCopyableType(context) &&
            !record_decl->hasFlexibleArrayMember()) {
          auto rec_it =
              conv_ctx.record_annotations.find(record_decl->getName());
          if (rec_it != conv_ctx.record_annotations.end()) {
            const RecordAnnotations& rec_ann = rec_it->second;
            std::vector<StructSync> inferred_syncs;
            bool all_fields_ok = true;
            for (const clang::FieldDecl* field : record_decl->fields()) {
              llvm::StringRef field_name = field->getName();
              const DataMemberAnnotations* matched_member = nullptr;
              for (const auto& member_ann : rec_ann.member_annotations) {
                if (member_ann.name == field_name) {
                  matched_member = &member_ann;
                  break;
                }
              }
              if (matched_member != nullptr) {
                if (matched_member->ptr_dir == PointerDir::kSandboxOpaque ||
                    matched_member->callback_typedef.has_value()) {
                  continue;
                }
                if (!std::holds_alternative<std::monostate>(
                        matched_member->size_type) &&
                    field->getType()->isPointerType() &&
                    !field->getType()->isFunctionPointerType() &&
                    (!std::holds_alternative<NullTerminated>(
                         matched_member->size_type) ||
                     IsSupportedNullTerminatedInputType(field->getType()))) {
                  const bool is_void_byte_sized =
                      (std::holds_alternative<ByteSizedBy>(
                           matched_member->size_type) ||
                       (std::holds_alternative<SizedByBinding>(
                            matched_member->size_type) &&
                        !std::get<SizedByBinding>(matched_member->size_type)
                             .is_elem_sized)) &&
                      IsSupportedArgByteSizedByType(field->getType());
                  // The pointed-to buffer is copied as raw bytes, and callback
                  // fields inside it are never translated (`collect_callbacks`
                  // only follows by-value struct fields). Check it as an array
                  // so such callback fields are rejected instead of copying
                  // host function pointers across the sandbox boundary.
                  std::string pointee_reason;
                  if (!is_void_byte_sized &&
                      !IsDeeplyTriviallyCopyableType(
                          context, field->getType()->getPointeeType(), conv_ctx,
                          &pointee_reason, /*in_array=*/true)) {
                    copyability_reason = absl::Substitute(
                        "field $0 of '$1': $2", std::string(field_name),
                        type->getPointeeType().getAsString(), pointee_reason);
                    all_fields_ok = false;
                    break;
                  }
                  const bool is_const_or_null_term =
                      field->getType()->getPointeeType().isConstQualified() ||
                      std::holds_alternative<NullTerminated>(
                          matched_member->size_type);
                  if (is_null_terminated_struct_array &&
                      (!field->getType()->getPointeeType().isConstQualified() ||
                       !std::holds_alternative<std::monostate>(
                           matched_member->lifetime))) {
                    copyability_reason = absl::Substitute(
                        "field $0 of '$1': null-terminated struct array "
                        "requires const non-malloc pointee",
                        std::string(field_name),
                        type->getPointeeType().getAsString());
                    all_fields_ok = false;
                    break;
                  }
                  const bool is_malloc_out =
                      std::holds_alternative<MallocLifetime>(
                          matched_member->lifetime) &&
                      *ann.ptr_dir != PointerDir::kIn;
                  if (is_single_struct_ptr && !is_malloc_out &&
                      *ann.ptr_dir == PointerDir::kOut) {
                    copyability_reason = absl::Substitute(
                        "field $0 of '$1': caller-allocated pointer field on "
                        "out_ptr struct requires inout_ptr on the struct "
                        "parameter",
                        std::string(field_name),
                        type->getPointeeType().getAsString());
                    all_fields_ok = false;
                    break;
                  }
                  PointerDir member_dir =
                      is_malloc_out ? PointerDir::kOut
                                    : ((is_null_terminated_struct_array ||
                                        is_const_or_null_term)
                                           ? PointerDir::kIn
                                           : *ann.ptr_dir);
                  inferred_syncs.push_back(StructSync{
                      .access_path =
                          absl::StrCat(name, "->", field->getNameAsString()),
                      .ptr_dir = member_dir,
                      .lifetime = is_malloc_out
                                      ? PointerLifetime{MallocLifetime{}}
                                      : PointerLifetime{},
                  });
                  continue;
                }
              }
              std::string field_reason;
              if (!IsDeeplyTriviallyCopyableType(context, field->getType(),
                                                 conv_ctx, &field_reason)) {
                copyability_reason = absl::Substitute(
                    "field $0 of '$1': $2", std::string(field_name),
                    type->getPointeeType().getAsString(), field_reason);
                all_fields_ok = false;
                break;
              }
            }
            if (all_fields_ok && !inferred_syncs.empty()) {
              ann.struct_sync = std::move(inferred_syncs);
            }
          }
        }
      }
      // Set only when the check below runs and fails, which is exactly when
      // the chain can reach the error return.
      if (!ann.shallow_struct_sync && ann.struct_sync.empty() &&
          !IsDeeplyTriviallyCopyableType(
              context, type->getPointeeType(), conv_ctx,
              copyability_reason.empty() ? &copyability_reason : nullptr,
              /*in_array=*/false, /*in_callback=*/is_callback_param) &&
          !((std::holds_alternative<ByteSizedBy>(ann.size_type) ||
             (std::holds_alternative<SizedByBinding>(ann.size_type) &&
              !std::get<SizedByBinding>(ann.size_type).is_elem_sized)) &&
            IsSupportedArgByteSizedByType(type)) &&
          !(std::holds_alternative<NullTerminated>(ann.size_type) &&
            std::holds_alternative<SandboxGlobalLifetime>(ann.lifetime) &&
            ann.ptr_dir != PointerDir::kIn &&
            (IsSupportedOutParamNullTerminatedType(type) ||
             (is_pointee_contract &&
              IsSupportedNullTerminatedGlobalStringType(type)))) &&
          !(std::holds_alternative<MallocLifetime>(ann.lifetime) &&
            ann.ptr_dir == PointerDir::kOut &&
            is_supported_malloc_ptr(
                is_pointee_contract ? type : type->getPointeeType())) &&
          !(ann.context_bound.copy_from_and_bind.has_value() &&
            ann.ptr_dir == PointerDir::kOut &&
            (IsSupportedOutParamContextBoundType(context, type, conv_ctx) ||
             (is_pointee_contract &&
              IsDeeplyTriviallyCopyableType(context, type->getPointeeType(),
                                            conv_ctx))))) {
        return absl::InvalidArgumentError(absl::Substitute(
            "pointer argument $0 has unsupported pointee type: $1", name,
            copyability_reason));
      }
    } else if (is_return_value &&
               !(type->getPointeeType()->isArithmeticType() ||
                 type->getPointeeType()->isEnumeralType()) &&
               !std::holds_alternative<AliasHostPtrLifetime>(ann.lifetime) &&
               !std::holds_alternative<AliasCallbackReturnLifetime>(
                   ann.lifetime) &&
               !(std::holds_alternative<MallocLifetime>(ann.lifetime) &&
                 is_supported_malloc_ptr(type))) {
      const auto* fr = std::get_if<FunctionReturnSource>(&source);
      if (fr == nullptr || !ann.context_bound.copy_from_and_bind.has_value() ||
          !IsDeeplyTriviallyCopyableType(fr->decl->getASTContext(),
                                         type->getPointeeType(), conv_ctx)) {
        return absl::InvalidArgumentError(absl::Substitute(
            "return pointer $0 has unsupported pointee type", name));
      }
    }
  }

  if (ir_param.type.kind == ir::TypeKind::kVoid) {
    if (!is_return_value) {
      return absl::InvalidArgumentError(
          absl::Substitute("parameter $0 cannot have void type", name));
    }
    ir_param.payload = ir::VoidParam{};
    return ir_param;
  }

  // Populate callback parameter metadata if applicable
  if (ir_param.type.kind == ir::TypeKind::kCallback) {
    ir::CallbackParam cb;
    const clang::ASTContext* ast_ctx = GetASTContext(source);
    // Check ABI compatibility before resolving annotations or typedefs.
    // Callbacks without a function prototype (`FunctionNoProtoType`) are
    // rejected below by `ExtractCallbackParams`.
    if (ast_ctx != nullptr) {
      if (const clang::FunctionProtoType* proto =
              ast::GetFunctionProtoType(type);
          proto != nullptr) {
        ABSL_RETURN_IF_ERROR(
            CheckCallbackSignatureIsCallable(name, *ast_ctx, *proto));
      }
    }

    const clang::ParmVarDecl* param = std::visit(
        absl::Overload{
            [](const FunctionParamSource& s) { return s.decl; },
            [](const CallbackParamSource& s) { return s.decl; },
            [](const FunctionReturnSource&) -> const clang::ParmVarDecl* {
              return nullptr;
            },
            [](const CallbackReturnSource&) -> const clang::ParmVarDecl* {
              return nullptr;
            },
            [](const PointeeContractSource&) -> const clang::ParmVarDecl* {
              return nullptr;
            },
        },
        source);
    if (param != nullptr) {
      // Resolve any referenced callback `typedef` first so all callback-return
      // and callback-parameter annotations below come from a single source.
      const CallbackTypedef* cb_typedef =
          LookupCallbackTypedef(param->getType(), conv_ctx);
      if (cb_typedef == nullptr) {
        if (const clang::TypedefNameDecl* td =
                FindCallbackTypedef(param->getType());
            td != nullptr && !HasSandboxAnnotations(*td)) {
          ABSL_RETURN_IF_ERROR(ParseCallbackTypedef(*td).status());
        }
      } else if (cb_typedef->no_reverse_reason.empty()) {
        cb.typedef_name = cb_typedef->name;
      }
      // Only a callback `typedef` can specify annotations for a callback's
      // return value; `CheckParsedAnnotations` rejects pointer-returning
      // callbacks that do not reference a typedef.
      const Annotations kNoReturnAnnotations;
      const Annotations& return_ann = cb_typedef != nullptr
                                          ? cb_typedef->return_annotations
                                          : kNoReturnAnnotations;
      cb.uninitialized = return_ann.uninitialized;
      cb.idempotent = cb_typedef != nullptr ? cb_typedef->idempotent
                                            : ann.callback_idempotent;

      std::vector<std::string> cb_names;
      std::vector<const clang::ParmVarDecl*> cb_decls;
      ABSL_RETURN_IF_ERROR(ExtractCallbackParams(*param, cb_names, cb_decls));
      if (cb_typedef != nullptr) {
        for (size_t i = 0; i < cb_names.size(); ++i) {
          cb_names[i] = cb_typedef->params[i].name;
        }
      }
      ABSL_ASSIGN_OR_RETURN(
          const std::vector<Annotations> cb_annotations,
          ResolveCallbackParamAnnotations(name, cb_names, cb_decls, cb_typedef,
                                          conv_ctx));
      cb.callback_parameters.reserve(cb_decls.size());
      for (size_t i = 0; i < cb_decls.size(); ++i) {
        ABSL_ASSIGN_OR_RETURN(
            ir::Parameter cb_param,
            ConvertParameterToIR(
                cb_names[i], cb_decls[i]->getType(),
                CallbackParamSource{cb_decls[i], cb_annotations[i]}, conv_ctx));
        cb.callback_parameters.push_back(std::move(cb_param));
      }
      clang::QualType cb_return_qual_type;
      std::string template_name;
      if (const auto* functor_type = ast::GetFunctorUnderlyingFunctionType(
              param->getType(), template_name)) {
        cb.functor_template_name = template_name;
        cb_return_qual_type = functor_type->getReturnType();
      } else if (param->getType()->isFunctionPointerType()) {
        const auto* function_type = param->getType()
                                        ->getPointeeType()
                                        ->getAs<clang::FunctionProtoType>();
        if (function_type == nullptr) {
          // Unreachable in practice: `ExtractCallbackParams` above already
          // requires a `FunctionProtoTypeLoc`. Kept so that a prototype-less
          // callback can never silently lose its return value.
          return absl::InvalidArgumentError(
              absl::Substitute("callback $0 does not have a prototype", name));
        }
        cb_return_qual_type = function_type->getReturnType();
      }
      if (!cb_return_qual_type.isNull() && !cb_return_qual_type->isVoidType()) {
        ABSL_ASSIGN_OR_RETURN(
            ir::Parameter cb_ret_param,
            ConvertParameterToIR("return_val", cb_return_qual_type,
                                 CallbackReturnSource{return_ann}, conv_ctx));
        cb.return_value =
            std::make_shared<ir::Parameter>(std::move(cb_ret_param));
      }
    } else {
      // No `ParmVarDecl`, so this callback is being handed back rather than
      // passed in. There is nothing here to read the callback's own parameter
      // names and annotations from, which is exactly the gap a callback typedef
      // fills, and the invoker built from that typedef is what would make
      // the call. So a callback typedef is required, and is also the only thing
      // worth recording.
      std::string template_name;
      if (ast::GetFunctorUnderlyingFunctionType(type, template_name) !=
          nullptr) {
        // A functor is an object with captured state that only means anything
        // in the address space that built it, so there is nothing a thunk
        // could stand in for.
        return absl::InvalidArgumentError(absl::Substitute(
            "$0 hands back C++ functor '$1', which is not supported. Only a "
            "plain function pointer can be replaced by a host thunk",
            name, template_name));
      }
      if (std::holds_alternative<CallbackReturnSource>(source)) {
        // A callback typedef describes one callback, not a callback that
        // itself hands back another callback.
        return absl::InvalidArgumentError(absl::Substitute(
            "$0: a callback that hands back another callback is not supported",
            name));
      }
      const clang::TypedefNameDecl* td = FindCallbackTypedef(type);
      if (td == nullptr) {
        return absl::InvalidArgumentError(absl::Substitute(
            "$0 hands back a callback, so its return type must be a callback "
            "typedef: a return has no parameter declarations to read the "
            "callback's annotations from",
            name));
      }
      const CallbackTypedef* cb_typedef = LookupCallbackTypedef(type, conv_ctx);
      if (cb_typedef == nullptr) {
        if (!HasSandboxAnnotations(*td)) {
          ABSL_RETURN_IF_ERROR(ParseCallbackTypedef(*td).status());
        }
        return absl::InvalidArgumentError(
            absl::Substitute("$0 references unknown callback typedef $1", name,
                             td->getNameAsString()));
      }
      if (!cb_typedef->no_reverse_reason.empty()) {
        return absl::InvalidArgumentError(
            absl::Substitute("$0 hands back a callback, but $1", name,
                             cb_typedef->no_reverse_reason));
      }
      ir_param.payload = ir::SandboxeeCallbackParam{
          .typedef_name = cb_typedef->name,
      };
      return ir_param;
    }
    ir_param.payload = std::move(cb);
    return ir_param;
  }

  // C++ string types. TypeKind::kString covers every spelling; the exact form
  // is recovered from the canonical name normalized by QualTypeToTypeInfo.
  if (ir_param.type.kind == ir::TypeKind::kString ||
      (ir_param.type.is_pointer() && ir_param.type.is_pointee_string())) {
    ir::CppStringParam cpp_str;
    cpp_str.form = ir_param.type.is_pointer()
                       ? ir::CppStringParam::Form::kPointer
                       : CppStringForm(ir_param.type.canonical_name);
    // For `const std::string*` it is the pointee that is const, not the
    // pointer itself, and only the pointee's const-ness says whether the
    // string may be written back after the call.
    cpp_str.is_const = ir_param.type.is_pointer()
                           ? ir_param.type.pointee->is_const
                           : ir_param.type.is_const;
    // Only a form the callee can write through defaults to kInOut. In practice
    // a string pointer always arrives with an explicit direction already: a
    // regular parameter without one is rejected above, a callback parameter
    // has one defaulted from its pointee, and a pointer return is rejected
    // just below. The kPointer term therefore never decides anything today; it
    // is kept so the fallback stays correct, rather than silently read-only,
    // if that validation is ever relaxed.
    PointerDir default_dir =
        (!cpp_str.is_const &&
         (cpp_str.form == ir::CppStringParam::Form::kPointer ||
          cpp_str.form == ir::CppStringParam::Form::kReference))
            ? PointerDir::kInOut
            : PointerDir::kIn;
    cpp_str.direction = ann.ptr_dir.value_or(default_dir);
    // A const string cannot be written back, so an out direction on one is a
    // contradiction. Honoring it would emit glue that copies out through a
    // pointer-to-const.
    if (cpp_str.is_const && cpp_str.direction != PointerDir::kIn) {
      return absl::InvalidArgumentError(absl::Substitute(
          "argument $0: a const C++ string cannot be an output parameter",
          name));
    }
    // A C++ string return is marshalled back through a LenVal buffer and
    // materialized as a fresh std::string on the host. Only a by-value return
    // can own that string: a reference, std::string_view or pointer return
    // would have to refer to a temporary that dies with the wrapper.
    if (is_return_value && cpp_str.form != ir::CppStringParam::Form::kValue) {
      return absl::InvalidArgumentError(absl::Substitute(
          "return value $0 returns a C++ string indirectly. Only returning "
          "std::string by value is supported.",
          name));
    }
    ir_param.payload = cpp_str;
    return ir_param;
  }

  // Opaque pointers
  if (ann.ptr_dir == PointerDir::kSandboxOpaque ||
      ann.ptr_dir == PointerDir::kHostOpaque) {
    ir_param.payload = ir::OpaquePointerParam{
        .direction = *ann.ptr_dir,
        .lifetime = AnnotationsToLifetimePolicy(ann),
        .context_bound = std::move(ann.context_bound),
    };
    return ir_param;
  }

  // Struct synchronized pointers (including structs whose fields, or nested
  // struct fields, carry callback typedefs).
  std::vector<ir::StructCallbackField> struct_callbacks;
  if (type->isPointerType() && !is_callback_param) {
    if (const auto* top_record = type->getPointeeType()->getAsRecordDecl();
        top_record != nullptr) {
      auto collect_callbacks =
          [&](auto& self, const clang::RecordDecl& record_decl,
              absl::string_view prefix, bool in_array) -> absl::Status {
        const RecordAnnotations* rec_ann = nullptr;
        auto it = conv_ctx.record_annotations.find(record_decl.getName());
        if (it != conv_ctx.record_annotations.end()) {
          rec_ann = &it->second;
        }
        for (const clang::FieldDecl* field : record_decl.fields()) {
          const bool field_in_array =
              in_array || field->getType()->isArrayType();
          clang::QualType base_field_type =
              field->getASTContext().getBaseElementType(field->getType());
          std::string member_path =
              absl::StrCat(prefix, field->getNameAsString());
          bool is_opaque_member = false;
          bool has_callback_typedef = false;
          if (rec_ann != nullptr) {
            for (const auto& member_ann : rec_ann->member_annotations) {
              if (member_ann.name == field->getName()) {
                is_opaque_member =
                    (member_ann.ptr_dir == PointerDir::kSandboxOpaque);
                if (member_ann.callback_typedef.has_value()) {
                  has_callback_typedef = true;
                  std::string field_label = absl::Substitute(
                      "field $0 of '$1'", field->getNameAsString(),
                      record_decl.getNameAsString());
                  if (field_in_array) {
                    return absl::InvalidArgumentError(absl::Substitute(
                        "$0: callback fields inside an array are not supported",
                        field_label));
                  }
                  const clang::FunctionProtoType* proto =
                      ast::GetFunctionProtoType(field->getType());
                  if (proto == nullptr) {
                    return absl::InvalidArgumentError(absl::Substitute(
                        "callback $0 does not have a prototype", field_label));
                  }
                  ABSL_RETURN_IF_ERROR(CheckCallbackSignatureIsCallable(
                      field_label, field->getASTContext(), *proto));
                  auto typedef_it = conv_ctx.callback_typedefs.find(
                      *member_ann.callback_typedef);
                  if (typedef_it == conv_ctx.callback_typedefs.end()) {
                    return absl::InvalidArgumentError(absl::Substitute(
                        "callback $0 names unknown callback typedef $1",
                        field_label, *member_ann.callback_typedef));
                  }
                  const CallbackTypedef* cb_typedef = &typedef_it->second;
                  std::string canonical_type = clang::QualType(proto, 0)
                                                   .getCanonicalType()
                                                   .getAsString();
                  if (canonical_type != cb_typedef->canonical_type) {
                    return absl::InvalidArgumentError(absl::Substitute(
                        "callback $0 has type '$1', which does not match "
                        "callback typedef $2 of type '$3'",
                        field_label, canonical_type, cb_typedef->name,
                        cb_typedef->canonical_type));
                  }
                  if (!cb_typedef->no_reverse_reason.empty()) {
                    return absl::InvalidArgumentError(absl::Substitute(
                        "$0 hands back a callback, but $1", field_label,
                        cb_typedef->no_reverse_reason));
                  }
                  struct_callbacks.push_back(ir::StructCallbackField{
                      .member_name = member_path,
                      .typedef_name = cb_typedef->name,
                  });
                }
                break;
              }
            }
          }
          if (!ann.shallow_struct_sync && !is_opaque_member &&
              !has_callback_typedef && field_in_array &&
              base_field_type->isFunctionPointerType()) {
            return absl::InvalidArgumentError(absl::Substitute(
                "pointer argument $0 has unsupported pointee type: field $1 of "
                "'$2': $3",
                name, field->getNameAsString(), record_decl.getNameAsString(),
                DescribeUncopyableType(base_field_type)));
          }
          if (const auto* nested_record = base_field_type->getAsRecordDecl();
              nested_record != nullptr) {
            ABSL_RETURN_IF_ERROR(self(self, *nested_record,
                                      absl::StrCat(member_path, "."),
                                      field_in_array));
          }
        }
        return absl::OkStatus();
      };
      ABSL_RETURN_IF_ERROR(collect_callbacks(collect_callbacks, *top_record, "",
                                             /*in_array=*/false));
    }
  }

  if (is_return_value && !struct_callbacks.empty()) {
    return absl::InvalidArgumentError(absl::Substitute(
        "return pointer $0: structs with callback fields cannot be returned by "
        "pointer",
        name));
  }

  if (!struct_callbacks.empty() &&
      !std::holds_alternative<std::monostate>(ann.size_type) &&
      !(std::holds_alternative<NullTerminated>(ann.size_type) &&
        ann.ptr_dir == PointerDir::kIn &&
        type->getPointeeType().isConstQualified())) {
    return absl::InvalidArgumentError(absl::Substitute(
        "$0: array of structs with callback fields is not supported", name));
  }

  if (!ann.struct_sync.empty() || ann.shallow_struct_sync ||
      !struct_callbacks.empty()) {
    // Unlike a plain buffer, a synchronized struct has no defensible default
    // direction: guessing kIn would silently drop the write-back of the
    // synchronized members, which is the whole point of the annotation. Every
    // path that reaches here already requires the direction to be spelled out
    // (see the buffer-pointer check and the callback parameter check above),
    // so this is a backstop that keeps the requirement local and explicit.
    if (!ann.ptr_dir.has_value()) {
      return absl::InvalidArgumentError(absl::Substitute(
          "argument $0: struct_sync requires an explicit pointer direction",
          name));
    }
    ir::StructSyncParam sync_param{
        .direction = *ann.ptr_dir,
        .callbacks = std::move(struct_callbacks),
        .is_shallow = ann.shallow_struct_sync,
        .array_bounds = AnnotationsToBufferBounds(ann),
        .context_bound = std::move(ann.context_bound),
    };
    // The pointee's record declaration and annotations are the same for every
    // synchronized member, so resolve them once rather than per member. The
    // annotations guarantee a pointer to a record here (see
    // CheckParsedAnnotations).
    const clang::RecordDecl* record_decl = nullptr;
    const RecordAnnotations* rec_ann = nullptr;
    if (type->isPointerType()) {
      if (const auto* rd = type->getPointeeType()->getAsRecordDecl();
          rd != nullptr) {
        record_decl = rd->getDefinition() != nullptr ? rd->getDefinition() : rd;
        auto it = conv_ctx.record_annotations.find(record_decl->getName());
        if (it != conv_ctx.record_annotations.end()) {
          rec_ann = &it->second;
        }
      }
    }

    sync_param.members.reserve(ann.struct_sync.size());
    for (const auto& sync : ann.struct_sync) {
      std::optional<std::string> member_name =
          ast::MemberNameOfAccessPath(sync.access_path);
      std::optional<std::string> parent_prefix =
          ast::ParentPrefixOfAccessPath(sync.access_path);
      if (!member_name.has_value() || !parent_prefix.has_value()) {
        return absl::InvalidArgumentError(absl::Substitute(
            "struct_sync access path format $0 is not supported",
            sync.access_path));
      }
      Annotations sync_a;
      sync_a.lifetime = sync.lifetime;
      ir::StructMemberSync ir_sync{
          .member_name = std::move(*member_name),
          .parent_prefix = std::move(*parent_prefix),
          .direction = sync.ptr_dir,
          .context_bound = sync.context_bound,
          .lifetime = AnnotationsToLifetimePolicy(sync_a),
      };
      if (rec_ann != nullptr) {
        for (const auto& member_ann : rec_ann->member_annotations) {
          if (member_ann.name == ir_sync.member_name) {
            Annotations member_a;
            member_a.size_type = member_ann.size_type;
            ir_sync.bounds = AnnotationsToBufferBounds(member_a);
            if (record_decl != nullptr) {
              std::string size_expr_prefix = ir_sync.parent_prefix;
              if (!std::holds_alternative<std::monostate>(ann.size_type)) {
                absl::string_view nested_prefix = ir_sync.parent_prefix;
                absl::ConsumePrefix(&nested_prefix, absl::StrCat(name, "->"));
                size_expr_prefix =
                    absl::Substitute("$0[sapi_i].$1", name, nested_prefix);
              }
              if (auto* elem =
                      std::get_if<ir::bounds::ElemCount>(&ir_sync.bounds)) {
                elem->size_expr = ast::PrefixStructMemberSizeExpr(
                    *record_decl, size_expr_prefix, elem->size_expr);
              } else if (auto* byte = std::get_if<ir::bounds::ByteCount>(
                             &ir_sync.bounds)) {
                byte->size_expr = ast::PrefixStructMemberSizeExpr(
                    *record_decl, size_expr_prefix, byte->size_expr);
              } else if (auto* binding =
                             std::get_if<ir::bounds::SizedByBinding>(
                                 &ir_sync.bounds)) {
                binding->context_expr = ast::PrefixStructMemberSizeExpr(
                    *record_decl, size_expr_prefix, binding->context_expr);
                binding->binding_name = ast::PrefixStructMemberBindingExpr(
                    *record_decl, size_expr_prefix, binding->binding_name);
              } else if (std::holds_alternative<ir::bounds::NullTerminated>(
                             ir_sync.bounds)) {
                const clang::FieldDecl* synced_field = nullptr;
                for (const clang::FieldDecl* f : record_decl->fields()) {
                  if (absl::string_view(f->getName().data(),
                                        f->getName().size()) ==
                      ir_sync.member_name) {
                    synced_field = f;
                    break;
                  }
                }
                if (synced_field != nullptr) {
                  clang::QualType ftype = synced_field->getType();
                  const bool is_const_struct_in =
                      ir_sync.direction == PointerDir::kIn &&
                      ftype->isPointerType() &&
                      ftype->getPointeeType()->isRecordType() &&
                      ftype->getPointeeType().isConstQualified();
                  if (!ftype->isPointerType() ||
                      !(IsIntegerOrEnumType(ftype->getPointeeType()) ||
                        is_const_struct_in)) {
                    return absl::InvalidArgumentError(absl::Substitute(
                        "struct_sync access path $0: null_terminated member "
                        "must point to an integer, enum, or const struct type",
                        sync.access_path));
                  }
                  if (ir_sync.direction == PointerDir::kInOut &&
                      ftype->getPointeeType().isConstQualified()) {
                    return absl::InvalidArgumentError(absl::Substitute(
                        "struct_sync access path $0: inout_ptr null_terminated "
                        "member cannot have a const pointee",
                        sync.access_path));
                  }
                  if (is_const_struct_in) {
                    // The pointed-to null-terminated struct array is copied as
                    // raw bytes, without translating nested pointers or
                    // registering callback trampolines.
                    std::string pointee_reason;
                    if (!IsDeeplyTriviallyCopyableType(
                            synced_field->getASTContext(),
                            ftype->getPointeeType(), conv_ctx, &pointee_reason,
                            /*in_array=*/true)) {
                      return absl::InvalidArgumentError(absl::Substitute(
                          "struct_sync access path $0 has unsupported pointee "
                          "type: $1",
                          sync.access_path, pointee_reason));
                    }
                  }
                }
              }
            }
            if (std::holds_alternative<MallocLifetime>(member_ann.lifetime) &&
                sync_param.direction != PointerDir::kIn &&
                ir_sync.direction != PointerDir::kIn) {
              ir_sync.lifetime = ir::lifetime::Malloc{};
            }
            break;
          }
        }
      }
      if (std::holds_alternative<ir::lifetime::Malloc>(ir_sync.lifetime)) {
        if (sync_param.direction == PointerDir::kIn ||
            ir_sync.direction != PointerDir::kOut) {
          return absl::InvalidArgumentError(absl::Substitute(
              "struct_sync access path $0: lifetime_malloc requires out_ptr "
              "member direction on a non-in_ptr struct",
              sync.access_path));
        }
        if (ir_sync.context_bound.copy_from_and_bind.has_value() ||
            ir_sync.context_bound.retain_and_bind.has_value()) {
          return absl::InvalidArgumentError(absl::Substitute(
              "struct_sync access path $0: lifetime_malloc cannot be combined "
              "with retain_and_bind or copy_from_and_bind_out_ptr",
              sync.access_path));
        }
        if (std::holds_alternative<ir::bounds::SizedByBinding>(
                ir_sync.bounds)) {
          return absl::InvalidArgumentError(absl::Substitute(
              "struct_sync access path $0: lifetime_malloc cannot be combined "
              "with sized_by_binding or elem_sized_by_binding, because the "
              "binding context would come from sandboxee-written fields",
              sync.access_path));
        }
      }
      if (std::holds_alternative<ir::bounds::NullTerminated>(ir_sync.bounds)) {
        if (ir_sync.context_bound.retain_and_bind.has_value()) {
          return absl::InvalidArgumentError(absl::Substitute(
              "struct_sync access path $0: cannot combine retain_and_bind "
              "with null_terminated",
              sync.access_path));
        }
        if (ir_sync.direction == PointerDir::kOut &&
            !std::holds_alternative<ir::lifetime::Malloc>(ir_sync.lifetime)) {
          return absl::InvalidArgumentError(absl::Substitute(
              "struct_sync access path $0: null_terminated output member "
              "requires a lifetime annotation",
              sync.access_path));
        }
      }
      sync_param.members.push_back(std::move(ir_sync));
    }
    ir_param.payload = std::move(sync_param);
    return ir_param;
  }

  // General buffer pointers. Null-terminated C-strings are buffers whose
  // bounds are `bounds::NullTerminated`.
  if (ir_param.type.is_pointer()) {
    if (const auto* cr = std::get_if<CallbackReturnSource>(&source)) {
      if (std::holds_alternative<NullTerminated>(
              cr->callback_annotations.size_type) &&
          !IsIntegerOrEnumType(type->getPointeeType())) {
        return absl::InvalidArgumentError(absl::Substitute(
            "callback return pointer $0 with null_terminated must point to an "
            "integer or enum type",
            name));
      }
      // A buffer returned by a callback always flows out of the callback
      // (`kOut`), and its lifetime is specified by the callback typedef's
      // `SANDBOX_LIFETIME_OUTER_CALL` annotation (enforced by
      // `CheckCallbackTypedefReturnAnnotations`).
      ir_param.payload = ir::BufferParam{
          .direction = PointerDir::kOut,
          .bounds = AnnotationsToBufferBounds(cr->callback_annotations),
          .lifetime = AnnotationsToLifetimePolicy(cr->callback_annotations),
      };
    } else if (!is_return_value && !is_pointee_contract &&
               !ann.pointee_contract.has_value() &&
               std::holds_alternative<MallocLifetime>(ann.lifetime)) {
      ABSL_ASSIGN_OR_RETURN(ir::TypeInfo inner_type_info,
                            QualTypeToTypeInfo(type->getPointeeType()));
      auto inner_param = std::make_shared<ir::Parameter>(ir::Parameter{
          .name = std::string(name),
          .type = std::move(inner_type_info),
          .payload =
              ir::BufferParam{
                  .direction = PointerDir::kOut,
                  .bounds = AnnotationsToBufferBounds(ann),
                  .lifetime = ir::lifetime::Malloc{},
              },
      });
      ir_param.payload = ir::BufferParam{
          .direction = PointerDir::kOut,
          .bounds = ir::bounds::Singleton{},
          .lifetime = ir::lifetime::ScopedCall{},
          .context_bound = std::move(ann.context_bound),
          .pointee = std::move(inner_param),
      };
    } else {
      std::shared_ptr<ir::Parameter> pointee = nullptr;
      if (ann.pointee_contract.has_value()) {
        ABSL_ASSIGN_OR_RETURN(
            const PointeeContract* contract,
            ResolvePointeeContract(name, type, ann, conv_ctx));
        ABSL_ASSIGN_OR_RETURN(
            ir::Parameter pointee_param,
            ConvertParameterToIR(contract->name, type->getPointeeType(),
                                 PointeeContractSource{GetASTContext(source),
                                                       contract->annotations},
                                 conv_ctx));
        if (!pointee_param.Is<ir::BufferParam>() &&
            !pointee_param.Is<ir::OpaquePointerParam>()) {
          return absl::InvalidArgumentError(absl::Substitute(
              "pointer argument $0: unsupported pointee contract target kind "
              "on typedef $1",
              name, contract->name));
        }
        if (const auto* inner_op = pointee_param.As<ir::OpaquePointerParam>();
            inner_op != nullptr &&
            inner_op->direction == PointerDir::kHostOpaque &&
            (ann.ptr_dir == PointerDir::kOut ||
             ann.ptr_dir == PointerDir::kInOut)) {
          return absl::InvalidArgumentError(absl::Substitute(
              "$0 $1: host_opaque_ptr pointee typedef $2 is not supported on "
              "output or in-out multi-level pointers",
              is_pointee_contract ? "pointee typedef" : "pointer argument",
              name, contract->name));
        }
        if (auto* inner_buf =
                std::get_if<ir::BufferParam>(&pointee_param.payload)) {
          if (std::holds_alternative<ir::lifetime::Malloc>(
                  inner_buf->lifetime) &&
              ann.ptr_dir != PointerDir::kOut) {
            return absl::InvalidArgumentError(absl::Substitute(
                "pointer argument $0: outer pointer for lifetime_malloc "
                "pointee typedef $1 must be SANDBOX_OUT_PTR",
                name, contract->name));
          }
          const bool pointee_is_callee_written =
              inner_buf->pointee == nullptr &&
              (inner_buf->context_bound.copy_from_and_bind.has_value() ||
               (pointee_param.IsOutput() &&
                !std::holds_alternative<ir::lifetime::ScopedCall>(
                    inner_buf->lifetime)));
          if (ann.ptr_dir == PointerDir::kOut && !pointee_is_callee_written) {
            return absl::InvalidArgumentError(absl::Substitute(
                "$0 $1: SANDBOX_OUT_PTR cannot wrap caller-allocated pointee "
                "typedef $2 because *$1 is read before the call; use "
                "SANDBOX_IN_PTR or SANDBOX_INOUT_PTR, or annotate $2 with a "
                "callee-allocated output lifetime",
                is_pointee_contract ? "pointee typedef" : "pointer argument",
                name, contract->name));
          }
          const bool pointee_is_output_only_callee_written =
              pointee_is_callee_written &&
              (inner_buf->context_bound.copy_from_and_bind.has_value() ||
               inner_buf->direction == PointerDir::kOut);
          if (ann.ptr_dir != PointerDir::kOut &&
              pointee_is_output_only_callee_written) {
            return absl::InvalidArgumentError(absl::Substitute(
                "$0 $1: output-only callee-written pointee typedef $2 requires "
                "SANDBOX_OUT_PTR on the wrapping pointer",
                is_pointee_contract ? "pointee typedef" : "pointer argument",
                name, contract->name));
          }
          if (inner_buf->context_bound.copy_from_and_bind.has_value()) {
            if (is_pointee_contract ||
                !std::holds_alternative<std::monostate>(ann.size_type) ||
                inner_buf->pointee != nullptr) {
              return absl::InvalidArgumentError(absl::Substitute(
                  "pointer argument $0: copy_from_and_bind_out_ptr on pointee "
                  "typedef $1 is only supported on a singleton 2-level pointer",
                  name, contract->name));
            }
            // `SANDBOX_COPY_FROM_AND_BIND_OUT_PTR` generates
            // `binding_<__LINE__>` once at the pointee `typedef` declaration
            // site. Prefix the slot key with `<func>_<param>_` at each
            // parameter use site so parameters or functions sharing that
            // `typedef` on the same context handle do not overwrite and free
            // each other's host buffer.
            std::string func_prefix;
            if (const auto* fp = std::get_if<FunctionParamSource>(&source);
                fp != nullptr && fp->decl != nullptr) {
              if (const auto* fn = llvm::dyn_cast<clang::FunctionDecl>(
                      fp->decl->getDeclContext())) {
                func_prefix =
                    clang::ASTNameGenerator(fn->getASTContext()).getName(fn);
                if (func_prefix.empty()) {
                  func_prefix = fn->getNameAsString();
                }
              }
            }
            inner_buf->context_bound.copy_from_and_bind->binding_name =
                func_prefix.empty()
                    ? absl::StrCat(name, "_",
                                   inner_buf->context_bound.copy_from_and_bind
                                       ->binding_name)
                    : absl::StrCat(func_prefix, "_", name, "_",
                                   inner_buf->context_bound.copy_from_and_bind
                                       ->binding_name);
          }
          if (inner_buf->pointee != nullptr) {
            if (is_pointee_contract) {
              return absl::InvalidArgumentError(absl::Substitute(
                  "pointee typedef $0: multi-level pointers deeper than 3 "
                  "levels are not supported",
                  name));
            }
            const auto* leaf_buf = inner_buf->pointee->As<ir::BufferParam>();
            const bool leaf_is_callee_written =
                inner_buf->pointee->Is<ir::OpaquePointerParam>() ||
                (leaf_buf != nullptr &&
                 (leaf_buf->context_bound.copy_from_and_bind.has_value() ||
                  !std::holds_alternative<ir::lifetime::ScopedCall>(
                      leaf_buf->lifetime)));
            if (!std::holds_alternative<std::monostate>(ann.size_type) &&
                pointee_param.IsOutput() && leaf_is_callee_written) {
              return absl::InvalidArgumentError(absl::Substitute(
                  "pointer argument $0: 3-level output pointers with "
                  "callee-written leaf pointers require a singleton outer "
                  "pointer",
                  name));
            }
          }
        }
        pointee = std::make_shared<ir::Parameter>(std::move(pointee_param));
      }
      // `ann.ptr_dir` is absent for function return values, which the
      // direction validation above exempts, and for pointer-to-pointer
      // arguments, which it does not classify as buffers. Buffer pointers on
      // regular and callback parameters always carry one.
      ir_param.payload = ir::BufferParam{
          .direction = ann.ptr_dir.value_or(is_return_value ? PointerDir::kOut
                                                            : PointerDir::kIn),
          .bounds = AnnotationsToBufferBounds(ann),
          .lifetime = AnnotationsToLifetimePolicy(ann),
          .context_bound = std::move(ann.context_bound),
          .uninitialized = ann.uninitialized,
          .pointee = std::move(pointee),
      };
    }
    return ir_param;
  }

  // Default: scalar / value parameter
  if (ir_param.type.kind != ir::TypeKind::kScalar &&
      ir_param.type.kind != ir::TypeKind::kStruct) {
    return absl::InvalidArgumentError(
        absl::Substitute("unexpected type kind $0 for value parameter $1",
                         static_cast<int>(ir_param.type.kind), name));
  }
  ir_param.payload = ir::ScalarParam{};
  return ir_param;
}

bool HasSandboxAttr(const clang::Decl* decl) {
  for (const auto* attr : decl->attrs()) {
    const auto* ann = llvm::dyn_cast<clang::AnnotateAttr>(attr);
    if (ann != nullptr && ann->getAnnotation() == "sandbox") {
      return true;
    }
  }
  return false;
}

bool HasAnnotatedPointeeTypedef(clang::QualType type,
                                const ConversionContext& conv_ctx) {
  const clang::TypedefNameDecl* pt_td = FindPointeeTypedef(type);
  return pt_td != nullptr &&
         (HasSandboxAnnotations(*pt_td) ||
          conv_ctx.pointee_contracts.contains(pt_td->getNameAsString()));
}

bool IsAnnotatedCallbackTypedef(const clang::TypedefNameDecl& cb_td,
                                const ConversionContext& conv_ctx) {
  if (HasSandboxAnnotations(cb_td)) {
    return true;
  }
  if (auto it = conv_ctx.callback_typedefs.find(cb_td.getNameAsString());
      it != conv_ctx.callback_typedefs.end()) {
    return it->second.has_annotations;
  }
  return false;
}

int ReturnCallbackTypedefRank(clang::QualType type,
                              const ConversionContext& conv_ctx) {
  const clang::TypedefNameDecl* cb_td = FindCallbackTypedef(type);
  if (cb_td == nullptr) {
    return 0;
  }
  return IsAnnotatedCallbackTypedef(*cb_td, conv_ctx) ? 2 : 1;
}

// Ranks how much sandbox-specific type or annotation metadata `param` carries
// so `ConvertFunctionToIR` upgrades to the most informative redeclaration
// without letting unannotated C header typedefs shadow annotated ones:
//   0: plain type or unannotated non-callback typedef (e.g. `raw_str*`).
//   1: unannotated callback typedef (still preferred over a raw function
//      pointer for reverse-callback thunk pool naming when unannotated).
//   2: explicit sandbox annotations (annotated callback typedef, annotated
//      pointee typedef, or inline callback parameter annotations).
//
// Outer `ParmVarDecl` attributes are intentionally not checked here: Clang Sema
// merges outer parameter `AnnotateAttr`s onto the first `FunctionDecl`, so
// scoring outer attributes would skip upgrading `param` to a redeclaration's
// `FunctionProtoTypeLoc` or typedef sugar.
int ParamRedeclRank(const clang::ParmVarDecl* param,
                    const ConversionContext& conv_ctx) {
  if (param == nullptr) {
    return 0;
  }
  if (HasAnnotatedPointeeTypedef(param->getType(), conv_ctx)) {
    return 2;
  }
  if (const clang::TypedefNameDecl* cb_td =
          FindCallbackTypedef(param->getType());
      cb_td != nullptr) {
    return IsAnnotatedCallbackTypedef(*cb_td, conv_ctx) ? 2 : 1;
  }
  std::optional<clang::FunctionProtoTypeLoc> ftl =
      ast::GetFunctionProtoTypeLoc(param->getTypeSourceInfo());
  if (!ftl.has_value()) {
    return 0;
  }
  for (unsigned i = 0; i < ftl->getNumParams(); ++i) {
    const clang::ParmVarDecl* cb_param = ftl->getParam(i);
    if (cb_param == nullptr) {
      continue;
    }
    if (HasAnnotatedPointeeTypedef(cb_param->getType(), conv_ctx) ||
        HasSandboxAttr(cb_param)) {
      return 2;
    }
  }
  return 0;
}

}  // namespace

absl::StatusOr<sapi::ir::Function> ConvertFunctionToIR(
    const clang::FunctionDecl* func_decl, const ConversionContext& conv_ctx) {
  std::string name =
      clang::ASTNameGenerator(func_decl->getASTContext()).getName(func_decl);
  if (name.empty()) {
    name = func_decl->getNameAsString();
  }
  sapi::ir::Function ir_func{.name = std::move(name)};

  ABSL_ASSIGN_OR_RETURN(
      Annotations func_decl_annotations,
      ParseAnnotations(func_decl->getNameAsString(), func_decl));
  ir_func.context_bound = std::move(func_decl_annotations.context_bound);

  clang::QualType ret_type = func_decl->getReturnType();
  int best_ret_rank = ReturnCallbackTypedefRank(ret_type, conv_ctx);
  if (best_ret_rank < 2) {
    for (const clang::FunctionDecl* redecl : func_decl->redecls()) {
      const int cand_rank =
          ReturnCallbackTypedefRank(redecl->getReturnType(), conv_ctx);
      if (cand_rank > best_ret_rank) {
        ret_type = redecl->getReturnType();
        best_ret_rank = cand_rank;
        if (best_ret_rank == 2) {
          break;
        }
      }
    }
  }
  if (!ret_type->isVoidType()) {
    ABSL_ASSIGN_OR_RETURN(
        ir_func.return_value,
        ConvertParameterToIR("sapi_ret_arg", ret_type,
                             FunctionReturnSource{func_decl}, conv_ctx));
  }

  ir_func.parameters.reserve(func_decl->getNumParams());
  for (size_t i = 0; i < func_decl->getNumParams(); ++i) {
    const clang::ParmVarDecl* param = func_decl->getParamDecl(i);
    int best_rank = ParamRedeclRank(param, conv_ctx);
    if (best_rank < 2) {
      for (const clang::FunctionDecl* redecl : func_decl->redecls()) {
        if (i >= redecl->getNumParams()) {
          continue;
        }
        const clang::ParmVarDecl* cand = redecl->getParamDecl(i);
        const int cand_rank = ParamRedeclRank(cand, conv_ctx);
        if (cand_rank > best_rank) {
          param = cand;
          best_rank = cand_rank;
          if (best_rank == 2) {
            break;
          }
        }
      }
    }
    std::string name = param->getNameAsString();
    if (name.empty()) {
      for (const clang::FunctionDecl* redecl : func_decl->redecls()) {
        if (i < redecl->getNumParams() &&
            !redecl->getParamDecl(i)->getNameAsString().empty()) {
          name = redecl->getParamDecl(i)->getNameAsString();
          break;
        }
      }
    }
    if (name.empty()) {
      name = absl::StrFormat("sapi_arg%zu", i);
    }
    ABSL_ASSIGN_OR_RETURN(
        ir::Parameter ir_param,
        ConvertParameterToIR(name, param->getType(), FunctionParamSource{param},
                             conv_ctx));
    ir_func.parameters.push_back(std::move(ir_param));
  }

  // Note: alias_ptr linking is performed later by ValidateAndLinkLibraryIR,
  // once every function in the library has been converted.
  return ir_func;
}

absl::StatusOr<InvokerConversion> ConvertCallbackTypedefToInvokerIR(
    const clang::TypedefNameDecl& decl, const CallbackTypedef& cb_typedef,
    const ConversionContext& conv_ctx) {
  const clang::FunctionProtoType* proto = GetCallbackTypedefProto(decl);
  std::optional<clang::FunctionProtoTypeLoc> ftl =
      ast::GetFunctionProtoTypeLoc(decl.getTypeSourceInfo());
  if (proto == nullptr || !ftl.has_value()) {
    return absl::InvalidArgumentError(absl::Substitute(
        "callback typedef $0 does not have a function proto type loc",
        cb_typedef.name));
  }
  ABSL_RETURN_IF_ERROR(CheckCallbackSignatureIsCallable(
      absl::StrCat("callback typedef ", cb_typedef.name), decl.getASTContext(),
      *proto));
  ir::Function invoker;
  invoker.name = ir::InvokerName(cb_typedef.name);
  invoker.invoker_for_typedef = cb_typedef.name;
  invoker.is_idempotent_typedef = cb_typedef.idempotent;

  clang::QualType ret_type = proto->getReturnType();
  if (!ret_type->isVoidType()) {
    // Deliberately not re-parsed off `decl`: a callback typedef's return
    // annotations are only legal on a callback typedef
    // (SANDBOX_LIFETIME_OUTER_CALL is the case that matters), so they are taken
    // as already parsed rather than read again at a site that would reject
    // them.
    ABSL_ASSIGN_OR_RETURN(
        invoker.return_value,
        ConvertParameterToIR(
            "sapi_ret_arg", ret_type,
            CallbackReturnSource{cb_typedef.return_annotations}, conv_ctx));
    // Anything the host would have to copy out of the sandbox and then keep
    // alive has no reverse direction yet; see the header for why the
    // typedef's own lifetime cannot say how long for. Keyed on the payload
    // rather than on the type so that an opaque handle, which needs no copy
    // but also has nothing saying it may be dereferenced, is excluded too.
    if (!invoker.return_value->Is<ir::ScalarParam>()) {
      return InvokerConversion{
          .no_reverse_reason = absl::Substitute(
              "callback typedef $0 hands back '$1'. A callback the sandbox "
              "implements is called by host code rather than from inside a "
              "call into the sandbox, so there is no enclosing call for that "
              "buffer to outlive and nothing to free it",
              cb_typedef.name, ret_type.getAsString()),
      };
    }
  }

  // The target goes first so that the typedef's parameters keep the positions
  // and the names the typedef gave them: a size annotation on one of them
  // refers to another by name, and those names come from the typedef.
  invoker.parameters.reserve(ftl->getNumParams() + 1);
  invoker.parameters.push_back(ir::Parameter{
      .name = std::string(ir::kInvokerTargetParam),
      .type = {.kind = ir::TypeKind::kScalar, .canonical_name = "uintptr_t"},
      .payload = ir::ScalarParam{},
  });
  for (unsigned i = 0; i < ftl->getNumParams(); ++i) {
    const clang::ParmVarDecl* param = ftl->getParam(i);
    Annotations param_ann = cb_typedef.params[i].annotations;
    if (!param_ann.pointee_contract.has_value()) {
      if (const clang::TypedefNameDecl* pt_td =
              FindPointeeTypedef(param->getType())) {
        if (conv_ctx.pointee_contracts.contains(pt_td->getNameAsString())) {
          param_ann.pointee_contract = pt_td->getNameAsString();
        }
      }
    }
    if (param_ann.ptr_dir != PointerDir::kSandboxOpaque) {
      ABSL_RETURN_IF_ERROR(CheckCallbackParamAnnotations(
          decl.getASTContext(), cb_typedef.name, cb_typedef.params[i].name,
          param_ann, param->getType(), conv_ctx));
    }
    ABSL_ASSIGN_OR_RETURN(
        ir::Parameter ir_param,
        ConvertParameterToIR(cb_typedef.params[i].name, param->getType(),
                             CallbackParamSource{param, param_ann}, conv_ctx));
    invoker.parameters.push_back(std::move(ir_param));
  }
  return InvokerConversion{.invoker = std::move(invoker)};
}

}  // namespace sapi
