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

#include "sandboxed_api/tools/clang_generator/pointer_arg.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/functional/overload.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/strings/substitute.h"
#include "clang/AST/Type.h"
#include "sandboxed_api/tools/clang_generator/annotations.h"
#include "sandboxed_api/tools/clang_generator/arg.h"
#include "sandboxed_api/tools/clang_generator/ast_utils.h"

namespace sapi {

PointeeTypeInfo::PointeeTypeInfo(clang::QualType type)
    : type_class_(ClassifyPointeeType(type)),
      pointee_type_name_(type->getPointeeType().getAsString()),
      unqualified_pointee_type_name_(
          type->getPointeeType().getUnqualifiedType().getAsString()) {}

PointeeTypeInfo::TypeClass PointeeTypeInfo::ClassifyPointeeType(
    clang::QualType type) {
  if (type->getPointeeType()->isArithmeticType()) {
    return TypeClass::kArithmetic;
  } else if (type->getPointeeType()->isRecordType()) {
    return TypeClass::kRecord;
  } else if (type->getPointeeType()->isVoidType()) {
    return TypeClass::kVoid;
  } else if (type->getPointeeType()->isPointerType()) {
    return TypeClass::kPointerType;
  } else {
    return TypeClass::kOther;
  }
}

PointerArg::PointerArg(
    absl::string_view name, absl::string_view type,
    PointeeTypeInfo pointee_type, PointerDir ptr_dir,
    ArraySizedByType sized_by_type, PointerLifetime lifetime,
    ContextBoundAnnotations context_bound, std::vector<StructSync> struct_sync,
    const absl::flat_hash_map<std::string, RecordAnnotations>&
        record_annotations)
    : Arg(name, type),
      ptr_dir_(ptr_dir),
      pointee_type_(pointee_type),
      sized_by_type_(sized_by_type),
      lifetime_(lifetime),
      context_bound_(std::move(context_bound)),
      struct_sync_(std::move(struct_sync)),
      record_annotations_(record_annotations) {}

std::string PointerArg::EmitHostPreCall() const {
  // Declare any SAPI variables we need for the arguments.
  if (ptr_dir_ == PointerDir::kSandboxOpaque) {
    return absl::Substitute("sapi::v::RemotePtr sapi_tmp_$0($0);\n", name_);
  }
  if (ptr_dir_ == PointerDir::kHostOpaque) {
    return absl::Substitute("sapi::v::Reg<$1> sapi_tmp_$0($0);\n", name_,
                            type_);
  }
  // If this is a retained pointer, we need to allocate space for the sandbox
  // copy, and (if necessary) copy in the data.
  if (context_bound_.retain_and_bind.has_value()) {
    return EmitParamRetainPreCall(*context_bound_.retain_and_bind);
  }
  // Otherwise, just use sapi::v::Array, etc, which will allocate space for
  // a sandbox copy. The copy will be automatically freed after the call.
  std::string code;
  switch (pointee_type_.type_class()) {
    case PointeeTypeInfo::TypeClass::kArithmetic: {
      std::string capacity_expr = GetCapacityAsBytesExpr();
      // TODO(cffsmith): Make this nicer, maybe just sapi::Int and then
      // perform a manual copy?
      code = absl::Substitute(
          "sapi::v::Array<char> "
          "sapi_tmp_$0(const_cast<char*>(reinterpret_cast<const char*>($0)), "
          "$1);\n",
          name_, capacity_expr);
      break;
    }
    case PointeeTypeInfo::TypeClass::kRecord: {
      code = std::visit(
          absl::Overload(
              [this](std::monostate) -> std::string {
                std::string ret = absl::Substitute(
                    "sapi::v::Struct<$0> sapi_tmp_$1("
                    "$1 != nullptr ? *$1 : ($0){});\n",
                    pointee_type_.unqualified_pointee_type_name(), name_);
                if (!struct_sync_.empty()) {
                  absl::StrAppend(&ret, EmitStructMemberSyncsPreCall());
                }
                return ret;
              },
              [this](const ElemSizedBy& arg) -> std::string {
                if (!struct_sync_.empty())
                  LOG(FATAL) << "Not expecting ElemSizedBy with struct sync";
                std::string num_elems;
                if (arg.sized_by_outparam_data.has_value()) {
                  num_elems = absl::Substitute(
                      "($0) / sizeof(*$1)",
                      arg.sized_by_outparam_data->capacity_expr, name_);
                } else {
                  num_elems = arg.expr;
                }
                return absl::Substitute(
                    "sapi::v::Array<$0> sapi_tmp_$1($1, $2);\n",
                    pointee_type_.pointee_type_name(), name_, num_elems);
              },
              [this](const ByteSizedBy& arg) -> std::string {
                if (!struct_sync_.empty())
                  LOG(FATAL) << "Not expecting ByteSizedBy with struct sync";
                std::string bytes_size =
                    arg.sized_by_outparam_data.has_value()
                        ? arg.sized_by_outparam_data->capacity_expr
                        : arg.expr;
                return absl::Substitute(
                    "sapi::v::Array<char> "
                    "sapi_tmp_$0(const_cast<char*>(reinterpret_cast<const "
                    "char*>($0)), $1);\n",
                    name_, bytes_size);
              },
              [this](const SizedByBinding& arg) -> std::string {
                if (!struct_sync_.empty())
                  LOG(FATAL) << "Not expecting SizedByBinding with struct sync";
                std::string bytes_size = GetCapacityAsBytesExpr();
                return absl::Substitute(
                    "sapi::v::Array<char> "
                    "sapi_tmp_$0(const_cast<char*>(reinterpret_cast<const "
                    "char*>($0)), $1);\n",
                    name_, bytes_size);
              },
              [](const NullTerminated& arg) -> std::string {
                LOG(FATAL) << "Not expecting null-terminated PointerArg "
                              "(should be CStrArg)";
              }),
          sized_by_type_);
      break;
    }
    case PointeeTypeInfo::TypeClass::kVoid: {
      if (std::holds_alternative<ByteSizedBy>(sized_by_type_) ||
          std::holds_alternative<SizedByBinding>(sized_by_type_)) {
        std::string capacity_expr = GetCapacityAsBytesExpr();
        code = absl::Substitute(
            "sapi::v::Array<char> "
            "sapi_tmp_$0(const_cast<char*>(reinterpret_cast<const "
            "char*>($0)), $1);\n",
            name_, capacity_expr);
        break;
      }
      if (std::holds_alternative<ElemSizedBy>(sized_by_type_))
        LOG(FATAL) << "Cannot determine elem-size for void*. Did you mean "
                      "to use SANDBOX_BYTE_SIZED_BY?";
      LOG(FATAL) << "Unsupported size type for void*";
    }
    case PointeeTypeInfo::TypeClass::kPointerType: {
      if (ptr_dir_ == PointerDir::kOut &&
          context_bound_.copy_from_and_bind.has_value()) {
        code = absl::Substitute("  sapi::v::Reg<$0> sapi_tmp_$1(nullptr);\n",
                                pointee_type_.pointee_type_name(), name_);
        break;
      }
      LOG(FATAL)
          << "Unsupported pointer-to-pointer types (not "
             "null-terminated w/ global lifetime, and not context-bound) "
          << type_ << " for param " << name_;
    }
    case PointeeTypeInfo::TypeClass::kOther: {
      LOG(FATAL) << "Unsupported pointer direction for other pointee types "
                 << type_ << " for param " << name_;
      return "";  // NOT REACHED
    }
  }

  // If this pointer has a size based on dereferencing an output pointer,
  // then we'll later need to manually CopyFromSandbox post call.
  // That means, we won't use PtrAfter and we will use PtrNone.
  // PtrAfter helps allocate the space for a sandbox copy, but we aren't using
  // that, so we'll need to manually allocate:
  if (IsSizeBasedOnDerefOutParam()) {
    absl::StrAppend(
        &code,
        absl::Substitute("if ($0 != nullptr) {\n"
                         "  sandbox->Check(sandbox->Allocate(&sapi_tmp_$0, "
                         "      /*automatic_free=*/true));\n"
                         "}\n",
                         name_));
    if (ptr_dir_ == PointerDir::kInOut) {
      // Additionally, for in-out pointers, we'll need to copy in the initial
      // data (up to size). We allocated up to capacity, to ensure there
      // is enough space for the output later.
      std::string init_size_expr = GetSizeAsBytesExpr();
      absl::StrAppend(
          &code, absl::Substitute(
                     "if ($0 != nullptr) {\n"
                     "  sandbox->Check(sandbox->rpc_channel()->CopyToSandbox(\n"
                     "      "
                     "reinterpret_cast<uintptr_t>(sapi_tmp_$0.GetRemote()),\n"
                     "      absl::MakeSpan(reinterpret_cast<const char*>($0), "
                     "$1)).status());\n"
                     "}\n",
                     name_, init_size_expr));
    }
  }
  return code;
}

std::string PointerArg::EmitHostPostCall() const {
  // Post call, for out and in-out structs we'll have something like:
  //   *p = *sapi_tmp_p.data();
  //
  // That overwrites all fields of `*p` with the sandbox copy's data.
  // However, if any struct members are pointers (e.g., `p->buf`), and if
  // (a) the sandbox copy had sandbox ptrs
  // (b) the host copy originally had host ptrs
  // then the assignment would overwrite the host ptrs with sandbox ptrs.
  //
  // To maintain the host pointers, for now, we assume that the function does
  // not modify the pointers themselves, only what they point to. Then, we
  // save the host pointers before the call, and restore them after the call.
  //
  // If we want to handle modification of the pointers themselves, that would
  // need more design to mirror what is happening in the sandbox on the host
  // (are they being reallocated, swapped with some other visible pointer,
  //  set to nullptr, set to the address of a global variable, etc.?)
  std::string save_host_ptrs;
  std::string restore_host_ptrs;
  bool should_save_host_ptr =
      (ptr_dir_ == PointerDir::kInOut || ptr_dir_ == PointerDir::kOut);
  if (!struct_sync_.empty() && should_save_host_ptr) {
    for (const auto& sync : struct_sync_) {
      // Allow the sandbox to modify sandbox opaque members, and don't
      // restore them.
      if (sync.ptr_dir == PointerDir::kSandboxOpaque) continue;
      std::optional<std::string> member_name =
          ast::MemberNameOfAccessPath(sync.access_path);
      if (!member_name.has_value()) {
        LOG(FATAL) << "Failed to get member name for " << sync.access_path;
      }
      std::string suffix = absl::StrCat(name_, "_", *member_name);
      absl::SubstituteAndAppend(&save_host_ptrs,
                                "const auto sapi_host_ptr_$0 = $1;\n", suffix,
                                sync.access_path);
      // Also allow the sandbox to set to nullptr.
      absl::SubstituteAndAppend(
          &restore_host_ptrs,
          "if ($0 != nullptr) {\n$0 = sapi_host_ptr_$1;\n}\n", sync.access_path,
          suffix);
    }
  }

  // Manually sync back from sandbox to host if needed.
  std::string copy_back;
  if (context_bound_.retain_and_bind.has_value()) {
    absl::StrAppend(&copy_back,
                    EmitParamRetainPostCall(*context_bound_.retain_and_bind));
  } else if (ptr_dir_ == PointerDir::kOut || ptr_dir_ == PointerDir::kInOut) {
    // In the Array case, the PtrAfter or PtrBoth synchronization writes to
    // $0 directly. However, in the non-Array case, we use a separate temp
    // sapi::v::Struct. The PtrAfter/PtrBoth only synchronizes to that temp
    // Struct, and we need to manually copy back from there to $0.
    if (pointee_type_.type_class() == PointeeTypeInfo::TypeClass::kRecord &&
        std::holds_alternative<std::monostate>(sized_by_type_)) {
      absl::StrAppend(&copy_back,
                      absl::Substitute("if ($0 != nullptr) {\n"
                                       "  *$0 = sapi_tmp_$0.data();\n"
                                       "}\n",
                                       name_));
    } else if (IsSizeBasedOnDerefOutParam()) {
      // If this an Array case where the size is based on dereferencing an
      // outparam, we'll need to manually copy. By this point, the outparam
      // should have the post-call value copied back to the host, so we can
      // use that value for the amount to copy.
      std::string size_expr = GetSizeAsBytesExpr();
      std::string cap_expr = GetCapacityAsBytesExpr();
      absl::StrAppend(
          &copy_back,
          absl::Substitute(
              "if ($0 != nullptr) {\n"
              "  size_t final_size = $1;\n"
              "  size_t cap_size = $2;\n"
              "  if (final_size > cap_size) {\n"
              "    sandbox->Check(absl::OutOfRangeError("
              "absl::StrCat(\"Sandboxee returned size (\", final_size, "
              "\") exceeding capacity (\", cap_size, \") for param $0\")));\n"
              "  }\n"
              "  sandbox->Check(sandbox->rpc_channel()->CopyFromSandbox(\n"
              "      reinterpret_cast<uintptr_t>(sapi_tmp_$0.GetRemote()),\n"
              "      absl::MakeSpan(reinterpret_cast<char*>($0), "
              "final_size)).status());\n"
              "}\n",
              name_, size_expr, cap_expr));
    } else if (ptr_dir_ == PointerDir::kOut &&
               context_bound_.copy_from_and_bind.has_value()) {
      absl::StrAppend(&copy_back, EmitCopyFromAndBindOutPtrCode(
                                      *context_bound_.copy_from_and_bind,
                                      absl::StrCat("sapi_tmp_", name_),
                                      pointee_type_.pointee_type_name()));
      absl::StrAppend(&copy_back,
                      absl::Substitute("  if ($0 != nullptr) {\n"
                                       "    *$0 = sapi_tmp_$0.GetValue();\n"
                                       "  }\n",
                                       name_));
    }
  }

  std::string out;
  if (!struct_sync_.empty()) {
    // After a possible *0 = sapi_tmp_0.data() copy, we need to restore any
    // host pointers that were switched to sandbox pointers.
    if (should_save_host_ptr) {
      absl::SubstituteAndAppend(
          &out,
          "if ($0 != nullptr) {\n"
          "  // Save original host pointers\n"
          "  $1"
          "  // Sync post-call sandbox data to the struct\n"
          "  $2"
          "  // Restore original host pointers\n"
          "  $3"
          "}\n",
          name_, save_host_ptrs, copy_back, restore_host_ptrs);
    }
    // Sync the pointed-to data and retain+bind or free any copies.
    absl::StrAppend(&out, EmitStructMemberSyncsPostCall());
  } else {
    absl::StrAppend(&out, copy_back);
  }

  // Clear any bindings and free memory, if needed.
  if (context_bound_.clear_bindings) {
    absl::StrAppend(
        &out,
        absl::Substitute("sapi_internal_clear_context_bindings(sandbox, $0);\n",
                         name_));
  }
  return out;
}

std::string PointerArg::EmitHostArgs() const {
  std::string arg;
  if (context_bound_.retain_and_bind.has_value()) {
    arg = absl::Substitute("&sapi_tmp_$0", name_);
  } else {
    switch (ptr_dir_) {
      case PointerDir::kIn:
        arg = absl::Substitute("sapi_tmp_$0.PtrBefore()", name_);
        break;
      case PointerDir::kOut:
        if (IsSizeBasedOnDerefOutParam()) {
          // We manually copy in EmitHostPostCall, so don't use PtrAfter().
          arg = absl::Substitute("sapi_tmp_$0.PtrNone()", name_);
        } else {
          arg = absl::Substitute("sapi_tmp_$0.PtrAfter()", name_);
        }
        break;
      case PointerDir::kInOut:
        if (IsSizeBasedOnDerefOutParam()) {
          // We manually allocated and copied in EmitHostPreCall, and we'll
          // manually copy in EmitHostPostCall.
          arg = absl::Substitute("sapi_tmp_$0.PtrNone()", name_);
        } else {
          arg = absl::Substitute("sapi_tmp_$0.PtrBoth()", name_);
        }
        break;
      case PointerDir::kSandboxOpaque:
        // Don't do anything here, we just transparently pass this through
        // It behaves as an in ptr here.
        arg = absl::Substitute("&sapi_tmp_$0", name_);
        break;
      case PointerDir::kHostOpaque:
        // Don't do anything here, we just transparently pass this through
        arg = absl::Substitute("sapi_tmp_$0.PtrNone()", name_);
        break;
      default:
        LOG(FATAL) << "Unsupported pointer direction";
    }
  }
  return absl::Substitute("$0 == nullptr ? nullptr : $1", name_, arg);
}

std::string PointerArg::EmitRetArgs() const {
  // Return pointers can only be kOut or kSandboxOpaque.
  // Either the returned pointer is output data (kOut) that has a type in the
  // host which it can modify, or it is some opaque handle (kSandboxOpaque)
  // that makes sense only in the sandbox.
  if (ptr_dir_ == PointerDir::kOut) {
    return "sapi_ret_arg.PtrAfter()";
  } else if (ptr_dir_ == PointerDir::kSandboxOpaque) {
    // Since this is a pointer even in the sandbox, and we have a pointer to
    // *that* we still need to synchronize it to actually get the pointer that
    // lives in the sandbox.
    return "sapi_ret_arg.PtrAfter()";
  } else {
    LOG(FATAL) << "Unsupported return pointer direction";
  }
}

std::string PointerArg::EmitRetParams() const {
  // We add another indirection here.
  return absl::Substitute("$0* $1", type_, name_);
}

std::string PointerArg::EmitSandboxeeParams() const {
  return absl::Substitute("$0 $1", type_, name_);
}

std::string PointerArg::EmitSandboxeeArgs() const {
  return absl::Substitute("$0", name_);
}

std::string PointerArg::EmitRetPreCall() const {
  return absl::Substitute("sapi::v::Reg<$0> sapi_ret_arg;\n", type_);
}

// In the sandboxee, the name is always sapi_ret_val for the return value.
std::string PointerArg::EmitSandboxeeRet() const {
  return absl::Substitute("*$0 = sapi_ret_val;\n", name_);
}

std::string PointerArg::EmitHostRet() const {
  if (std::holds_alternative<AliasHostPtrLifetime>(lifetime_)) {
    return absl::Substitute(
        "return sapi_ret_arg.GetValue() ? $0 : nullptr;",
        std::get<AliasHostPtrLifetime>(lifetime_).param_name);
  }
  if (std::holds_alternative<AliasCallbackReturnLifetime>(lifetime_)) {
    return absl::Substitute(
        R"cc(return sapi_ret_arg.GetValue()
                        ? static_cast<$0>(
                              sapi_alias_cb_return_map_$1[reinterpret_cast<
                                  uintptr_t>(sapi_ret_arg.GetValue())])
                        : nullptr;)cc",
        type_,
        std::get<AliasCallbackReturnLifetime>(lifetime_).callback_param_name);
  }
  std::string out;
  if (context_bound_.copy_from_and_bind.has_value()) {
    absl::StrAppend(
        &out, EmitCopyFromAndBindOutPtrCode(*context_bound_.copy_from_and_bind,
                                            "sapi_ret_arg", type_));
  }
  absl::StrAppend(&out, "return sapi_ret_arg.GetValue();");
  return out;
}

