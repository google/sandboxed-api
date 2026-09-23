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
#include "llvm/Config/llvm-config.h"
#include "sandboxed_api/tools/clang_generator/annotations.h"
#include "sandboxed_api/tools/clang_generator/arg.h"
#include "sandboxed_api/tools/clang_generator/ast_utils.h"
#include "sandboxed_api/tools/clang_generator/callback_arg.h"
#include "sandboxed_api/tools/clang_generator/ir.h"
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

// TODO: Simplify dual out-params once legacy MakeCallbackArg is removed.
absl::Status ExtractCallbackParams(
    const clang::ParmVarDecl& param, std::vector<std::string>& param_names,
    std::vector<std::string>& param_types,
    std::vector<Annotations>* param_annotations = nullptr,
    std::vector<const clang::ParmVarDecl*>* param_decls = nullptr) {
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
  if (param_annotations != nullptr) {
    param_annotations->reserve(ftl.getNumParams());
  }
  if (param_decls != nullptr) {
    param_decls->reserve(ftl.getNumParams());
  }
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
    if (param_annotations != nullptr) {
      param_annotations->push_back(std::move(annotations));
    }
    if (param_decls != nullptr) {
      param_decls->push_back(cb_param);
    }
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
                                             &param_annotations));

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
      },
      ann.lifetime);
}

