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

#include "sandboxed_api/tools/clang_generator/callback_arg.h"

#include <cstddef>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "absl/functional/overload.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "sandboxed_api/tools/clang_generator/annotations.h"

namespace sapi {


std::string CallbackArg::GetReturnSizeAsBytesExpr() const {
  return std::visit(
      absl::Overload{
          [this](std::monostate) {
            return absl::Substitute("sizeof(*sapi_$0_ret_host)", name_);
          },
          [this](const ElemSizedBy& arg) {
            return absl::Substitute(
                "sandbox->CheckedMultiply(sizeof(*sapi_$0_ret_host), ($1))",
                name_, arg.expr);
          },
          [](const ByteSizedBy& arg) { return arg.expr; },
          [](const SizedByBinding& arg) -> std::string {
            LOG(FATAL)
                << "SizedByBinding not supported for callback return values.";
          },
          [this](const NullTerminated& arg) {
            return absl::Substitute(
                "strlen(reinterpret_cast<const char*>(sapi_$0_ret_host)) + 1",
                name_);
          }},
      ret_annotations_.size_type);
}

bool CallbackArg::IsScalarParam(const Annotations& param_ann) {
  return !param_ann.ptr_dir.has_value() &&
         std::holds_alternative<std::monostate>(param_ann.size_type);
}

std::string CallbackArg::LambdaWrapperRetVariables() const {
  std::string out;
  if (is_ret_pointer_) {
    absl::SubstituteAndAppend(&out,
                              R"cc(std::vector<std::unique_ptr<sapi::v::Var>>
                                       sapi_cb_ret_vars_$0;)cc",
                              name_);
    if (ret_val_is_alias_for_outer_function_return_) {
      absl::SubstituteAndAppend(&out,
                                R"cc(absl::flat_hash_map<uintptr_t, void*>
                                         sapi_alias_cb_return_map_$0;)cc",
                                name_);
    }
  }
  return out;
}

std::string CallbackArg::LambdaWrapperParamList() const {
  std::string out;
  for (size_t i = 0; i < param_names_.size(); ++i) {
    if (i > 0) {
      absl::StrAppend(&out, ", ");
    }
    absl::StrAppend(&out, param_types_[i], " ", param_names_[i]);
  }
  return out;
}

std::string CallbackArg::LambdaWrapperParamSyncPreCall() const {
  std::string out;
  for (size_t i = 0; i < param_names_.size(); ++i) {
    const auto& ann = param_annotations_[i];
    const std::string& param_name = param_names_[i];
    const std::string& param_type = param_types_[i];

    // Scalar types don't need syncing or sandbox -> host pointer conversion.
    // Perhaps wrap in ScalarArg like class.
    if (IsScalarParam(ann)) {
      continue;
    }

    if (ann.ptr_dir == PointerDir::kIn) {
      // Sync the sandbox supplied pointee data to the host, and store a host
      // pointer in `sapi_host_$param_name` before calling the host callback.
      auto SyncAndGetHostPointerForNonCStrCases =
          [&](absl::string_view size_expr) {
            absl::SubstituteAndAppend(&out,
                                      R"cc(
                                        $1 sapi_host_$0 = nullptr;
                                        std::optional<sapi::v::Array<char>> sapi_arr_$0;
                                        if ($0 != nullptr) {
                                          sapi_arr_$0.emplace($2);
                                          sapi_arr_$0->SetRemote(const_cast<void*>(reinterpret_cast<const void*>($0)));
                                          sandbox->Check(sandbox->TransferFromSandboxee(&sapi_arr_$0.value()));
                                          sapi_host_$0 = reinterpret_cast<$1>(sapi_arr_$0->GetData());
                                        }
                                      )cc",
                                      param_name, param_type, size_expr);
          };
      // TODO(b/491762076): consider limiting the size, since that is specified
      // by a callback param (which can be controlled by the sandbox).
      // (similar for returned buffers).
      std::visit(
          absl::Overload{
              [&](std::monostate) {
                SyncAndGetHostPointerForNonCStrCases(
                    absl::Substitute("sizeof(*$0)", param_name));
              },
              [&](const ElemSizedBy& elem_sized_by) {
                SyncAndGetHostPointerForNonCStrCases(absl::Substitute(
                    "sandbox->CheckedMultiply(sizeof(*$0), ($1))", param_name,
                    elem_sized_by.expr));
              },
              [&](const ByteSizedBy& byte_sized_by) {
                SyncAndGetHostPointerForNonCStrCases(byte_sized_by.expr);
              },
              [&](const NullTerminated&) {
                absl::SubstituteAndAppend(
                    &out,
                    R"cc(
                      std::optional<std::string> sapi_str_$0;
                      $1 sapi_host_$0 = nullptr;
                      if ($0 != nullptr) {
                        auto sapi_cstr_status = sandbox->GetCString(
                            sapi::v::RemotePtr(const_cast<char*>(
                                reinterpret_cast<const char*>($0))));
                        sandbox->Check(sapi_cstr_status.status());
                        sapi_str_$0 = std::move(*sapi_cstr_status);
                        sapi_host_$0 = sapi_str_$0->c_str();
                      }
                    )cc",
                    param_name, param_type);
              },
              [](const SizedByBinding&) {
                LOG(FATAL)
                    << "SizedByBinding not supported for callback params.";
              }},
          ann.size_type);
    } else if (ann.ptr_dir.has_value()) {
      // We should have rejected this case this after parsing annotations.
      // TODO(b/491762076): support out direction too.
      LOG(FATAL) << "Unsupported pointer direction for callback param: "
                 << param_name;
    }
  }
  return out;
}