bool PointerArg::HasContextBindings() const {
  return context_bound_.clear_bindings ||
         context_bound_.copy_from_and_bind.has_value() ||
         context_bound_.retain_and_bind.has_value();
}

std::string PointerArg::GetCapacityAsBytesExpr() const {
  return std::visit(
      absl::Overload(
          [this](std::monostate) -> std::string {
            return absl::Substitute("sizeof(*$0)", name_);
          },
          [this](const ElemSizedBy& arg) -> std::string {
            if (arg.sized_by_outparam_data.has_value()) {
              // Capacity is always in bytes.
              return arg.sized_by_outparam_data->capacity_expr;
            }
            return absl::Substitute(
                "sandbox->CheckedMultiply(sizeof(*$0), ($1))", name_, arg.expr);
          },
          [](const ByteSizedBy& arg) -> std::string {
            return arg.sized_by_outparam_data.has_value()
                       ? arg.sized_by_outparam_data->capacity_expr
                       : arg.expr;
          },
          [](const SizedByBinding& arg) -> std::string {
            std::string context_var = ResolveContextName(arg.context);
            return CompileBindingExpr(context_var, arg.binding_expr,
                                      /*locked=*/false);
          },
          [](const NullTerminated& arg) -> std::string {
            LOG(FATAL) << "Not expecting null-terminated PointerArg "
                          "(should be CStrArg)";
          }),
      sized_by_type_);
}

