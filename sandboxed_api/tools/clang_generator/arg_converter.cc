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
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Type.h"
#include "clang/AST/TypeLoc.h"
#include "clang/Basic/LLVM.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Config/llvm-config.h"
#include "sandboxed_api/tools/clang_generator/annotations.h"
#include "sandboxed_api/tools/clang_generator/arg.h"
#include "sandboxed_api/tools/clang_generator/ast_utils.h"
#include "sandboxed_api/tools/clang_generator/callback_arg.h"
#include "sandboxed_api/tools/clang_generator/pointer_arg.h"
#include "sandboxed_api/tools/clang_generator/simple_args.h"

namespace sapi {
namespace {

// Returns true if the type is trivially copyable and wouldn't involve complex
// lifetimes or aliasing (e.g., struct with pointer fields that are outputs).
bool IsDeeplyTriviallyCopyableType(
    const clang::ASTContext& context, clang::QualType type,
    const absl::flat_hash_map<std::string, RecordAnnotations>&
        record_annotations) {
  if (type.isNull()) {
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
        context, const_array_type->getElementType(), record_annotations);
  }

  if (auto* record_decl = type->getAsRecordDecl(); record_decl != nullptr) {
    if (!type.isTriviallyCopyableType(context) ||
        record_decl->hasFlexibleArrayMember()) {
      return false;
    }

    const RecordAnnotations* rec_ann = nullptr;
    auto it = record_annotations.find(record_decl->getName());
    if (it != record_annotations.end()) {
      rec_ann = &it->second;
    }

    for (const clang::FieldDecl* field : record_decl->fields()) {
      // If the field is annotated as sandbox_opaque_ptr, we'll treat it as
      // trivially copyable.
      llvm::StringRef field_name = field->getName();
      bool is_opaque = false;
      if (rec_ann != nullptr) {
        for (const auto& member_ann : rec_ann->member_annotations) {
          if (member_ann.name == field_name) {
            is_opaque = (member_ann.ptr_dir == PointerDir::kSandboxOpaque);
            break;
          }
        }
      }
      if (is_opaque) {
        continue;
      }

      if (!IsDeeplyTriviallyCopyableType(context, field->getType(),
                                         record_annotations)) {
        return false;
      }
    }
    return true;
  }
  return false;
}

// Returns true if the type is a supported NullTerminated input or return type.
bool IsSupportedArgRetNullTerminatedType(clang::QualType type) {
  return type->isPointerType() && type->getPointeeType()->isCharType() &&
         type->getPointeeType().isConstQualified();
}

// Returns true if the type is a supported NullTerminated outparam type.
bool IsSupportedOutParamNullTerminatedType(clang::QualType type) {
  return type->isPointerType() && type->getPointeeType()->isPointerType() &&
         type->getPointeeType()->getPointeeType()->isCharType() &&
         type->getPointeeType()->getPointeeType().isConstQualified();
}

// Returns true if the type is a supported context-bound outparam type.
bool IsSupportedOutParamContextBoundType(
    const clang::ASTContext& context, clang::QualType type,
    const absl::flat_hash_map<std::string, RecordAnnotations>&
        record_annotations) {
  if (!type->isPointerType() || !type->getPointeeType()->isPointerType())
    return false;
  auto pointee_pointee_type = type->getPointeeType()->getPointeeType();
  return IsDeeplyTriviallyCopyableType(context, pointee_pointee_type,
                                       record_annotations);
}

bool IsSupportedArgByteSizedByType(clang::QualType type) {
  return type->isPointerType() && type->getPointeeType()->isVoidType();
}

absl::Status CheckCallbackParamAnnotations(absl::string_view cb_name,
                                           absl::string_view cb_param_name,
                                           Annotations& annotations,
                                           clang::QualType cb_param_type) {
  // For now, the callback trampolines only support integral, enumeration,
  // or pointer types as arguments (no floating point / vector types).
  if (cb_param_type->isIntegralOrEnumerationType()) {
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

    // Check supported size annotations. For now, this is different from the
    // non-callback case.
    // TODO(b/491762076): support more cases of pointee types (e.g.,
    // isDeeplyTriviallyCopyableType as well).
    bool is_primitive_pointee =
        cb_param_type->getPointeeType()->isArithmeticType() ||
        cb_param_type->getPointeeType()->isEnumeralType();
    absl::Status error_status = absl::InvalidArgumentError(absl::Substitute(
        "callback $0 pointer argument $1 has unsupported pointee type: $2",
        cb_name, cb_param_name, cb_param_type->getPointeeType().getAsString()));
    return std::visit(
        absl::Overload{
            [&](const std::monostate&) {
              if (!is_primitive_pointee) return error_status;
              return absl::OkStatus();
            },
            [&](const ElemSizedBy& elem_sized_by) {
              if (!is_primitive_pointee) return error_status;
              return absl::OkStatus();
            },
            [&](const ByteSizedBy& byte_sized_by) {
              if (!is_primitive_pointee &&
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
              if (!IsSupportedArgRetNullTerminatedType(cb_param_type))
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

absl::Status ExtractCallbackParams(
    const clang::ParmVarDecl& param, std::vector<std::string>& param_names,
    std::vector<std::string>& param_types,
    std::vector<Annotations>& param_annotations) {
  // Details: the Clang Type does not include the parameter names (more of
  // a canonical type). However, the TypeSourceInfo and TypeLoc does let us
  // retrieve that information.
  auto* type_source_info = param.getTypeSourceInfo();
  if (!type_source_info) {
    return absl::InvalidArgumentError(absl::Substitute(
        "callback $0 does not have type source info", param.getName().str()));
  }
  clang::TypeLoc tl = type_source_info->getTypeLoc();
  while (true) {
    tl = tl.getUnqualifiedLoc();
    if (auto ptr_tl = tl.getAs<clang::PointerTypeLoc>()) {
      tl = ptr_tl.getPointeeLoc();
    } else if (auto ref_tl = tl.getAs<clang::ReferenceTypeLoc>()) {
      tl = ref_tl.getPointeeLoc();
    } else if (auto paren_tl = tl.getAs<clang::ParenTypeLoc>()) {
      tl = paren_tl.getInnerLoc();
    } else if (auto attr_tl = tl.getAs<clang::AttributedTypeLoc>()) {
      tl = attr_tl.getModifiedLoc();
#if LLVM_VERSION_MAJOR < 22
      // ElaboratedType was removed from the AST in LLVM 22.
    } else if (auto elab_tl = tl.getAs<clang::ElaboratedTypeLoc>()) {
      tl = elab_tl.getNamedTypeLoc();
#endif
    } else if (auto spec_tl =
                   tl.getAs<clang::TemplateSpecializationTypeLoc>()) {
      if (spec_tl.getNumArgs() >= 1) {
        clang::TemplateArgumentLoc arg_loc = spec_tl.getArgLoc(0);
        if (arg_loc.getArgument().getKind() == clang::TemplateArgument::Type) {
          if (clang::TypeSourceInfo* arg_tsi = arg_loc.getTypeSourceInfo()) {
            tl = arg_tsi->getTypeLoc();
            continue;
          }
        }
      }
      break;
    } else {
      break;
    }
  }
  auto ftl = tl.getAs<clang::FunctionProtoTypeLoc>();
  if (!ftl) {
    return absl::InvalidArgumentError(
        absl::Substitute("callback $0 does not have a function proto type loc",
                         param.getName().str()));
  }
  param_names.reserve(ftl.getNumParams());
  param_types.reserve(ftl.getNumParams());
  param_annotations.reserve(ftl.getNumParams());
  for (unsigned i = 0; i < ftl.getNumParams(); ++i) {
    clang::ParmVarDecl* cb_param = ftl.getParam(i);
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
    param_types.push_back(cb_param->getType().getCanonicalType().getAsString());
    ABSL_ASSIGN_OR_RETURN(Annotations annotations,
                          ParseAnnotations(param_name, cb_param));
    ABSL_RETURN_IF_ERROR(CheckCallbackParamAnnotations(
        param.getName().str(), param_name, annotations, cb_param->getType()));
    param_annotations.push_back(std::move(annotations));
  }
  return absl::OkStatus();
}

absl::StatusOr<ArgPtr> MakeCallbackArg(
    absl::string_view name, absl::string_view type_name,
    Annotations&& annotations, const clang::ParmVarDecl& param,
    const clang::FunctionProtoType& function_type,
    std::optional<std::string> functor_template_name) {
  // Extract callback parameter names, types, and annotations from a callback
  // function pointer. We need the param names to coordinate with annotations
  // like SANDBOX_ELEM_SIZED_BY(param_name).
  // Otherwise, we also support SANDBOX_ELEM_SIZED_BY(cb_argN) if the
  // function pointer declaration did not name the parameters.
  std::vector<std::string> param_names;
  std::vector<std::string> param_types;
  std::vector<Annotations> param_annotations;
  ABSL_RETURN_IF_ERROR(ExtractCallbackParams(param, param_names, param_types,
                                             param_annotations));

  clang::QualType cb_ret_type =
      function_type.getReturnType().getCanonicalType();
  if (!cb_ret_type->isVoidType() &&
      !cb_ret_type->isIntegralOrEnumerationType() &&
      !cb_ret_type->isPointerType()) {
    return absl::InvalidArgumentError(absl::Substitute(
        "callback $0 has unsupported non-primitive return type: $1", name,
        cb_ret_type.getAsString()));
  }

  return std::make_unique<CallbackArg>(
      name, type_name, std::move(annotations), std::move(param_names),
      std::move(param_types), std::move(param_annotations),
      cb_ret_type->isPointerType(), cb_ret_type.getAsString(),
      functor_template_name);
}

absl::StatusOr<ArgPtr> ConvertArgImpl(
    const clang::ASTContext& context, absl::string_view name,
    clang::QualType type, const clang::ParmVarDecl* param,
    Annotations&& annotations,
    const absl::flat_hash_map<std::string, RecordAnnotations>&
        record_annotations) {
  bool is_param = param != nullptr;
  // We are not interested in typedefs.
  type = type.getCanonicalType();
  std::string type_name = type.getAsString();
  if (type->isArithmeticType()) {
    return std::make_unique<ScalarArg>(name, type_name);
  }
  if (type_name == "std::string" ||
      type_name == "class std::basic_string<char>") {
    return std::make_unique<StringArg>(name, type_name);
  }
  if (type_name == "const std::string &" ||
      type_name == "const class std::basic_string<char> &") {
    return std::make_unique<StringConstRefArg>(name, type_name);
  }
  if (type_name == "std::string &" ||
      type_name == "class std::basic_string<char> &") {
    return std::make_unique<StringRefArg>(name, type_name);
  }
  if (type_name == "std::string *" ||
      type_name == "class std::basic_string<char> *") {
    return std::make_unique<StringPtrArg>(name, type_name);
  }
  if (type_name == "std::string_view" ||
      type_name == "class std::basic_string_view<char>") {
    return std::make_unique<StringViewArg>(name, type_name);
  }

  if (type->isFunctionPointerType()) {
    if (param == nullptr) {
      return absl::InvalidArgumentError(absl::Substitute(
          "return function pointer $0 is not supported", name));
    }
    const auto* function_type =
        type->getPointeeType()->getAs<clang::FunctionProtoType>();
    if (!function_type) {
      return absl::InvalidArgumentError(
          absl::Substitute("callback $0 does not have a prototype", name));
    }
    return MakeCallbackArg(name, type_name, std::move(annotations), *param,
                           *function_type,
                           /*functor_template_name=*/std::nullopt);
  }
  std::string template_name;
  if (const auto* functor_type =
          ast::GetFunctorUnderlyingFunctionType(type, template_name)) {
    if (param == nullptr) {
      return absl::InvalidArgumentError(
          absl::Substitute("return C++ functor $0 is not supported", name));
    }
    return MakeCallbackArg(name, type_name, std::move(annotations), *param,
                           *functor_type, template_name);
  }

  if (type->isPointerType()) {
    // Check whether this pointer even needs syncing or is an opaque handle.
    if (annotations.ptr_dir == PointerDir::kSandboxOpaque ||
        annotations.ptr_dir == PointerDir::kHostOpaque) {
      // Shouldn't be sized by in any way.
      if (!std::holds_alternative<std::monostate>(annotations.size_type)) {
        return absl::InvalidArgumentError(absl::Substitute(
            "pointer argument $0 is opaque and should not be sized (kind $1)",
            name, annotations.size_type.index()));
      }
      // Shouldn't need a lifetime annotation.
      if (!std::holds_alternative<std::monostate>(annotations.lifetime)) {
        return absl::InvalidArgumentError(absl::Substitute(
            "pointer argument $0 is opaque and should not have a lifetime "
            "annotation",
            name));
      }
      return std::make_unique<PointerArg>(
          name, type_name, PointeeTypeInfo(type), *annotations.ptr_dir,
          /*sized_by_type=*/std::monostate{}, /*lifetime=*/std::monostate{},
          std::move(annotations.context_bound),
          std::move(annotations.struct_sync), record_annotations);
    }
    if (is_param) {
      if (!annotations.shallow_struct_sync && annotations.struct_sync.empty() &&
          !IsDeeplyTriviallyCopyableType(context, type->getPointeeType(),
                                         record_annotations) &&
          !((std::holds_alternative<ByteSizedBy>(annotations.size_type) ||
             std::holds_alternative<SizedByBinding>(annotations.size_type)) &&
            IsSupportedArgByteSizedByType(type)) &&
          !(std::holds_alternative<NullTerminated>(annotations.size_type) &&
            std::holds_alternative<SandboxGlobalLifetime>(
                annotations.lifetime) &&
            annotations.ptr_dir != PointerDir::kIn &&
            IsSupportedOutParamNullTerminatedType(type)) &&
          !(annotations.context_bound.copy_from_and_bind.has_value() &&
            annotations.ptr_dir == PointerDir::kOut &&
            IsSupportedOutParamContextBoundType(context, type,
                                                record_annotations))) {
        return absl::InvalidArgumentError(absl::Substitute(
            "pointer argument $0 has unsupported pointee type", name));
      }
    } else if (!type->getPointeeType()->isArithmeticType() &&
               !std::holds_alternative<AliasHostPtrLifetime>(
                   annotations.lifetime) &&
               !std::holds_alternative<AliasCallbackReturnLifetime>(
                   annotations.lifetime)) {
      return absl::InvalidArgumentError(absl::Substitute(
          "return pointer $0 has unsupported pointee type", name));
    }
    // Infer "IN" for const pointers.
    std::optional<PointerDir> ptr_dir;
    if (type->getPointeeType().isConstQualified()) {
      ptr_dir = PointerDir::kIn;
    }
    if (annotations.ptr_dir) {
      ptr_dir = annotations.ptr_dir;
    }
    if (!ptr_dir) {
      return absl::InvalidArgumentError(
          absl::Substitute("pointer argument $0 has unknown direction", name));
    }
    return std::visit(
        absl::Overload{
            [&](const std::monostate&) -> absl::StatusOr<ArgPtr> {
              return std::make_unique<PointerArg>(
                  name, type_name, PointeeTypeInfo(type), *ptr_dir,
                  std::monostate{}, annotations.lifetime,
                  std::move(annotations.context_bound),
                  std::move(annotations.struct_sync), record_annotations);
            },
            [&](const ElemSizedBy& elem_sized_by) -> absl::StatusOr<ArgPtr> {
              return std::make_unique<PointerArg>(
                  name, type_name, PointeeTypeInfo(type), *ptr_dir,
                  elem_sized_by, annotations.lifetime,
                  std::move(annotations.context_bound),
                  std::move(annotations.struct_sync), record_annotations);
            },
            [&](const ByteSizedBy& byte_sized_by) -> absl::StatusOr<ArgPtr> {
              return std::make_unique<PointerArg>(
                  name, type_name, PointeeTypeInfo(type), *ptr_dir,
                  byte_sized_by, annotations.lifetime,
                  std::move(annotations.context_bound),
                  std::move(annotations.struct_sync), record_annotations);
            },
            [&](const SizedByBinding& sized_by_binding)
                -> absl::StatusOr<ArgPtr> {
              return std::make_unique<PointerArg>(
                  name, type_name, PointeeTypeInfo(type), *ptr_dir,
                  sized_by_binding, annotations.lifetime,
                  std::move(annotations.context_bound),
                  std::move(annotations.struct_sync), record_annotations);
            },
            [&](const NullTerminated& null_terminated)
                -> absl::StatusOr<ArgPtr> {
              if (annotations.context_bound.copy_from_and_bind.has_value()) {
                // For context-bound null-terminated outputs, we handle that
                // through PointerArg instead of ConstCStrArg. We could consider
                // merging PointerArg and ConstCStrArg in the future.
                return std::make_unique<PointerArg>(
                    name, type_name, PointeeTypeInfo(type), *ptr_dir,
                    null_terminated, annotations.lifetime,
                    std::move(annotations.context_bound),
                    std::move(annotations.struct_sync), record_annotations);
              }
              // Return values, or input-only null-terminated pointers (char*):
              if (!is_param || ptr_dir == PointerDir::kIn) {
                if (!IsSupportedArgRetNullTerminatedType(type)) {
                  return absl::InvalidArgumentError(absl::Substitute(
                      "$0 $1 is null-terminated but not a const char*",
                      is_param ? "pointer argument" : "return pointer", name));
                }
                if (ptr_dir == PointerDir::kIn ||
                    std::holds_alternative<SandboxGlobalLifetime>(
                        annotations.lifetime)) {
                  return std::make_unique<ConstCStrArg>(
                      name, type_name, *ptr_dir, annotations.lifetime);
                }
                return absl::InvalidArgumentError(absl::Substitute(
                    "function $0: null_terminated annotation for "
                    "return values requires a lifetime annotation.",
                    name));
              }
              // Outparams (char**):
              if (ptr_dir != PointerDir::kIn) {
                if (std::holds_alternative<SandboxGlobalLifetime>(
                        annotations.lifetime)) {
                  if (!IsSupportedOutParamNullTerminatedType(type)) {
                    return absl::InvalidArgumentError(absl::Substitute(
                        "pointer argument $0 with lifetime_sandbox_global must "
                        "be a const char**",
                        name));
                  }
                  return std::make_unique<ConstCStrArg>(
                      name, type_name, *ptr_dir, annotations.lifetime);
                }
                return absl::InvalidArgumentError(absl::Substitute(
                    "pointer argument $0: null_terminated annotation for "
                    "output requires a lifetime annotation.",
                    name));
              }
              return absl::InvalidArgumentError(absl::Substitute(
                  "unsupported null_terminated pointer $0", name));
            }},
        annotations.size_type);
  }
  return nullptr;
}

}  // namespace

absl::StatusOr<ArgPtr> ConvertArg(
    absl::string_view name, clang::QualType type,
    const clang::ParmVarDecl* param, const clang::FunctionDecl* funcDecl,
    const absl::flat_hash_map<std::string, RecordAnnotations>&
        record_annotations) {
  Annotations annotations;
  // Either we got a param or a funcDecl, but not both.
  if (param && funcDecl) {
    // TODO(cffsmith): improve this error message.
    return absl::InvalidArgumentError(absl::Substitute(
        "argument $0: cannot have both param and funcDecl", name));
  }
  if (!param && !funcDecl) {
    return absl::InvalidArgumentError(absl::Substitute(
        "argument $0: must have at least one of param and funcDecl", name));
  }
  const clang::ASTContext& context =
      funcDecl ? funcDecl->getASTContext() : param->getASTContext();
  if (param) {
    ABSL_ASSIGN_OR_RETURN(annotations, ParseAnnotations(name, param));
  }
  if (funcDecl) {
    ABSL_ASSIGN_OR_RETURN(annotations, ParseAnnotations(name, funcDecl));
  }

  if (type->isPointerType() && !type->isFunctionPointerType() &&
      annotations.ptr_dir == std::nullopt) {
    return absl::InvalidArgumentError(
        absl::Substitute("argument $0 with type $1: missing sandbox annotation",
                         name, type.getAsString()));
  }

  ABSL_ASSIGN_OR_RETURN(
      ArgPtr arg, ConvertArgImpl(context, name, type, param,
                                 std::move(annotations), record_annotations));
  if (arg && ((param || funcDecl) || !arg->EmitRetParams().empty())) {
    return std::move(arg);
  }
  if (param) {
    return absl::UnimplementedError(absl::Substitute(
        "arg $0: unsupported type: $1 ($2)", name, type.getAsString(),
        type.getCanonicalType().getAsString()));
  }
  return absl::UnimplementedError(
      absl::Substitute("unsupported return type: $0 ($1)", type.getAsString(),
                       type.getCanonicalType().getAsString()));
}

}  // namespace sapi
