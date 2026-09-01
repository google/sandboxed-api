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

#ifndef SANDBOXED_API_TOOLS_CLANG_GENERATOR_SIMPLE_ARGS_H_
#define SANDBOXED_API_TOOLS_CLANG_GENERATOR_SIMPLE_ARGS_H_

#include <string>
#include <variant>
#include <vector>

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "sandboxed_api/tools/clang_generator/annotations.h"
#include "sandboxed_api/tools/clang_generator/arg.h"

namespace sapi {

// Simple scalar arguments (ints).
// Nothing to see here, move along.
struct ScalarArg : Arg {
  using Arg::Arg;
  std::string EmitHostArgs() const override { return name_; }
  std::string EmitSandboxeeParams() const override { return EmitHostParams(); }
  std::string EmitSandboxeeArgs() const override { return name_; }
  std::string EmitRetParams() const override {
    return absl::Substitute("$0* $1", type_, name_);
  }
  std::string EmitRetPreCall() const override {
    return absl::Substitute("sapi::v::Reg<$0> $1_tmp;\n", type_, name_);
  }
  std::string EmitRetArgs() const override {
    return absl::Substitute("$0_tmp.PtrAfter()", name_);
  }
  std::string EmitHostRet() const override {
    return absl::Substitute("return $0_tmp.GetValue();\n", name_);
  }
  std::string EmitSandboxeeRet() const override {
    return absl::Substitute("*$0 = sapi_ret_val;\n", name_);
  }
};

// "const std::string&", these are always "input" to the library.
struct StringConstRefArg : Arg {
  using Arg::Arg;
  std::vector<std::string> Includes() const override { return {"<string>"}; }
  std::string EmitHostPreCall() const override {
    return absl::Substitute(
        "sapi::v::Array<const char> sapi_tmp_$0($0.data(), $0.size());\n",
        name_);
  }
  std::string EmitHostArgs() const override {
    return absl::Substitute("sapi_tmp_$0.PtrBefore(), $0.size()", name_);
  }
  std::string EmitSandboxeeParams() const override {
    return absl::Substitute("const char* $0_data, size_t $0_size", name_);
  }
  std::string EmitSandboxeeArgs() const override {
    return absl::Substitute("std::string($0_data, $0_size)", name_);
  }
};

// "std::string", same as "const std::string&", but also can be a return type.
struct StringArg : StringConstRefArg {
  using StringConstRefArg::StringConstRefArg;
  std::vector<std::string> Includes() const override {
    return {
        "<string>",
        absl::Substitute("\"$0sandboxed_api/lenval_core.h\"", kIncludePrefix),
    };
  }
  std::string EmitRetPreCall() const override {
    return "sapi::v::LenVal sapi_ret_tmp(0);\n";
  }
  std::string EmitRetArgs() const override { return "sapi_ret_tmp.PtrAfter()"; }
  std::string EmitRetParams() const override {
    return absl::Substitute("sapi::LenValStruct* $0", name_);
  }
  std::string EmitHostRet() const override {
    return "return "
           "std::string(reinterpret_cast<char*>(sapi_ret_tmp.GetData()), "
           "sapi_ret_tmp.GetDataSize());";
  }
  std::string EmitSandboxeeRet() const override {
    return absl::Substitute(
        "$0->data = strdup(sapi_ret_val.c_str());\n"
        "$0->size = sapi_ret_val.size();\n",
        name_);
  }
};

// "std::string&", input/output argument.
struct StringRefArg : Arg {
  using Arg::Arg;
  std::vector<std::string> Includes() const override {
    return {
        "<string>",
        absl::Substitute("\"$0sandboxed_api/lenval_core.h\"", kIncludePrefix),
    };
  }
  std::string EmitHostPreCall() const override {
    return absl::Substitute(
        "sapi::v::LenVal sapi_tmp_$0($0.data(), $0.size());\n", name_);
  }
  std::string EmitHostPostCall() const override {
    return absl::Substitute(
        "$0.assign(reinterpret_cast<char*>(sapi_tmp_$0.GetData()), "
        "sapi_tmp_$0.GetDataSize());\n",
        name_);
  }
  std::string EmitHostArgs() const override {
    return absl::Substitute("sapi_tmp_$0.PtrBoth()", name_);
  }
  std::string EmitSandboxeeParams() const override {
    return absl::Substitute("sapi::LenValStruct* $0", name_);
  }
  std::string EmitSandboxeeArgs() const override {
    return absl::Substitute("sapi_tmp_$0", name_);
  }
  std::string EmitSandboxeePreCall() const override {
    return absl::Substitute(
        "std::string sapi_tmp_$0(reinterpret_cast<char*>($0->data), "
        "$0->size);\n"
        "free($0->data);\n",
        name_);
  }
  std::string EmitSandboxeePostCall() const override {
    return absl::Substitute(
        "$0->data = strdup(sapi_tmp_$0.c_str());\n"
        "$0->size = sapi_tmp_$0.size();\n",
        name_);
  }
};

// "std::string*", input/output argument.
struct StringPtrArg : Arg {
  using Arg::Arg;
  std::vector<std::string> Includes() const override {
    return {
        "<string>",
        "<memory>",
        absl::Substitute("\"$0sandboxed_api/lenval_core.h\"", kIncludePrefix),
    };
  }
  std::string EmitHostPreCall() const override {
    return absl::Substitute(
        "std::unique_ptr<sapi::v::LenVal> sapi_tmp_$0;\n"
        "if ($0 != nullptr) {\n"
        "  sapi_tmp_$0 = std::make_unique<sapi::v::LenVal>($0->data(), "
        "$0->size());\n"
        "}\n",
        name_);
  }
  std::string EmitHostPostCall() const override {
    return absl::Substitute(
        "if ($0 != nullptr && sapi_tmp_$0) {\n"
        "  $0->assign(reinterpret_cast<char*>(sapi_tmp_$0->GetData()), "
        "sapi_tmp_$0->GetDataSize());\n"
        "}\n",
        name_);
  }
  std::string EmitHostArgs() const override {
    return absl::Substitute("sapi_tmp_$0 ? sapi_tmp_$0->PtrBoth() : nullptr",
                            name_);
  }
  std::string EmitSandboxeeParams() const override {
    return absl::Substitute("sapi::LenValStruct* $0", name_);
  }
  std::string EmitSandboxeeArgs() const override {
    return absl::Substitute("sapi_tmp_ptr_$0", name_);
  }
  std::string EmitSandboxeePreCall() const override {
    return absl::Substitute(
        "std::string* sapi_tmp_ptr_$0 = nullptr;\n"
        "std::string sapi_tmp_$0;\n"
        "if ($0 != nullptr) {\n"
        "  sapi_tmp_$0 = std::string(reinterpret_cast<char*>($0->data), "
        "$0->size);\n"
        "  free($0->data);\n"
        "  sapi_tmp_ptr_$0 = &sapi_tmp_$0;\n"
        "}\n",
        name_);
  }
  std::string EmitSandboxeePostCall() const override {
    return absl::Substitute(
        "if ($0 != nullptr) {\n"
        "  $0->data = strdup(sapi_tmp_$0.c_str());\n"
        "  $0->size = sapi_tmp_$0.size();\n"
        "}\n",
        name_);
  }
};

// "std::string_view", pretty much the same as "const std::string&".
struct StringViewArg : Arg {
  using Arg::Arg;
  std::vector<std::string> Includes() const override {
    return {"<string_view>"};
  }
  std::string EmitHostPreCall() const override {
    return absl::Substitute(
        "sapi::v::Array<char> sapi_tmp_$0(const_cast<char*>($0.data()), "
        "$0.size());\n",
        name_);
  }
  std::string EmitHostArgs() const override {
    return absl::Substitute("sapi_tmp_$0.PtrBefore(), $0.size()", name_);
  }
  std::string EmitSandboxeeParams() const override {
    return absl::Substitute("const char* $0_data, size_t $0_size", name_);
  }
  std::string EmitSandboxeeArgs() const override {
    return absl::Substitute("std::string_view($0_data, $0_size)", name_);
  }
};

// A null-terminated C string.
// Currently supports:
// - inputs, or
// - outputs with a global lifetime (vs malloc/free, etc.).
// TODO(b/491826267): support alias lifetime as well? Right now, we don't
// yet support INOUT CString arguments (b/491826252), so the only time you
// return an alias of a parameter is fairly trivial (e.g. return the input
// unmodified).
struct ConstCStrArg : Arg {
  ConstCStrArg(absl::string_view name, absl::string_view type,
               PointerDir ptr_dir, PointerLifetime lifetime)
      : Arg(name, type), ptr_dir_(ptr_dir), lifetime_(lifetime) {
    if (ptr_dir_ != PointerDir::kIn &&
        !std::holds_alternative<SandboxGlobalLifetime>(lifetime_)) {
      LOG(FATAL) << "ConstCStrArg outparams must have SANDBOX_LIFETIME_GLOBAL";
    }
  }