std::string PointerArg::GetSizeAsBytesExpr() const {
  return std::visit(
      absl::Overload(
          [this](std::monostate) -> std::string {
            return absl::Substitute("sizeof(*$0)", name_);
          },
          [this](const ElemSizedBy& arg) -> std::string {
            return absl::Substitute(
                "sandbox->CheckedMultiply(sizeof(*$0), ($1))", name_, arg.expr);
          },
          [](const ByteSizedBy& arg) -> std::string { return arg.expr; },
          [](const SizedByBinding& arg) -> std::string {
            std::string context_var = ResolveContextName(arg.context);
            return CompileBindingExpr(context_var, arg.binding_expr,
                                      /*locked=*/false);
          },
          [](const NullTerminated& arg) -> std::string {
            LOG(FATAL) << "Not expecting null-terminated PointerArg "
                          "(should be CStrArg)";
          }),
      sized_by_type_);
}

absl::Status PointerArg::LinkArgsIfNeeded(
    const std::vector<std::unique_ptr<Arg>>& args) {
  bool must_be_sized_by_outparam = std::visit(
      absl::Overload([](std::monostate) { return false; },
                     [](const ElemSizedBy& arg) {
                       return arg.sized_by_outparam_data.has_value();
                     },
                     [](const ByteSizedBy& arg) {
                       return arg.sized_by_outparam_data.has_value();
                     },
                     [](const SizedByBinding& arg) { return false; },
                     [](const NullTerminated& arg) { return false; }),
      sized_by_type_);

  std::string size_expr = std::visit(
      absl::Overload([](std::monostate) { return std::string(); },
                     [](const ElemSizedBy& arg) { return arg.expr; },
                     [](const ByteSizedBy& arg) { return arg.expr; },
                     [](const SizedByBinding& arg) { return std::string(); },
                     [](const NullTerminated& arg) { return std::string(); }),
      sized_by_type_);

  // Check if size_expr is roughly just "*param". We strip surrounding spaces
  // and parentheses, to cover some simple equivalent cases.
  auto StripSpacesAndParens = [](std::string& s) {
    absl::RemoveExtraAsciiWhitespace(&s);
    while (!s.empty() && s.front() == '(' && s.back() == ')') {
      s = s.substr(1, s.size() - 2);
      absl::RemoveExtraAsciiWhitespace(&s);
    }
  };
  StripSpacesAndParens(size_expr);
  std::string param_name = size_expr;
  if (!param_name.empty() && param_name.front() == '*') {
    param_name = param_name.substr(1);
    StripSpacesAndParens(param_name);
  } else {
    // Not just `*param`.
    // - this could be simple cases like "param" or "width * height", in which
    // case we proceed as a non-outparam based size.
    // - TODO(jvoung): reject more complex outparam-based expressions like
    // `**param` or `param->next`.
    return absl::OkStatus();
  }

  // Now, try to find a match for param_name in the args.
  for (const auto& arg : args) {
    if (arg->GetName() != param_name) {
      continue;
    }
    // Found a match!
    // Do a few sanity checks, and check if param is actually an outparam.
    const PointerArg* derefed_param =
        dynamic_cast<const PointerArg*>(arg.get());
    if (!derefed_param) {
      return absl::InternalError(absl::Substitute(
          "Param $0 used in a sized_by expression ($1) is not a pointer.",
          param_name, size_expr));
    }
    is_size_based_on_deref_out_param_ =
        (derefed_param->ptr_dir_ == PointerDir::kOut ||
         derefed_param->ptr_dir_ == PointerDir::kInOut);
    if (must_be_sized_by_outparam && !is_size_based_on_deref_out_param_) {
      return absl::InternalError(
          absl::Substitute("Param $0 used in a sized_by expression ($1) must "
                           "have OUT or INOUT direction.",
                           param_name, size_expr));
    }
    break;
  }
  if (must_be_sized_by_outparam && !is_size_based_on_deref_out_param_) {
    return absl::InternalError(absl::Substitute(
        "Param $0 used in a sized_by expression ($1) not found in args.",
        param_name, size_expr));
  }
  return absl::OkStatus();
}

