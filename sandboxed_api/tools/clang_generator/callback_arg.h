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

#ifndef SANDBOXED_API_TOOLS_CLANG_GENERATOR_CALLBACK_ARG_H_
#define SANDBOXED_API_TOOLS_CLANG_GENERATOR_CALLBACK_ARG_H_

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "sandboxed_api/tools/clang_generator/annotations.h"
#include "sandboxed_api/tools/clang_generator/arg.h"

namespace sapi {

struct CallbackArg : public Arg {
  CallbackArg(absl::string_view name, absl::string_view type,
              Annotations ret_annotations, std::vector<std::string> param_names,
              std::vector<std::string> param_types,
              std::vector<Annotations> param_annotations, bool is_ret_pointer,
              std::string ret_type_name,
              std::optional<std::string> functor_type_name = std::nullopt)
      : Arg(name, type),
        ret_annotations_(std::move(ret_annotations)),
        param_names_(std::move(param_names)),
        param_types_(std::move(param_types)),
        param_annotations_(std::move(param_annotations)),
        is_ret_pointer_(is_ret_pointer),
        ret_type_name_(std::move(ret_type_name)),
        functor_type_name_(functor_type_name),
        ret_val_is_alias_for_outer_function_return_(false),
        param_host_opaque_handles_(param_names_.size(), std::nullopt) {
    if (param_names_.size() != param_types_.size()) {
      LOG(FATAL) << "Different number of param names vs types for " << name_
                 << " (" << param_names_.size() << " vs " << param_types_.size()
                 << ")";
    }
    if (param_names_.size() != param_annotations_.size()) {
      LOG(FATAL) << "Different number of param names vs annotations for "
                 << name_ << " (" << param_names_.size() << " vs "
                 << param_annotations_.size() << ")";
    }
  }

  // Indicates that the outer function's return value aliases with this
  // callback argument's return value (one of them, if any).
  // We discover this property in a second pass after creating the CallbackArg.
  void SetRetValIsAliasForOuterFunctionReturn() {
    ret_val_is_alias_for_outer_function_return_ = true;
  }

  // Indicates that the callback parameter (identified by `param_idx`) is a
  // host opaque pointer. Right now, this corresponds to when the callback param
  // aliases an outer function's host opaque pointer param. When entering the
  // sandbox (calling the outer function), `handle` is used to replace the host
  // pointer. When coming back to the host (in the callback), we check that the
  // callback parameter is equal to the handle, and substitute it with the host
  // pointer.
  void SetParamHostOpaqueHandle(size_t param_idx, size_t handle) {
    if (param_idx < param_host_opaque_handles_.size()) {
      param_host_opaque_handles_[param_idx] = handle;
    }
  }

  // Accessor for the callback parameter names and annotations.
  // The param vectors should all be the same size.
  const std::vector<std::string>& param_names() const { return param_names_; }
  const std::vector<Annotations>& param_annotations() const {
    return param_annotations_;
  }

  std::vector<std::string> Includes() const override {
    std::vector<std::string> includes = {
        "<optional>",
        absl::Substitute("\"$0sandboxed_api/var_callback.h\"", kIncludePrefix),
    };
    if (functor_type_name_.has_value()) {
      if (*functor_type_name_ == "std::function") {
        includes.push_back("<functional>");
      } else if (*functor_type_name_ == "absl::AnyInvocable") {
        includes.push_back(absl::Substitute(
            "\"$0absl/functional/any_invocable.h\"", kIncludePrefix));
      } else {
        LOG(FATAL) << "Unsupported functor type: " << *functor_type_name_;
      }
    }
    if (NeedsLambdaWrapper()) {
      if (is_ret_pointer_) {
        includes.push_back("<vector>");
        includes.push_back("<memory>");
      }
      bool has_null_term =
          std::holds_alternative<NullTerminated>(ret_annotations_.size_type);
      for (const auto& ann : param_annotations_) {
        if (std::holds_alternative<NullTerminated>(ann.size_type)) {
          has_null_term = true;
        }
      }
      if (has_null_term) {
        includes.push_back("<string>");
        includes.push_back("<cstring>");
      }
    }
    if (ret_val_is_alias_for_outer_function_return_) {
      includes.push_back(absl::Substitute(
          "\"$0absl/container/flat_hash_map.h\"", kIncludePrefix));
    }
    return includes;
  }

  std::string EmitParamAsFunctionPointer() const {
    return absl::StrCat(ret_type_name_, " (*", name_, ")(",
                        absl::StrJoin(param_types_, ", "), ")");
  }

  std::string EmitHostParams() const override {
    if (functor_type_name_.has_value()) {
      // For functors, for the host we can use the original functor type.
      return absl::StrCat(type_, " ", name_);
    }
    return EmitParamAsFunctionPointer();
  }
  std::string EmitSandboxeeParams() const override {
    if (functor_type_name_.has_value()) {
      // For functors, for the sandboxee we use the equivalent function pointer
      // type.
      return EmitParamAsFunctionPointer();
    }
    return EmitHostParams();
  }

  std::string EmitHostPreCall() const override {
    // For very simple callbacks, we don't need to wrap in a lambda.
    if (!NeedsLambdaWrapper()) {
      return absl::Substitute(
          R"cc(std::optional<sapi::v::Callback> sapi_cb_$0;
               if ($0) {
                 sapi_cb_$0.emplace($0);
                 sandbox->Check(sandbox->Allocate(&sapi_cb_$0.value(),
                                                  /*automatic_free=*/true));
               })cc",
          name_);
    }
    // Otherwise, we need to wrap the callback in a lambda.

