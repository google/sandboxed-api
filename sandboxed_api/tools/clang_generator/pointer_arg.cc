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
#include "sandboxed_api/tools/clang_generator/arg.h"
#include "sandboxed_api/tools/clang_generator/ast_utils.h"
#include "sandboxed_api/tools/clang_generator/sandboxed_library_emitter.h"

namespace sapi {

std::string PointerArg::GetCapacityAsBytesExpr() const {
  return std::visit(
      absl::Overload(
          [this](std::monostate) {
            return absl::Substitute("sizeof(*$0)", name_);
          },
          [this](const ElemSizedBy& arg) {
            if (arg.sized_by_outparam_data.has_value()) {
              // Capacity is always in bytes.
              return arg.sized_by_outparam_data->capacity_expr;
            }
            return absl::Substitute(
                "sandbox->CheckedMultiply(sizeof(*$0), ($1))", name_, arg.expr);
          },
          [](const ByteSizedBy& arg) {
            return arg.sized_by_outparam_data.has_value()
                       ? arg.sized_by_outparam_data->capacity_expr
                       : arg.expr;
          },
          [](const SizedByBinding& arg) {
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
          [this](std::monostate) {
            return absl::Substitute("sizeof(*$0)", name_);
          },
          [this](const ElemSizedBy& arg) {
            return absl::Substitute(
                "sandbox->CheckedMultiply(sizeof(*$0), ($1))", name_, arg.expr);
          },
          [](const ByteSizedBy& arg) { return arg.expr; },
          [](const SizedByBinding& arg) {
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
    const CopyFromAndBindOutPtr& copy_from_and_bind_out_ptr,
    absl::string_view out_ptr_name, absl::string_view out_ptr_type) const {
  std::string ctx_name = ResolveContextName(copy_from_and_bind_out_ptr.context);
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
      ctx_name, copy_from_and_bind_out_ptr.binding_name, out_ptr_name,
      size_calc, alloc_and_copy_code, out_ptr_type);
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
        MemberNameOfAccessPath(sync.access_path);
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
        ParentPrefixOfAccessPath(sync.access_path);
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
        MemberNameOfAccessPath(sync.access_path);
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