std::string PointerArg::EmitCopyFromAndBindOutPtrCode(
    const CopyFromAndBindOutPtr& copy_from_bind, absl::string_view out_ptr_name,
    absl::string_view out_ptr_type) const {
  std::string ctx_name = ResolveContextName(copy_from_bind.context);
  // Code for calculating the size of the data, and for allocating and
  // copying. These will be used the first time we create a binding in our
  // host map. It will run while a lock is held as we look up or insert into
  // the host map.
  std::string size_calc;
  std::string alloc_and_copy_code;
  std::visit(
      absl::Overload(
          [&size_calc, &alloc_and_copy_code](const ByteSizedBy& arg) {
            size_calc =
                absl::Substitute("size_t sapi_binding_size = $0;\n", arg.expr);
            alloc_and_copy_code = R"cc(
              void* sapi_host_copy = calloc(sapi_binding_size, sizeof(char));
              sandbox->Check(sapi_internal_sync_from_sandbox_to_host(
                  sandbox, sapi_sb_ptr,
                  reinterpret_cast<uintptr_t>(sapi_host_copy),
                  sapi_binding_size));
            )cc";
          },
          [this, &size_calc, &alloc_and_copy_code](const ElemSizedBy& arg) {
            size_calc = absl::Substitute(
                "size_t sapi_binding_size = "
                "sandbox->CheckedMultiply(sizeof(*$0), ($1));\n",
                name_, arg.expr);
            alloc_and_copy_code = R"cc(
              void* sapi_host_copy = calloc(sapi_binding_size, sizeof(char));
              sandbox->Check(sapi_internal_sync_from_sandbox_to_host(
                  sandbox, sapi_sb_ptr,
                  reinterpret_cast<uintptr_t>(sapi_host_copy),
                  sapi_binding_size));
            )cc";
          },
          [&size_calc, &alloc_and_copy_code](const NullTerminated& arg) {
            size_calc = "";  // part of alloc_and_copy_code instead
            alloc_and_copy_code = R"cc(
              absl::StatusOr<std::string> sapi_remote_str =
                  sandbox->GetCString(sapi::v::RemotePtr(
                      reinterpret_cast<const void*>(sapi_sb_ptr)));
              sandbox->Check(sapi_remote_str.status());
              size_t sapi_binding_size = sapi_remote_str->size();
              void* sapi_host_copy = calloc(sapi_binding_size + 1, sizeof(char));
              memcpy(sapi_host_copy, sapi_remote_str->data(), sapi_binding_size);
            )cc";
          },
          [&size_calc, &alloc_and_copy_code](const SizedByBinding& arg) {
            std::string context_var = ResolveContextName(arg.context);
            std::string size_expr = CompileBindingExpr(
                context_var, arg.binding_expr, /*locked=*/true);
            size_calc =
                absl::Substitute("size_t sapi_binding_size = $0;\n", size_expr);
            alloc_and_copy_code = R"cc(
              void* sapi_host_copy = calloc(sapi_binding_size, sizeof(char));
              sandbox->Check(sapi_internal_sync_from_sandbox_to_host(
                  sandbox, sapi_sb_ptr,
                  reinterpret_cast<uintptr_t>(sapi_host_copy),
                  sapi_binding_size));
            )cc";
          },
          [](std::monostate) {
            LOG(FATAL) << "Expected a sized context-bound pointer.";
          }),
      sized_by_type_);

  // On the first call, make a host copy and bind. Otherwise, sync if already
  // bound. TODO(b/491828958): to start, we abort if the sapi_sb_ptr changed
  // vs a previous binding. Should we allow overwriting?
  return absl::Substitute(
      R"cc({  // Get or create context binding.
             auto* sapi_returned_sb_ptr = $2.GetValue();
             if (sapi_returned_sb_ptr != nullptr && $0 != nullptr) {
               absl::MutexLock sapi_lock(sapi_internal_context_binding_mutex);
               auto sapi_it = sapi_internal_context_binding_map.find(
                   // {ctx, binding}
                   {$0, "$1"});
               if (sapi_it == sapi_internal_context_binding_map.end()) {
                 uintptr_t sapi_sb_ptr = reinterpret_cast<uintptr_t>(sapi_returned_sb_ptr);
                 $3
                     // Alloc for host and copy
                     $4
                     // Bind
                     auto [sapi_new_it, sapi_inserted] =
                         sapi_internal_context_binding_map.insert(
                             {{$0, "$1"},
                              std::make_tuple(sapi_sb_ptr, sapi_binding_size,
                                              reinterpret_cast<uintptr_t>(
                                                  sapi_host_copy))});
                 CHECK(sapi_inserted);
                 sapi_it = sapi_new_it;
               } else {
                 auto& sapi_binding_data = sapi_it->second;
                 uintptr_t sapi_sb_ptr = std::get<0>(sapi_binding_data);
                 if (sapi_sb_ptr != reinterpret_cast<uintptr_t>(sapi_returned_sb_ptr)) {
                   // Allow re-binding if the sandbox pointer changed.
                   // First, free the old host copy.
                   free(reinterpret_cast<void*>(std::get<2>(sapi_binding_data)));
                   sapi_sb_ptr = reinterpret_cast<uintptr_t>(sapi_returned_sb_ptr);
                   // Compute size
                   $3
                       // Alloc for host and copy
                       $4
                           // Update binding
                           sapi_binding_data = std::make_tuple(
                               sapi_sb_ptr, sapi_binding_size,
                               reinterpret_cast<uintptr_t>(sapi_host_copy));
                 } else {
                   sandbox->Check(sapi_internal_sync_from_sandbox_to_host(
                       sandbox, sapi_sb_ptr, std::get<2>(sapi_binding_data),
                       std::get<1>(sapi_binding_data)));
                 }
               }
               $2.SetValue(reinterpret_cast<$5>(std::get<2>(sapi_it->second)));
             }
           })cc",
      ctx_name, copy_from_bind.binding_name, out_ptr_name, size_calc,
      alloc_and_copy_code, out_ptr_type);
}

