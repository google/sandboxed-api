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
#ifndef SANDBOXED_API_TOOLS_CLANG_GENERATOR_POINTER_ARG_H_
#define SANDBOXED_API_TOOLS_CLANG_GENERATOR_POINTER_ARG_H_

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/functional/overload.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "clang/AST/Type.h"
#include "sandboxed_api/tools/clang_generator/arg.h"
#include "sandboxed_api/tools/clang_generator/ast_utils.h"
#include "sandboxed_api/tools/clang_generator/sandboxed_library_emitter.h"

namespace sapi {

// Basic information about a pointer's pointee's type.
class PointeeTypeInfo {
 public:
  enum class TypeClass { kOther, kArithmetic, kRecord, kPointerType, kVoid };

  explicit PointeeTypeInfo(clang::QualType type)
      : type_class_(ClassifyPointeeType(type)),
        pointee_type_name_(type->getPointeeType().getAsString()),
        unqualified_pointee_type_name_(
            type->getPointeeType().getUnqualifiedType().getAsString()) {}

  TypeClass type_class() const { return type_class_; }

  absl::string_view pointee_type_name() const { return pointee_type_name_; }

  absl::string_view unqualified_pointee_type_name() const {
    return unqualified_pointee_type_name_;
  }

 private:
  static TypeClass ClassifyPointeeType(clang::QualType type) {
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

  const TypeClass type_class_;
  std::string pointee_type_name_;
  std::string unqualified_pointee_type_name_;
};

// Handles in/out/inout pointers to arithmetic types and structs (singular and
// arrays), with some limitations on structs. Structs can either:
// - be trivially copyable, with no caveats
// - or have pointer-typed members that need to be synced (but how to sync
//   needs to be described with StructSync annotations).
class PointerArg : public SandboxedLibraryEmitter::Arg {
 public:
  PointerArg(absl::string_view name, absl::string_view type,
             PointeeTypeInfo pointee_type, PointerDir ptr_dir,
             ArraySizedByType sized_by_type, PointerLifetime lifetime,
             ContextBoundAnnotations context_bound,
             std::vector<StructSync> struct_sync,
             const absl::flat_hash_map<
                 std::string, SandboxedLibraryEmitter::RecordAnnotations>&
                 record_annotations)
      : Arg(name, type),
        ptr_dir_(ptr_dir),
        pointee_type_(pointee_type),
        sized_by_type_(sized_by_type),
        lifetime_(lifetime),
        context_bound_(std::move(context_bound)),
        struct_sync_(std::move(struct_sync)),
        record_annotations_(record_annotations) {}

  absl::Status LinkArgsIfNeeded(
      const std::vector<std::unique_ptr<Arg>>& args) override;