  std::vector<std::string> Includes() const override {
    if (std::holds_alternative<SandboxGlobalLifetime>(lifetime_)) {
      // TODO(jvoung): Make the hash map and mutex includes are only needed
      // for the host (for HostStateVars), and not the sandboxee.
      return {
          "<string>",
          absl::Substitute("\"$0absl/container/node_hash_map.h\"",
                           kIncludePrefix),
          absl::Substitute("\"$0absl/synchronization/mutex.h\"",
                           kIncludePrefix),
      };
    }
    return {};
  }

  std::vector<std::string> HostStateVars() const override {
    if (std::holds_alternative<SandboxGlobalLifetime>(lifetime_)) {
      return {"absl::Mutex sapi_internal_global_cstr_mutex;",
              "absl::node_hash_map<const void*, std::string> "
              "sapi_internal_global_cstr_map "
              "ABSL_GUARDED_BY(sapi_internal_global_cstr_mutex);"};
    }
    return {};
  }

  std::string EmitHostPreCall() const override {
    if (ptr_dir_ == PointerDir::kIn) {
      return absl::Substitute("  sapi::v::ConstCStr sapi_tmp_$0($0);\n", name_);
    } else if (ptr_dir_ == PointerDir::kOut) {
      // For outputs (char**), we want to get the raw library pointer (char*)
      // from the library:
      return absl::Substitute(
          "  sapi::v::Reg<const char*> sapi_tmp_$0(nullptr);\n", name_);
    } else if (ptr_dir_ == PointerDir::kInOut) {
      return absl::Substitute(
          R"(  std::unique_ptr<sapi::v::ConstCStr> sapi_cstr_$0;
  const char* remote_$0 = nullptr;
  if ($0 != nullptr && *$0 != nullptr) {
    sapi_cstr_$0 = std::make_unique<sapi::v::ConstCStr>(*$0);
    sandbox->Check(sandbox->Allocate(sapi_cstr_$0.get(),
                   /*automatic_free=*/true));
    sandbox->Check(sandbox->TransferToSandboxee(sapi_cstr_$0.get()));
    remote_$0 = reinterpret_cast<const char*>(sapi_cstr_$0->GetRemote());
  }
  sapi::v::Reg<const char*> sapi_tmp_$0(remote_$0);
)",
          name_);
    } else {
      LOG(FATAL) << "ConstCStrArg has unknown ptr_dir_";
    }
  }