std::string PointerArg::EmitParamRetainPreCall(
    const RetainAndBind& retain_and_bind) const {
  std::string size_calc;
  std::string suffix = name_;
  std::visit(absl::Overload(
                 [&size_calc, &suffix](const ByteSizedBy& arg) {
                   size_calc = absl::Substitute("sapi_binding_size_$0 = $1;\n",
                                                suffix, arg.expr);
                 },
                 [&size_calc, &suffix](const ElemSizedBy& arg) {
                   size_calc = absl::Substitute(
                       "sapi_binding_size_$0 = "
                       "sandbox->CheckedMultiply(sizeof(*$0), ($1));\n",
                       suffix, arg.expr);
                 },
                 [&size_calc, &suffix](const NullTerminated& arg) {
                   size_calc = absl::Substitute(
                       "sapi_binding_size_$0 = "
                       "strlen(reinterpret_cast<const char*>($0)) + 1;\n",
                       suffix);
                 },
                 [&size_calc, &suffix](const SizedByBinding& arg) {
                   std::string context_var = ResolveContextName(arg.context);
                   std::string size_expr = CompileBindingExpr(
                       context_var, arg.binding_expr, /*locked=*/false);
                   size_calc = absl::Substitute("sapi_binding_size_$0 = $1;\n",
                                                suffix, size_expr);
                 },
                 [](std::monostate) {
                   LOG(FATAL) << "Expected a sized context-bound pointer.";
                 }),
             sized_by_type_);

  std::string copy_code;
  if (ptr_dir_ == PointerDir::kIn || ptr_dir_ == PointerDir::kInOut) {
    copy_code = absl::Substitute(
        R"cc(
          sandbox->Check(
              sandbox->rpc_channel()
                  ->CopyToSandbox(
                      reinterpret_cast<uintptr_t>(sapi_sb_copy_$0),
                      absl::MakeSpan(reinterpret_cast<const char*>($0),
                                     sapi_binding_size_$0))
                  .status());
        )cc",
        name_);
  }
  return absl::Substitute(
      R"cc(
        void* sapi_sb_copy_$0 = nullptr;
        size_t sapi_binding_size_$0 = 0;
        if ($0 != nullptr) {
          // Compute size
          $1
              // Allocate
              sandbox->Check(sandbox->rpc_channel()->Allocate(
                  sapi_binding_size_$0, &sapi_sb_copy_$0));
          $2
        }
        sapi::v::RemotePtr sapi_tmp_$0(sapi_sb_copy_$0);
      )cc",
      name_, size_calc, copy_code);
}