// Validates annotations against parameter type and semantics.
absl::Status ValidateParamAnnotations(absl::string_view name,
                                      clang::QualType type, Annotations& ann,
                                      bool is_return_value,
                                      bool is_callback_param) {
  // A pointer direction annotation written on a callback parameter describes
  // the callback's *return* pointer, since C syntax offers nowhere else to
  // attach it (see ret_alias_func_pointer in lwbox_callbacks_sandbox.cc). It is
  // therefore only meaningful when the callback returns something; on a
  // void-returning callback it cannot describe anything and is a mistake.
  // Note: GetFunctionProtoType yields a prototype only for function pointers
  // and for the supported functors, so it doubles as the "is a callback" test.
  if (ann.ptr_dir.has_value()) {
    const clang::FunctionProtoType* proto = ast::GetFunctionProtoType(type);
    if (proto != nullptr && proto->getReturnType()->isVoidType()) {
      return absl::InvalidArgumentError(absl::Substitute(
          "callback argument $0 has a pointer direction annotation but "
          "returns void; on a callback such an annotation describes the "
          "return value",
          name));
    }
  }

  // Validate that raw buffer pointers have an explicit direction annotation.
  // Function pointers, pointer-to-pointers and callback parameters (which are
  // defaulted just below) are exempted.
  //
  // Return values are exempted because a direction is never spelled on the
  // returned pointer itself. On a function return it is only implied by the
  // lifetime annotation, which ParseAnnotations turns into kOut (see
  // alias_ptr, alias_callback_return and copy_from_and_bind_out_ptr), and is
  // absent otherwise. On a callback's return pointer it is written on the
  // enclosing callback parameter instead (SANDBOX_OUT_PTR on `cb`), which
  // reaches ConvertParameterToIR as a `CallbackReturnSource`, so `ann` is
  // empty here. Either way, data coming back out of the sandbox has only one
  // direction to flow in.
  //
  // Functors need no exemption: they are record types, so they never satisfy
  // the isPointerType() check below.
  if (type->isPointerType() && !is_return_value && !is_callback_param) {
    bool is_buffer_pointer = !type->getPointeeType()->isPointerType() &&
                             !type->isFunctionPointerType();
    if (is_buffer_pointer && !ann.ptr_dir.has_value()) {
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
    if (!ann.context_bound.copy_from_and_bind.has_value()) {
      if (is_return_value || ann.ptr_dir == PointerDir::kIn) {
        if (!IsSupportedArgRetNullTerminatedType(type)) {
          return absl::InvalidArgumentError(absl::Substitute(
              "$0 $1 is null-terminated but not a const char*",
              !is_return_value ? "pointer argument" : "return pointer", name));
        }
        if (is_return_value &&
            !std::holds_alternative<SandboxGlobalLifetime>(ann.lifetime)) {
          return absl::InvalidArgumentError(
              absl::Substitute("function $0: null_terminated annotation for "
                               "return values requires a lifetime annotation.",
                               name));
        }
      } else if (ann.ptr_dir != PointerDir::kIn) {
        if (std::holds_alternative<SandboxGlobalLifetime>(ann.lifetime)) {
          if (!IsSupportedOutParamNullTerminatedType(type)) {
            return absl::InvalidArgumentError(absl::Substitute(
                "pointer argument $0 with lifetime_sandbox_global must "
                "be a const char**",
                name));
          }
        } else {
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
  const clang::ParmVarDecl* decl;
};
struct FunctionReturnSource {
  const clang::FunctionDecl* decl;
};
struct CallbackReturnSource {
  // Callback return values have no AST Decl of their own; pointer direction
  // and buffer bounds written on the enclosing callback parameter apply to the
  // buffer the callback returns.
  const Annotations& callback_annotations;
};

using ParamSource = std::variant<FunctionParamSource, CallbackParamSource,
                                 FunctionReturnSource, CallbackReturnSource>;

absl::StatusOr<ir::Parameter> ConvertParameterToIR(
    absl::string_view name, clang::QualType type, const ParamSource& source,
    const absl::flat_hash_map<std::string, RecordAnnotations>&
        record_annotations) {
  const bool is_return_value =
      std::holds_alternative<FunctionReturnSource>(source) ||
      std::holds_alternative<CallbackReturnSource>(source);
  const bool is_callback_param =
      std::holds_alternative<CallbackParamSource>(source);

  ABSL_ASSIGN_OR_RETURN(
      Annotations ann,
      std::visit(
          absl::Overload{
              [&](const FunctionParamSource& s) {
                return ParseAnnotations(name, s.decl);
              },
              [&](const CallbackParamSource& s) {
                return ParseAnnotations(name, s.decl);
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
          },
          source));

  ABSL_RETURN_IF_ERROR(ValidateParamAnnotations(
      name, type, ann, is_return_value, is_callback_param));

  ABSL_ASSIGN_OR_RETURN(ir::TypeInfo type_info, QualTypeToTypeInfo(type));

  ir::Parameter ir_param{
      .name = std::string(name),
      .type = std::move(type_info),
      .is_return_value = is_return_value,
  };

  // Note: functors need no exemption here; they are record types, so they
  // never satisfy isPointerType().
  if (type->isPointerType() && !type->getPointeeType()->isPointerType() &&
      !type->isFunctionPointerType() &&
      ann.ptr_dir != PointerDir::kSandboxOpaque &&
      ann.ptr_dir != PointerDir::kHostOpaque &&
      !(ir_param.type.kind == ir::TypeKind::kString ||
        (ir_param.type.is_pointer() && ir_param.type.is_pointee_string()))) {
    if (const auto* fp = std::get_if<FunctionParamSource>(&source)) {
      const clang::ASTContext& context = fp->decl->getASTContext();
      if (!ann.shallow_struct_sync && ann.struct_sync.empty() &&
          !IsDeeplyTriviallyCopyableType(context, type->getPointeeType(),
                                         record_annotations) &&
          !((std::holds_alternative<ByteSizedBy>(ann.size_type) ||
             std::holds_alternative<SizedByBinding>(ann.size_type)) &&
            IsSupportedArgByteSizedByType(type)) &&
          !(std::holds_alternative<NullTerminated>(ann.size_type) &&
            std::holds_alternative<SandboxGlobalLifetime>(ann.lifetime) &&
            ann.ptr_dir != PointerDir::kIn &&
            IsSupportedOutParamNullTerminatedType(type)) &&
          !(ann.context_bound.copy_from_and_bind.has_value() &&
            ann.ptr_dir == PointerDir::kOut &&
            IsSupportedOutParamContextBoundType(context, type,
                                                record_annotations))) {
        return absl::InvalidArgumentError(absl::Substitute(
            "pointer argument $0 has unsupported pointee type", name));
      }
    } else if (is_return_value && !type->getPointeeType()->isArithmeticType() &&
               !std::holds_alternative<AliasHostPtrLifetime>(ann.lifetime) &&
               !std::holds_alternative<AliasCallbackReturnLifetime>(
                   ann.lifetime)) {
      return absl::InvalidArgumentError(absl::Substitute(
          "return pointer $0 has unsupported pointee type", name));
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
    cb.uninitialized = ann.uninitialized;
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
        },
        source);
    if (param != nullptr) {
      std::vector<std::string> cb_names;
      std::vector<std::string> cb_types;
      std::vector<const clang::ParmVarDecl*> cb_decls;
      ABSL_RETURN_IF_ERROR(ExtractCallbackParams(*param, cb_names, cb_types,
                                                 nullptr, &cb_decls));
      cb.callback_parameters.reserve(cb_decls.size());
      for (size_t i = 0; i < cb_decls.size(); ++i) {
        ABSL_ASSIGN_OR_RETURN(
            ir::Parameter cb_param,
            ConvertParameterToIR(cb_names[i], cb_decls[i]->getType(),
                                 CallbackParamSource{cb_decls[i]},
                                 record_annotations));
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
                                 CallbackReturnSource{ann},
                                 record_annotations));
        cb.return_value =
            std::make_shared<ir::Parameter>(std::move(cb_ret_param));
      }
    } else {
      // A callback only has a `ParmVarDecl` when it appears as a function
      // parameter. Without one there is nothing to read the callback's own
      // parameter names and annotations from, so returning a callback (either
      // from a function or from another callback) is unsupported.
      std::string template_name;
      if (ast::GetFunctorUnderlyingFunctionType(type, template_name) !=
          nullptr) {
        return absl::InvalidArgumentError(
            absl::Substitute("return C++ functor $0 is not supported", name));
      }
      return absl::InvalidArgumentError(absl::Substitute(
          "return function pointer $0 is not supported", name));
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

  // Struct synchronized pointers
  if (!ann.struct_sync.empty() || ann.shallow_struct_sync) {
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
        .is_shallow = ann.shallow_struct_sync,
        .context_bound = std::move(ann.context_bound),
    };
    // The pointee's record annotations are the same for every synchronized
    // member, so resolve them once rather than per member. The annotations
    // guarantee a pointer to a record here (see CheckParsedAnnotations).
    const RecordAnnotations* rec_ann = nullptr;
    if (type->isPointerType()) {
      if (const auto* record_decl = type->getPointeeType()->getAsRecordDecl();
          record_decl != nullptr) {
        auto it = record_annotations.find(record_decl->getName());
        if (it != record_annotations.end()) {
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
      ir::StructMemberSync ir_sync{
          .member_name = std::move(*member_name),
          .parent_prefix = std::move(*parent_prefix),
          .direction = sync.ptr_dir,
          .context_bound = sync.context_bound,
      };
      if (rec_ann != nullptr) {
        for (const auto& member_ann : rec_ann->member_annotations) {
          if (member_ann.name == ir_sync.member_name) {
            Annotations member_a;
            member_a.size_type = member_ann.size_type;
            ir_sync.bounds = AnnotationsToBufferBounds(member_a);
            break;
          }
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
      // Nothing requires the enclosing callback parameter to spell a
      // direction, so it may be absent. A buffer the callback hands back
      // travels out of it either way.
      ir_param.payload = ir::BufferParam{
          .direction =
              cr->callback_annotations.ptr_dir.value_or(PointerDir::kOut),
          .bounds = AnnotationsToBufferBounds(cr->callback_annotations),
      };
    } else {
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

absl::StatusOr<sapi::ir::Function> ConvertFunctionToIR(
    const clang::FunctionDecl* func_decl,
    const absl::flat_hash_map<std::string, RecordAnnotations>&
        record_annotations) {
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
  if (!ret_type->isVoidType()) {
    ABSL_ASSIGN_OR_RETURN(ir_func.return_value,
                          ConvertParameterToIR("sapi_ret_arg", ret_type,
                                               FunctionReturnSource{func_decl},
                                               record_annotations));
  }

  ir_func.parameters.reserve(func_decl->getNumParams());
  for (size_t i = 0; i < func_decl->getNumParams(); ++i) {
    const clang::ParmVarDecl* param = func_decl->getParamDecl(i);
    std::string name = param->getNameAsString();
    if (name.empty()) {
      name = absl::StrFormat("sapi_arg%zu", i);
    }
    ABSL_ASSIGN_OR_RETURN(
        ir::Parameter ir_param,
        ConvertParameterToIR(name, param->getType(), FunctionParamSource{param},
                             record_annotations));
    ir_func.parameters.push_back(std::move(ir_param));
  }

  // Note: alias_ptr linking is performed later by ValidateAndLinkLibraryIR,
  // once every function in the library has been converted.
  return ir_func;
}

}  // namespace sapi