  std::string EmitHostArgs() const override {
    if (ptr_dir_ == PointerDir::kIn) {
      return absl::Substitute("sapi_tmp_$0.PtrBefore()", name_);
    } else if (ptr_dir_ == PointerDir::kOut) {
      return absl::Substitute("sapi_tmp_$0.PtrAfter()", name_);
    } else {
      return absl::Substitute("sapi_tmp_$0.PtrBoth()", name_);
    }
  }

  std::string EmitHostPostCall() const override {
    if (ptr_dir_ == PointerDir::kIn) {
      return "";
    }
    // And then, on the host, we will use the raw library pointer
    // (char* set by the library) to copy, or look up an earlier copy.
    // NOTE: If the sandbox violates the "const" and mutates the value, we'll
    // incorrectly re-use an older copy, but at least it will consistently use
    // this stale copy on the host side, rather than have TOCTTOU issues.
    return absl::Substitute(R"(  if ($0 != nullptr) {
    const char* remote_ptr = sapi_tmp_$0.GetValue();
    if (!remote_ptr) {
      *$0 = nullptr;
    } else {
      bool found = false;
      {
        absl::MutexLock sapi_lock(sapi_internal_global_cstr_mutex);
        auto it = sapi_internal_global_cstr_map.find(remote_ptr);
        if (it != sapi_internal_global_cstr_map.end()) {
          *$0 = it->second.c_str();
          found = true;
        }
      }
      if (!found) {
        absl::StatusOr<std::string> remote_str = sandbox->GetCString(
            sapi::v::RemotePtr(remote_ptr));
        sandbox->Check(remote_str.status());
        absl::MutexLock sapi_lock(sapi_internal_global_cstr_mutex);
        auto [it, inserted] = sapi_internal_global_cstr_map.insert(
            {remote_ptr, *std::move(remote_str)});
        *$0 = it->second.c_str();
      }
    }
  }
)",
                            name_);
  }

  std::string EmitSandboxeeParams() const override {
    return absl::Substitute("$0 $1", type_, name_);
  }
  std::string EmitSandboxeeArgs() const override {
    return absl::Substitute("$0", name_);
  }

  std::string EmitRetParams() const override {
    return absl::Substitute("$0* $1", type_, name_);
  }
  std::string EmitRetPreCall() const override {
    return absl::Substitute("sapi::v::Reg<$0> sapi_ret_arg;\n", type_);
  }
  std::string EmitRetArgs() const override { return "sapi_ret_arg.PtrAfter()"; }
  std::string EmitSandboxeeRet() const override {
    return absl::Substitute("*$0 = sapi_ret_val;\n", name_);
  }

  std::string EmitHostRet() const override {
    std::string out;
    absl::StrAppend(
        &out, absl::Substitute(R"(  $0 remote_ptr = sapi_ret_arg.GetValue();
  if (!remote_ptr) return nullptr;
  {
    absl::MutexLock sapi_lock(sapi_internal_global_cstr_mutex);
    auto it = sapi_internal_global_cstr_map.find(remote_ptr);
    if (it != sapi_internal_global_cstr_map.end()) {
      return it->second.c_str();
    }
  }
  absl::StatusOr<std::string> remote_str = sandbox->GetCString(
      sapi::v::RemotePtr(remote_ptr));
  sandbox->Check(remote_str.status());
  absl::MutexLock sapi_lock(sapi_internal_global_cstr_mutex);
  auto [it, inserted] = sapi_internal_global_cstr_map.insert(
      {remote_ptr, *std::move(remote_str)});
  return it->second.c_str();
)",
                               type_));
    return out;
  }

 private:
  const PointerDir ptr_dir_;
  const PointerLifetime lifetime_;
};

}  // namespace sapi

#endif  // SANDBOXED_API_TOOLS_CLANG_GENERATOR_SIMPLE_ARGS_H_