std::string PointerArg::EmitParamRetainPostCall(
    const RetainAndBind& retain_and_bind) const {
  std::string out;
  if (ptr_dir_ == PointerDir::kOut || ptr_dir_ == PointerDir::kInOut) {
    // Sync after the call.
    absl::SubstituteAndAppend(
        &out,
        R"cc(
          if (sapi_sb_copy_$0 != nullptr) {
            sandbox->Check(sapi_internal_sync_from_sandbox_to_host(
                sandbox, reinterpret_cast<uintptr_t>(sapi_sb_copy_$0),
                reinterpret_cast<uintptr_t>($0), sapi_binding_size_$0));
          }
        )cc",
        name_);
  }
  // Add binding to the map.
  std::string ctx_name = ResolveContextName(retain_and_bind.context);
  absl::SubstituteAndAppend(
      &out,
      R"cc(
        if (sapi_sb_copy_$0 != nullptr && $1 != nullptr) {
          absl::MutexLock sapi_lock(sapi_internal_context_binding_mutex);
          auto [sapi_new_it, sapi_inserted] =
              sapi_internal_context_retained_binding_map.insert(
                  {{$1, "$2"},
                   std::make_tuple(reinterpret_cast<uintptr_t>($0),
                                   reinterpret_cast<uintptr_t>(sapi_sb_copy_$0),
                                   sapi_binding_size_$0)});
          CHECK(sapi_inserted);
        }
      )cc",
      name_, ctx_name, retain_and_bind.binding_name);
  return out;
}

