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

#ifndef SANDBOXED_API_TOOLS_CLANG_GENERATOR_IR_H_
#define SANDBOXED_API_TOOLS_CLANG_GENERATOR_IR_H_

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "sandboxed_api/tools/clang_generator/annotations.h"

namespace sapi {
namespace ir {

// High-level semantic classification of a C/C++ argument or return type.
enum class TypeKind {
  kVoid,
  kScalar,
  // Any C++ standard string type (std::string, std::string&,
  // std::string_view). The exact spelling is carried by
  // CppStringParam::Form rather than being duplicated here.
  kString,
  kPointer,   // Raw pointer (T*) or array
  kCallback,  // Function pointer or C++ functor
  kStruct,    // Record / struct type
};

// Structured type metadata extracted from Clang AST.
struct TypeInfo {
  TypeKind kind = TypeKind::kVoid;
  std::string canonical_name;  // Full C++ type string (e.g. "const char*")
  bool is_const = false;

  // Recursive pointee metadata for multi-level indirection (e.g., T**).
  // Note: We use std::shared_ptr instead of std::unique_ptr so that TypeInfo
  // remains a copyable aggregate type in C++20, allowing both designated
  // initializers (`{.kind = ...}`) and std::initializer_list vector syntax.
  std::shared_ptr<TypeInfo> pointee = nullptr;

  // Total indirection depth (0 for T, 1 for T*, 2 for T**, etc.)
  size_t indirection_depth() const {
    return pointee != nullptr ? 1 + pointee->indirection_depth() : 0;
  }

  // Returns the root element type at the base of any indirection chain.
  const TypeInfo* base_type() const {
    return pointee != nullptr ? pointee->base_type() : this;
  }

  // Query helpers for the immediate pointee
  std::string pointee_type_name() const {
    return pointee != nullptr ? pointee->canonical_name : "";
  }

  bool is_pointer() const { return kind == TypeKind::kPointer; }

  bool is_pointee_arithmetic() const {
    return pointee != nullptr && pointee->kind == TypeKind::kScalar;
  }

  bool is_pointee_record() const {
    return pointee != nullptr && pointee->kind == TypeKind::kStruct;
  }

  bool is_pointee_pointer() const {
    return pointee != nullptr && pointee->is_pointer();
  }

  bool is_pointee_void() const {
    return pointee != nullptr && pointee->kind == TypeKind::kVoid;
  }

  // True if the pointee is `std::string` specifically. TypeKind::kString also
  // covers `std::string&` and `std::string_view`, whose normalized canonical
  // names differ, so the kind alone is not sufficient to distinguish them.
  bool is_pointee_string() const {
    return pointee != nullptr && pointee->kind == TypeKind::kString &&
           pointee->canonical_name == "std::string";
  }

  // Returns the pointee type name without leading 'const ' qualifier.
  // Note: Clang canonical type names format const-qualified pointees with a
  // leading "const " prefix.
  std::string unqualified_pointee_type_name() const {
    if (pointee == nullptr) return "";
    absl::string_view name = pointee->canonical_name;
    if (absl::StartsWith(name, "const ")) {
      name = name.substr(6);
    }
    return std::string(name);
  }
};

// Structured representation of how a buffer's bounds/size are computed.
// TODO: Consider migrating BufferBounds to a std::variant sum type.
// A tagged struct is currently used because it is simpler to read and consume
// without introducing nested std::visit blocks inside ParameterPayload.
struct BufferBounds {
  enum class Kind {
    kSingleton,       // Unsized pointer / pointer to single element
    kElemCount,       // Sized by number of elements (SANDBOX_ELEM_SIZED_BY)
    kByteCount,       // Sized by number of bytes (SANDBOX_BYTE_SIZED_BY)
    kNullTerminated,  // C-style null-terminated string
                      // (SANDBOX_NULL_TERMINATED)
    kElemSizedByOutparam,  // Element count returned in an outparam
                           // (SANDBOX_ELEM_SIZED_BY_OUTPARAM)
    kByteSizedByOutparam,  // Byte count returned in an outparam
                           // (SANDBOX_BYTE_SIZED_BY_OUTPARAM)
    kSizedByBinding,       // Size stored in runtime context map
                           // (SANDBOX_SIZED_BY_BINDING)
  };

  Kind kind = Kind::kSingleton;
  std::string size_expr;  // C++ expression computing size/count
  std::optional<std::string> capacity_expr;  // Maximum capacity (for outparams)
  std::optional<std::string> binding_name;   // Key name for kSizedByBinding
  std::optional<std::string>
      referenced_sibling_param;  // Sibling parameter identifier (e.g., "len"
                                 // for "len" or "*len") referenced in size_expr
  std::optional<std::string>
      outparam_size_param;  // Sibling outparam parameter name (without leading
                            // '*') holding written size
};

// Structured representation of parameter lifetime policies.
struct LifetimePolicy {
  enum class Kind {
    kScopedCall,           // Memory lives only for the duration of the Call()
    kSandboxGlobal,        // Memory lives indefinitely in sandbox
                           // (SANDBOX_LIFETIME_SANDBOX_GLOBAL)
    kAliasHostPtr,         // Pointer aliases a host pointer
    kAliasCallbackReturn,  // Pointer is aliased by a callback return value
  };

