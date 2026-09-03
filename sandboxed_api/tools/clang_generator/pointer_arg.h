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

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "clang/AST/Type.h"
#include "sandboxed_api/tools/clang_generator/annotations.h"
#include "sandboxed_api/tools/clang_generator/arg.h"

namespace sapi {

// Basic information about a pointer's pointee's type.
class PointeeTypeInfo {
 public:
  enum class TypeClass { kOther, kArithmetic, kRecord, kPointerType, kVoid };

  explicit PointeeTypeInfo(clang::QualType type);

  TypeClass type_class() const { return type_class_; }

  absl::string_view pointee_type_name() const { return pointee_type_name_; }

  absl::string_view unqualified_pointee_type_name() const {
    return unqualified_pointee_type_name_;
  }

 private:
  static TypeClass ClassifyPointeeType(clang::QualType type);

  TypeClass type_class_;
  std::string pointee_type_name_;
  std::string unqualified_pointee_type_name_;
};

// Handles in/out/inout pointers to arithmetic types and structs (singular and
// arrays), with some limitations on structs. Structs can either:
// - be trivially copyable, with no caveats
// - or have pointer-typed members that need to be synced (but how to sync
//   needs to be described with StructSync annotations).
class PointerArg : public Arg {
 public:
  PointerArg(absl::string_view name, absl::string_view type,
             PointeeTypeInfo pointee_type, PointerDir ptr_dir,
             ArraySizedByType sized_by_type, PointerLifetime lifetime,
             ContextBoundAnnotations context_bound,
             std::vector<StructSync> struct_sync,
             const absl::flat_hash_map<std::string, RecordAnnotations>&
                 record_annotations);

  absl::Status LinkArgsIfNeeded(
      const std::vector<std::unique_ptr<Arg>>& args) override;

  std::string EmitHostPreCall() const override;
  std::string EmitHostPostCall() const override;
  std::string EmitHostArgs() const override;
  std::string EmitRetArgs() const override;
  std::string EmitRetParams() const override;
  std::string EmitSandboxeeParams() const override;
  std::string EmitSandboxeeArgs() const override;
  std::string EmitRetPreCall() const override;
  std::string EmitSandboxeeRet() const override;
  std::string EmitHostRet() const override;

  bool HasContextBindings() const;

  PointerDir ptr_dir() const { return ptr_dir_; }

  void SetHostOpaqueHandleForCbAlias(size_t handle) {
    host_opaque_handle_for_cb_alias_ = handle;
  }
  std::optional<size_t> host_opaque_handle_for_cb_alias() const {
    return host_opaque_handle_for_cb_alias_;
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
      const CopyFromAndBindOutPtr& copy_from_bind,
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
  const absl::flat_hash_map<std::string, RecordAnnotations>&
      record_annotations_;

  // Handle used to replace a host opaque pointer when passed to the sandboxee.
  // This is currently only used when the host opaque pointer is passed to a
  // callback (an alias of one of the callback's parameters).
  // Otherwise, left as std::nullopt.
  // If set to 0, then the original host pointer was null.
  std::optional<size_t> host_opaque_handle_for_cb_alias_;
};

}  // namespace sapi

#endif  // SANDBOXED_API_TOOLS_CLANG_GENERATOR_POINTER_ARG_H_