std::string CallbackArg::LambdaWrapperCallArgs() const {
  std::vector<std::string> args;
  args.reserve(param_names_.size());
  for (size_t i = 0; i < param_names_.size(); ++i) {
    // Pass scalar arguments directly.
    if (IsScalarParam(param_annotations_[i])) {
      args.push_back(param_names_[i]);
    } else {
      // Pointer args were translated from sandbox -> host pointers in
      // LambdaWrapperParamSyncPreCall.
      args.push_back(absl::StrCat("sapi_host_", param_names_[i]));
    }
  }
  return absl::StrJoin(args, ", ");
}

std::string CallbackArg::SyncAndTrackReturnedValue() const {
  std::string out;

  // Create the SAPI variable to wrap the returned host pointer:
  absl::SubstituteAndAppend(
      &out,
      R"cc(
        size_t ret_size_bytes = $0;
        auto sapi_ret_var = std::make_unique<sapi::v::Array<char>>(
            reinterpret_cast<char*>(sapi_$1_ret_host), ret_size_bytes);
        sandbox->Check(sandbox->Allocate(sapi_ret_var.get(),
                                         /*automatic_free=*/true));)cc",
      GetReturnSizeAsBytesExpr(), name_);

  // If the returned pointer's pointee is not uninitialized, transfer the data
  // to the sandbox (assuming this is a host callback returning a host pointer).
  if (!ret_annotations_.uninitialized) {
    absl::StrAppend(
        &out,
        "sandbox->Check(sandbox->TransferToSandboxee(sapi_ret_var.get()));\n");
  }
  // If "alias_callback_return" store the remote -> host mapping:
  if (ret_val_is_alias_for_outer_function_return_) {
    absl::SubstituteAndAppend(
        &out,
        R"cc(sapi_alias_cb_return_map_$0[reinterpret_cast<uintptr_t>(
                 sapi_ret_var->GetRemote())] = sapi_$0_ret_host;)cc",
        name_);
  }

  // Extend lifetime of the SAPI ret var to the end of the function outer
  // function call (the scope of the `sapi_cb_ret_vars_` vector).
  absl::SubstituteAndAppend(&out,
                            R"cc(
                              auto ret_remote_ptr = sapi_ret_var->GetRemote();
                              sapi_cb_ret_vars_$0.push_back(std::move(sapi_ret_var));
                              return reinterpret_cast<$1>(ret_remote_ptr);
                            )cc",
                            name_, ret_type_name_);
  return out;
}

}  // namespace sapi