std::string PointerArg::EmitStructMemberSyncsPreCall() const {
  std::string ret;
  std::string struct_name = GetStructTypeName();
  auto record_annotations_it = record_annotations_.find(struct_name);

  for (const auto& sync : struct_sync_) {
    std::optional<std::string> member_name =
        ast::MemberNameOfAccessPath(sync.access_path);
    if (!member_name.has_value()) {
      LOG(FATAL) << "Failed to get member name for " << sync.access_path;
    }

    ArraySizedByType size_type = std::monostate{};
    if (record_annotations_it != record_annotations_.end()) {
      for (const auto& member :
           record_annotations_it->second.member_annotations) {
        if (member.name == *member_name) {
          size_type = member.size_type;
          break;
        }
      }
    }

    std::string suffix = absl::StrCat(name_, "_", *member_name);
    std::optional<std::string> parent_prefix =
        ast::ParentPrefixOfAccessPath(sync.access_path);
    if (!parent_prefix.has_value()) {
      LOG(FATAL) << "Failed to get parent prefix for " << sync.access_path;
    }
    std::string size_calc;

    std::visit(absl::Overload(
                   [&](const ByteSizedBy& size) {
                     size_calc =
                         absl::Substitute("sapi_member_size_$0 = $1$2;\n",
                                          suffix, *parent_prefix, size.expr);
                   },
                   [&](const ElemSizedBy& size) {
                     size_calc = absl::Substitute(
                         "sapi_member_size_$0 = "
                         "sandbox->CheckedMultiply(sizeof(*($1)), ($2$3));\n",
                         suffix, sync.access_path, *parent_prefix, size.expr);
                   },
                   [&](const NullTerminated& size) {
                     size_calc = absl::Substitute(
                         "sapi_member_size_$0 = strlen(reinterpret_cast<const "
                         "char*>($1)) + 1;\n",
                         suffix, sync.access_path);
                   },
                   [&](const SizedByBinding& size) {
                     std::string context_var = ResolveContextName(size.context);
                     std::string size_expr =
                         CompileBindingExpr(context_var, size.binding_expr,
                                            /*locked=*/false);
                     size_calc = absl::Substitute("sapi_member_size_$0 = $1;\n",
                                                  suffix, size_expr);
                   },
                   [&](std::monostate) {
                     size_calc = absl::Substitute(
                         "sapi_member_size_$0 = sizeof(*($1));\n", suffix,
                         sync.access_path);
                   }),
               size_type);

    std::string copy_code;
    if (sync.ptr_dir == PointerDir::kIn || sync.ptr_dir == PointerDir::kInOut) {
      copy_code = absl::Substitute(
          R"cc(
            sandbox->Check(
                sandbox->rpc_channel()
                    ->CopyToSandbox(
                        reinterpret_cast<uintptr_t>(sapi_sb_copy_$0),
                        absl::MakeSpan(reinterpret_cast<const char*>($1),
                                       sapi_member_size_$0))
                    .status());
          )cc",
          suffix, sync.access_path);
    }

    std::string update_host_to_sb_ptr_code = absl::Substitute(
        R"cc(
          sapi_tmp_$0.mutable_data()->$1 =
              reinterpret_cast<decltype(sapi_tmp_$0.mutable_data()->$1)>(
                  sapi_sb_copy_$2);
        )cc",
        name_, *member_name, suffix);

    absl::SubstituteAndAppend(
        &ret,
        R"cc(
          void* sapi_sb_copy_$0 = nullptr;
          size_t sapi_member_size_$0 = 0;
          if ($1 != nullptr && $2 != nullptr) {
            // Compute size
            $3
                // Allocate
                sandbox->Check(sandbox->rpc_channel()->Allocate(
                    sapi_member_size_$0, &sapi_sb_copy_$0));
            // copy host to sb if needed
            $4
                // update ptr member (host to sb)
                $5
          }
        )cc",
        suffix, name_, sync.access_path, size_calc, copy_code,
        update_host_to_sb_ptr_code);
  }
  return ret;
}