  std::string EmitHostPreCall() const override {
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
                [this](std::monostate) {
                  std::string ret = absl::Substitute(
                      "sapi::v::Struct<$0> sapi_tmp_$1("
                      "$1 != nullptr ? *$1 : ($0){});\n",
                      pointee_type_.unqualified_pointee_type_name(), name_);
                  if (!struct_sync_.empty()) {
                    absl::StrAppend(&ret, EmitStructMemberSyncsPreCall());
                  }
                  return ret;
                },
                [this](const ElemSizedBy& arg) {
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
                [this](const ByteSizedBy& arg) {
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
                [this](const SizedByBinding& arg) {
                  if (!struct_sync_.empty())
                    LOG(FATAL)
                        << "Not expecting SizedByBinding with struct sync";
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
            &code,
            absl::Substitute(
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

  std::string EmitHostPostCall() const override {
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
            MemberNameOfAccessPath(sync.access_path);
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
            "if ($0 != nullptr) {\n$0 = sapi_host_ptr_$1;\n}\n",
            sync.access_path, suffix);
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
          absl::Substitute(
              "sapi_internal_clear_context_bindings(sandbox, $0);\n", name_));
    }
    return out;
  }

  std::string EmitHostArgs() const override {
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

  std::string EmitRetArgs() const override {
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
  std::string EmitRetParams() const override {
    // We add another indirection here.
    return absl::Substitute("$0* $1", type_, name_);
  }
  std::string EmitSandboxeeParams() const override {
    return absl::Substitute("$0 $1", type_, name_);
  }
  std::string EmitSandboxeeArgs() const override {
    return absl::Substitute("$0", name_);
  }

  std::string EmitRetPreCall() const override {
    return absl::Substitute("sapi::v::Reg<$0> sapi_ret_arg;\n", type_);
  }

  // In the sandboxee, the name is always sapi_ret_val for the return value.
  std::string EmitSandboxeeRet() const override {
    return absl::Substitute("*$0 = sapi_ret_val;\n", name_);
  }

  std::string EmitHostRet() const override {
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
          &out, EmitCopyFromAndBindOutPtrCode(
                    *context_bound_.copy_from_and_bind, "sapi_ret_arg", type_));
    }
    absl::StrAppend(&out, "return sapi_ret_arg.GetValue();");
    return out;
  }

  bool HasContextBindings() const {
    return context_bound_.clear_bindings ||
           context_bound_.copy_from_and_bind.has_value() ||
           context_bound_.retain_and_bind.has_value();
  }

 private:
  bool IsSizeBasedOnDerefOutParam() const {
    return is_size_based_on_deref_out_param_;
  }

  // Returns code / an expression that evaluates to the capacity of the pointee
  // in bytes.
  std::string GetCapacityAsBytesExpr() const;

  // Returns code / an expression that evaluates to the size/length of
  // the data in the pointee in bytes. (should be <= capacity).
  std::string GetSizeAsBytesExpr() const;

  // Emits code to copy from the given out-pointer, and bind its lifetime to the
  // context.
  std::string EmitCopyFromAndBindOutPtrCode(
      const CopyFromAndBindOutPtr& copy_from_and_bind_out_ptr,
      absl::string_view out_ptr_name, absl::string_view out_ptr_type) const;

  // Emits code for before a function call, to help retain a parameter buffer.
  std::string EmitParamRetainPreCall(
      const RetainAndBind& retain_and_bind) const;

  // Emits code for after a function call, to help retain a parameter buffer by
  // binding it to the context.
  std::string EmitParamRetainPostCall(
      const RetainAndBind& retain_and_bind) const;

  // Emits code for before a function call, to help allocate and copy in data
  // for pointer-typed struct data members that need to be synced.
  std::string EmitStructMemberSyncsPreCall() const;

  // Emits code for after a function call, to help copy out data from pointer-
  // typed struct data members that need to be synced.
  // Also frees any sandbox copies that are no longer needed, or binds them to
  // a context if they need to be retained.
  std::string EmitStructMemberSyncsPostCall() const;

  // Assuming the PointerArg is a pointer to a struct, this returns the
  // type name of the struct.
  std::string GetStructTypeName() const;

  const PointerDir ptr_dir_;
  const PointeeTypeInfo pointee_type_;
  // If present, this is an array that is sized-by the given type.
  const ArraySizedByType sized_by_type_;
  const PointerLifetime lifetime_;
  // If true, the `sized_by_type_` requires dereferencing another output (or
  // in-out) pointer parameter.
  bool is_size_based_on_deref_out_param_ = false;

  // Information for context-bound inner pointers that will be used by the host.
  ContextBoundAnnotations context_bound_;

  // Information about struct inner pointers that need to be synced.
  const std::vector<StructSync> struct_sync_;

  // Annotations describing invariants of struct members.
  const absl::flat_hash_map<std::string,
                            SandboxedLibraryEmitter::RecordAnnotations>&
      record_annotations_;
};


}  // namespace sapi

#endif  // SANDBOXED_API_TOOLS_CLANG_GENERATOR_POINTER_ARG_H_