  Kind kind = Kind::kScopedCall;
  std::optional<std::string> aliased_host_param_name;
  std::optional<std::string> aliased_callback_param_name;
};

// Synchronized member field within a struct pointer argument
// (SANDBOX_STRUCT_SYNC).
struct StructMemberSync {
  std::string member_name;    // Terminal field name (e.g., "bar")
  std::string parent_prefix;  // Prefix before terminal (e.g., "foo->")
  PointerDir direction = PointerDir::kIn;
  BufferBounds bounds;
  ContextBoundAnnotations context_bound;

  // Full member access path (e.g., "foo->bar").
  std::string access_path() const {
    return absl::StrCat(parent_prefix, member_name);
  }
};

// SAPI parameter category payloads:

// Void parameter (used for void function return types).
struct VoidParam {};

// Scalar parameter (arithmetic, enum, or record passed by value).
struct ScalarParam {};

// Buffer pointer parameter (contiguous elements or bytes in memory).
// Null-terminated C-strings are buffers whose `bounds.kind` is
// `BufferBounds::Kind::kNullTerminated`.
struct BufferParam {
  PointerDir direction = PointerDir::kIn;
  BufferBounds bounds;
  LifetimePolicy lifetime;
  ContextBoundAnnotations context_bound;
  bool uninitialized = false;
};

// C++ string parameter (std::string, std::string_view, std::string*).
struct CppStringParam {
  enum class Form {
    kValue,      // std::string
    kReference,  // std::string& / const std::string&
    kPointer,    // std::string*
    kView,       // std::string_view
  };
  Form form = Form::kValue;
  PointerDir direction = PointerDir::kIn;
  bool is_const = false;
};

// Opaque pointer handle parameter (kSandboxOpaque or kHostOpaque token).
struct OpaquePointerParam {
  PointerDir direction = PointerDir::kSandboxOpaque;
  LifetimePolicy lifetime;
  ContextBoundAnnotations context_bound;
  std::optional<size_t> host_opaque_handle;
};

// Struct pointer parameter with synchronized member fields.
struct StructSyncParam {
  PointerDir direction = PointerDir::kIn;
  std::vector<StructMemberSync> members;
  bool is_shallow = false;
  ContextBoundAnnotations context_bound;
};

struct Parameter;

// Function pointer or functor callback parameter.
struct CallbackParam {
  std::vector<Parameter> callback_parameters;
  std::shared_ptr<Parameter> return_value;
  std::optional<std::string> functor_template_name;
  bool is_callback_return_aliased = false;
  bool uninitialized = false;
};

// Strongly-typed sum type payload representing all valid parameter kinds.
using ParameterPayload =
    std::variant<VoidParam, ScalarParam, BufferParam, CppStringParam,
                 OpaquePointerParam, StructSyncParam, CallbackParam>;

// Declarative Intermediate Representation of a function parameter or return
// value.
struct Parameter {
  std::string name;
  TypeInfo type;
  bool is_return_value = false;
  ParameterPayload payload;

  // Ergonomic sum-type query and cast helpers:
  template <typename T>
  const T* As() const {
    return std::get_if<T>(&payload);
  }

  template <typename T>
  T* As() {
    return std::get_if<T>(&payload);
  }

  template <typename T>
  bool Is() const {
    return std::holds_alternative<T>(payload);
  }

  // Convenience predicates:
  std::optional<PointerDir> direction() const;
  bool IsInput() const;
  bool IsOutput() const;
  bool IsInOut() const;
  bool IsOpaque() const;
  bool uninitialized() const;
  bool should_clear_bindings() const;
};

// Custom thunk code override attached to a function.
struct ThunkOverride {
  std::string function_name;
  std::string body;
  std::string declaration;
};

// Declarative Intermediate Representation of an entire library function.
struct Function {
  std::string name;
  std::optional<Parameter> return_value;
  std::vector<Parameter> parameters;

  std::optional<ThunkOverride> host_thunk;
  std::optional<ThunkOverride> sandboxee_thunk;

  ContextBoundAnnotations context_bound;

  // Finds a parameter by name (returns nullptr if not found).
  const Parameter* FindParameter(absl::string_view param_name) const;
  Parameter* FindParameter(absl::string_view param_name);
};

// Declarative Intermediate Representation of a complete sandboxed library API.
struct Library {
  std::string name;
  std::vector<Function> functions;
  absl::flat_hash_map<std::string, RecordAnnotations> record_annotations;
  std::vector<std::string> host_state_vars;
  std::optional<std::string> host_code;
  std::optional<std::string> sandboxee_code;

  // Finds a function by name (returns nullptr if not found).
  const Function* FindFunction(absl::string_view func_name) const;
  Function* FindFunction(absl::string_view func_name);
};

// Validates semantic invariants on the IR library and links inter-parameter
// references: verifies that ELEM_SIZED_BY references valid sibling parameters,
// links alias_callback_return parameters, and links callback parameters
// carrying alias_ptr annotations to the enclosing function's host opaque
// parameter.
absl::Status ValidateAndLinkLibraryIR(Library& library);

}  // namespace ir
}  // namespace sapi

#endif  // SANDBOXED_API_TOOLS_CLANG_GENERATOR_IR_H_