    // Translate and sync pointer params (sb -> host), if needed.
    std::string lambda_body = LambdaWrapperParamSyncPreCall();

    bool needs_ret_sync = is_ret_pointer_;

    // Do the call, and sync the return value if needed.
    if (needs_ret_sync) {
      absl::SubstituteAndAppend(&lambda_body,
                                R"cc(
                                  // Forward params -> args, and do the callback
                                  $0 sapi_$1_ret_host = $1($2);
                                  // If the callback returned null, no need to
                                  // allocate a sandbox copy, sync, etc.
                                  if (!sapi_$1_ret_host) {
                                    return nullptr;
                                  }
                                  // Otherwise, make a sandbox copy of the
                                  // return value, and return the sandbox copy.
                                  $3
                                )cc",
                                ret_type_name_, name_, LambdaWrapperCallArgs(),
                                SyncAndTrackReturnedValue());
    } else if (ret_type_name_ == "void") {
      absl::SubstituteAndAppend(
          &lambda_body,
          "// Forward params -> args, and do the callback\n"
          "$0($1);\n",
          name_, LambdaWrapperCallArgs());
    } else {
      absl::SubstituteAndAppend(
          &lambda_body,
          "// Forward params -> args, and do the callback\n"
          "return $0($1);\n",
          name_, LambdaWrapperCallArgs());
    }

    return absl::Substitute(
        R"cc(
          std::optional<sapi::v::Callback> sapi_cb_$0;
          // Variables to track callback return values, etc.
          $1
              // If the callback is not null, set the sapi_cb var to a lambda.
              if ($0) {
            auto sapi_lambda_$0 = [&]($2) -> $3 { $4 };
            sapi_cb_$0.emplace(sapi_lambda_$0);
            sandbox->Check(sandbox->Allocate(&sapi_cb_$0.value(),
                                             /*automatic_free=*/true));
          }
        )cc",
        name_, LambdaWrapperRetVariables(), LambdaWrapperParamList(),
        ret_type_name_, lambda_body);
  }

  std::string EmitHostPostCall() const override {
    if (!NeedsLambdaWrapper()) {
      return "";
    }
    // Check if we need to sync back to the host any of the (possibly
    // multiple) callback return values.
    // We currently assume
    // - we only need to sync from sandboxee -> host at the end of the outer
    //   function call and not earlier. I.e., the values do not need to be
    //   observed by callbacks in the middle of the outer function call.
    // - we need to sync all of the callback return values. There might be
    //   cases with a "realloc" callback, which frees the previous return
    //   values and only keeps the last one.
    if (is_ret_pointer_ && (ret_annotations_.ptr_dir == PointerDir::kOut ||
                            ret_annotations_.ptr_dir == PointerDir::kInOut)) {
      return absl::Substitute(
          R"cc(for (auto& var : sapi_cb_ret_vars_$0) {
                 sandbox->Check(sandbox->TransferFromSandboxee(var.get()));
               })cc",
          name_);
    }
    return "";
  }

  std::string EmitHostArgs() const override {
    return absl::Substitute("sapi_cb_$0 ? sapi_cb_$0->PtrBefore() : nullptr",
                            name_);
  }

  std::string EmitSandboxeeArgs() const override {
    return absl::Substitute("$0", name_);
  }

 private:
  // Returns true if the callback returns a pointer or if any of its parameters
  // are pointers. In these cases we must wrap the callback in a host-side
  // lambda wrapper to do translation, syncing, or any tracking.
  bool NeedsLambdaWrapper() const {
    if (is_ret_pointer_) {
      return true;
    }
    for (const auto& ann : param_annotations_) {
      if (!IsScalarParam(ann)) {
        return true;
      }
    }
    return false;
  }

  // Emits code for parts of the lambda wrapper.
  std::string LambdaWrapperRetVariables() const;
  std::string LambdaWrapperParamList() const;
  std::string LambdaWrapperParamSyncPreCall() const;
  std::string LambdaWrapperCallArgs() const;
  std::string SyncAndTrackReturnedValue() const;

  std::string GetReturnSizeAsBytesExpr() const;
  static bool IsScalarParam(const Annotations& param_ann);

  // TODO(b/491762076): We could consider something like ArgPtr subclasses
  // to wrap the param + annotations (or return value) emitter methods.
  // For now, we mostly need to emit host side code unlike `PointerArg`, and
  // the behavior is different (the directions are flipped, etc.).
  const Annotations ret_annotations_;
  const std::vector<std::string> param_names_;
  const std::vector<std::string> param_types_;
  const std::vector<Annotations> param_annotations_;
  const bool is_ret_pointer_;
  const std::string ret_type_name_;
  const std::optional<std::string> functor_type_name_;
  bool ret_val_is_alias_for_outer_function_return_;
  // Handles used substitute for host opaque pointers in a callback.
  // - If not set, then the callback parameter is not expected to be an alias of
  //   an outer function's host opaque pointer.
  // - If 0, then the original host opaque pointer was null.
  std::vector<std::optional<size_t>> param_host_opaque_handles_;
};

}  // namespace sapi

#endif  // SANDBOXED_API_TOOLS_CLANG_GENERATOR_CALLBACK_ARG_H_