std::string PointerArg::EmitStructMemberSyncsPostCall() const {
  std::string out;
  for (const auto& sync : struct_sync_) {
    std::optional<std::string> member_name =
        ast::MemberNameOfAccessPath(sync.access_path);
    if (!member_name.has_value()) {
      LOG(FATAL) << "Failed to get member name for " << sync.access_path;
    }
    std::string suffix = absl::StrCat(name_, "_", *member_name);
    std::string host_ptr_expr = sync.access_path;

    // Sync pointed to data, based on the size (computed in pre-call)
    if (sync.ptr_dir == PointerDir::kOut ||
        sync.ptr_dir == PointerDir::kInOut) {
      absl::SubstituteAndAppend(
          &out,
          R"cc(
            if (sapi_sb_copy_$0 != nullptr) {
              sandbox->Check(sapi_internal_sync_from_sandbox_to_host(
                  sandbox, reinterpret_cast<uintptr_t>(sapi_sb_copy_$0),
                  reinterpret_cast<uintptr_t>($1), sapi_member_size_$0));
            }
          )cc",
          suffix, host_ptr_expr);
    }

    // Either retain and bind, or free.
    if (sync.context_bound.retain_and_bind.has_value()) {
      std::string ctx_name =
          ResolveContextName(sync.context_bound.retain_and_bind->context);
      absl::SubstituteAndAppend(
          &out,
          R"cc(
            if (sapi_sb_copy_$0 != nullptr && $1 != nullptr) {
              absl::MutexLock sapi_lock(sapi_internal_context_binding_mutex);
              auto [sapi_new_it, sapi_inserted] =
                  sapi_internal_context_retained_binding_map.insert(
                      {{$1, "$2"},
                       std::make_tuple(
                           reinterpret_cast<uintptr_t>($3),
                           reinterpret_cast<uintptr_t>(sapi_sb_copy_$0),
                           sapi_member_size_$0)});
              CHECK(sapi_inserted);
            }
          )cc",
          suffix, ctx_name, sync.context_bound.retain_and_bind->binding_name,
          host_ptr_expr);
    } else {
      absl::SubstituteAndAppend(&out,
                                R"cc(
                                  if (sapi_sb_copy_$0 != nullptr) {
                                    sandbox->Check(sandbox->rpc_channel()->Free(sapi_sb_copy_$0));
                                  }
                                )cc",
                                suffix);
    }
  }
  return out;
}

std::string PointerArg::GetStructTypeName() const {
  std::string struct_name =
      std::string(pointee_type_.unqualified_pointee_type_name());
  absl::string_view struct_name_view = struct_name;
  if (absl::ConsumePrefix(&struct_name_view, "struct ")) {
    return std::string(struct_name_view);
  } else if (absl::ConsumePrefix(&struct_name_view, "class ")) {
    return std::string(struct_name_view);
  }
  return struct_name;
}

}  // namespace sapi
