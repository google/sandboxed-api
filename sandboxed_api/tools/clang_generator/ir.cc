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

#include "sandboxed_api/tools/clang_generator/ir.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>

#include "absl/functional/overload.h"
#include "absl/status/status.h"
#include "absl/status/status_macros.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/strings/substitute.h"
#include "absl/types/span.h"
#include "sandboxed_api/tools/clang_generator/annotations.h"

namespace sapi {
namespace ir {

namespace {

template <typename T, typename... Allowed>
inline constexpr bool IsOneOf = (std::is_same_v<T, Allowed> || ...);

}  // namespace

std::optional<PointerDir> Parameter::direction() const {
  return std::visit(
      absl::Overload{
          [](const CallbackParam&) -> std::optional<PointerDir> {
            return PointerDir::kIn;
          },
          [](const auto& p) -> std::optional<PointerDir> {
            using T = std::decay_t<decltype(p)>;
            if constexpr (requires { p.direction; }) {
              static_assert(IsOneOf<T, BufferParam, CppStringParam,
                                    OpaquePointerParam, StructSyncParam>,
                            "Unhandled parameter payload with direction!");
              return p.direction;
            } else {
              static_assert(IsOneOf<T, VoidParam, ScalarParam>,
                            "Unhandled parameter payload without direction!");
              return std::nullopt;
            }
          },
      },
      payload);
}

bool Parameter::IsInput() const {
  auto dir = direction();
  return dir == PointerDir::kIn || dir == PointerDir::kInOut;
}

bool Parameter::IsOutput() const {
  auto dir = direction();
  return dir == PointerDir::kOut || dir == PointerDir::kInOut;
}

bool Parameter::IsInOut() const { return direction() == PointerDir::kInOut; }

bool Parameter::IsOpaque() const {
  auto dir = direction();
  return dir == PointerDir::kSandboxOpaque || dir == PointerDir::kHostOpaque;
}

bool Parameter::uninitialized() const {
  return std::visit(
      absl::Overload{
          [](const BufferParam& p) { return p.uninitialized; },
          [](const CallbackParam& p) { return p.uninitialized; },
          [](const auto& p) {
            using T = std::decay_t<decltype(p)>;
            static_assert(IsOneOf<T, VoidParam, ScalarParam, CppStringParam,
                                  OpaquePointerParam, StructSyncParam>,
                          "Unhandled parameter payload in uninitialized()!");
            return false;
          },
      },
      payload);
}

bool Parameter::should_clear_bindings() const {
  return std::visit(
      absl::Overload{
          [](const auto& p) {
            using T = std::decay_t<decltype(p)>;
            if constexpr (requires { p.context_bound.clear_bindings; }) {
              static_assert(
                  IsOneOf<T, BufferParam, OpaquePointerParam, StructSyncParam>,
                  "Unhandled parameter payload with context_bound!");
              return p.context_bound.clear_bindings;
            } else {
              static_assert(
                  IsOneOf<T, VoidParam, ScalarParam, CppStringParam,
                          CallbackParam>,
                  "Unhandled parameter payload without context_bound!");
              return false;
            }
          },
      },
      payload);
}

const Parameter* Function::FindParameter(absl::string_view param_name) const {
  for (const auto& param : parameters) {
    if (param.name == param_name) {
      return &param;
    }
  }
  return nullptr;
}

Parameter* Function::FindParameter(absl::string_view param_name) {
  for (auto& param : parameters) {
    if (param.name == param_name) {
      return &param;
    }
  }
  return nullptr;
}

const Function* Library::FindFunction(absl::string_view func_name) const {
  for (const auto& func : functions) {
    if (func.name == func_name) {
      return &func;
    }
  }
  return nullptr;
}

Function* Library::FindFunction(absl::string_view func_name) {
  for (auto& func : functions) {
    if (func.name == func_name) {
      return &func;
    }
  }
  return nullptr;
}

namespace {

// Resolves a parameter reference made by a sizing annotation against the
// parameter list that encloses the annotation. For a top-level parameter that
// is the function's own parameter list; for a parameter of a callback it is
// the callback's own parameter list.
//
// There is deliberately no fallback to the enclosing function's parameters: a
// callback only ever sees the arguments it is passed, so sizing one of its
// buffers by a name it cannot reach is a mistake even though the generated
// trampoline would happen to have that name in scope.
const Parameter* FindInScope(absl::Span<const Parameter> scope,
                             absl::string_view param_name) {
  for (const Parameter& param : scope) {
    if (param.name == param_name) {
      return &param;
    }
  }
  return nullptr;
}

// Returns the single sibling parameter a sizing expression refers to, if it
// refers to exactly one: a direct parameter reference ("len") or a
// dereferenced single-indirection pointer parameter ("*len").
//
// Anything else yields nullopt and is therefore NOT validated against the
// enclosing scope. That deliberately includes compound expressions such as
// "width * height * 4" and multiple indirections such as "**len". Checking
// those would mean extracting every identifier in the expression, which draws
// in names that are not parameters at all (`sizeof(int)`, macro constants such
// as MAX_LEN), so we only check the shape we can check soundly.
std::optional<absl::string_view> SoleReferencedParam(
    absl::string_view size_expr) {
  absl::string_view stripped = absl::StripAsciiWhitespace(
      absl::StripPrefix(absl::StripAsciiWhitespace(size_expr), "*"));
  if (stripped.empty() ||
      !(absl::ascii_isalpha(stripped[0]) || stripped[0] == '_') ||
      !std::all_of(stripped.begin(), stripped.end(),
                   [](char c) { return absl::ascii_isalnum(c) || c == '_'; })) {
    return std::nullopt;
  }
  return stripped;
}

// Checks that a sizing expression which names exactly one sibling parameter
// names one that exists.
absl::Status ValidateSizeExpr(absl::Span<const Parameter> scope,
                              absl::string_view scope_desc,
                              absl::string_view role,
                              absl::string_view size_expr) {
  std::optional<absl::string_view> sibling = SoleReferencedParam(size_expr);
  if (sibling.has_value() && FindInScope(scope, *sibling) == nullptr) {
    return absl::InvalidArgumentError(absl::Substitute(
        "$0 $1 references non-existent sibling parameter $2 in "
        "its sizing expression.",
        scope_desc, role, *sibling));
  }
  return absl::OkStatus();
}

// Checks that the sibling an outparam-sized buffer is sized by exists and is
// actually an output.
absl::Status ValidateOutparamSize(absl::Span<const Parameter> scope,
                                  absl::string_view scope_desc,
                                  absl::string_view role,
                                  absl::string_view out_name,
                                  absl::string_view kind_name) {
  const Parameter* out_param = FindInScope(scope, out_name);
  if (!out_param) {
    return absl::InvalidArgumentError(
        absl::Substitute("$0 $1 references non-existent outparam $2 in $3.",
                         scope_desc, role, out_name, kind_name));
  }
  if (!out_param->IsOutput()) {
    return absl::InvalidArgumentError(absl::Substitute(
        "$0 $1 $2 references $3 which is not an output pointer.", scope_desc,
        role, kind_name, out_name));
  }
  return absl::OkStatus();
}

// `scope_desc` names the enclosing entity for diagnostics, e.g.
// "Function foo" or "callback on_data".
absl::Status ValidateBufferBounds(absl::Span<const Parameter> scope,
                                  absl::string_view scope_desc,
                                  absl::string_view role,
                                  const BufferBounds& buffer_bounds) {
  return std::visit(
      absl::Overload{
          [](const bounds::Singleton&) { return absl::OkStatus(); },
          [](const bounds::NullTerminated&) { return absl::OkStatus(); },
          [&](const bounds::ElemCount& elem) {
            return ValidateSizeExpr(scope, scope_desc, role, elem.size_expr);
          },
          [&](const bounds::ByteCount& byte) {
            return ValidateSizeExpr(scope, scope_desc, role, byte.size_expr);
          },
          [&](const bounds::ElemSizedByOutparam& elem) {
            return ValidateOutparamSize(scope, scope_desc, role,
                                        elem.outparam_name,
                                        "ELEM_SIZED_BY_OUTPARAM");
          },
          [&](const bounds::ByteSizedByOutparam& byte) {
            return ValidateOutparamSize(scope, scope_desc, role,
                                        byte.outparam_name,
                                        "BYTE_SIZED_BY_OUTPARAM");
          },
          // `context_expr` names the runtime context object the size is
          // looked up in, not a sibling parameter, so there is nothing to
          // resolve against the enclosing scope.
          [](const bounds::SizedByBinding&) { return absl::OkStatus(); },
      },
      buffer_bounds);
}

// Validates sizing annotations on the parameters and return value of a
// callback. Sibling references resolve against the callback's own parameter
// list.
absl::Status ValidateCallbackScope(absl::string_view cb_param_name,
                                   const CallbackParam& cb) {
  const std::string scope_desc = absl::StrCat("callback ", cb_param_name);
  for (const Parameter& cb_param : cb.callback_parameters) {
    if (const auto* buf = cb_param.As<BufferParam>()) {
      ABSL_RETURN_IF_ERROR(ValidateBufferBounds(
          cb.callback_parameters, scope_desc,
          absl::StrCat("parameter ", cb_param.name), buf->bounds));
    }
  }
  if (cb.return_value != nullptr) {
    if (const auto* buf = cb.return_value->As<BufferParam>()) {
      ABSL_RETURN_IF_ERROR(ValidateBufferBounds(
          cb.callback_parameters, scope_desc, "return value", buf->bounds));
    }
  }
  return absl::OkStatus();
}

// Resolves the cross-parameter references on `param` against `func`. Rules
// that concern a single parameter in isolation are enforced by the frontend
// when the IR is built (see arg_converter.cc); everything here needs the whole
// parameter list, either to look a sibling up or to write back to it.
absl::Status ValidateAndLinkParameter(Function& func, Parameter& param) {
  std::string role = param.is_return_value
                         ? "return value"
                         : absl::StrCat("parameter ", param.name);

  const LifetimePolicy* lifetime = nullptr;
  if (const auto* buf = param.As<BufferParam>()) {
    lifetime = &buf->lifetime;
    ABSL_RETURN_IF_ERROR(ValidateBufferBounds(
        func.parameters, absl::StrCat("Function ", func.name), role,
        buf->bounds));
  } else if (const auto* opaque = param.As<OpaquePointerParam>()) {
    lifetime = &opaque->lifetime;
  } else if (const auto* cb = param.As<CallbackParam>()) {
    ABSL_RETURN_IF_ERROR(ValidateCallbackScope(param.name, *cb));
  }

  // Check host pointer aliasing on top-level parameters / return values.
  // The frontend sets the kind and the name together, so a missing name means
  // the IR was built by hand and is internally inconsistent.
  if (lifetime != nullptr &&
      lifetime->kind == LifetimePolicy::Kind::kAliasHostPtr) {
    if (!lifetime->aliased_host_param_name.has_value()) {
      return absl::InvalidArgumentError(absl::Substitute(
          "Function $0 $1 is alias_ptr but names no host parameter.", func.name,
          role));
    }
    absl::string_view host_name = *lifetime->aliased_host_param_name;
    const Parameter* host_param = func.FindParameter(host_name);
    if (!host_param || !host_param->type.is_pointer()) {
      return absl::InvalidArgumentError(absl::Substitute(
          "Function $0 $1 alias_ptr references non-existent or "
          "non-pointer parameter $2.",
          func.name, role, host_name));
    }
  }

  // Check callback return aliasing
  if (lifetime != nullptr &&
      lifetime->kind == LifetimePolicy::Kind::kAliasCallbackReturn) {
    if (!lifetime->aliased_callback_param_name.has_value()) {
      return absl::InvalidArgumentError(
          absl::Substitute("Function $0 $1 is alias_callback_return but names "
                           "no callback parameter.",
                           func.name, role));
    }
    absl::string_view cb_name = *lifetime->aliased_callback_param_name;
    Parameter* cb_param = func.FindParameter(cb_name);
    if (!cb_param || !cb_param->Is<CallbackParam>()) {
      return absl::InvalidArgumentError(absl::Substitute(
          "Function $0 $1 alias_callback_return references non-existent or "
          "non-callback parameter $2.",
          func.name, role, cb_name));
    }
    cb_param->As<CallbackParam>()->is_callback_return_aliased = true;
  }

  return absl::OkStatus();
}

// Links callback parameters with alias_ptr annotations to the outer function's
// host opaque parameter.
absl::Status LinkAliasParamToCallbackParam(Function& func) {
  // Start handle number at 1, since 0 is reserved for nullptr.
  size_t next_handle = 1;
  for (auto& param : func.parameters) {
    auto* cb = param.As<CallbackParam>();
    if (!cb) continue;
    for (auto& cb_param : cb->callback_parameters) {
      std::optional<std::string> outer_param_name;
      if (const auto* opaque = cb_param.As<OpaquePointerParam>()) {
        if (opaque->lifetime.kind == LifetimePolicy::Kind::kAliasHostPtr) {
          outer_param_name = opaque->lifetime.aliased_host_param_name;
        }
      } else if (const auto* buf = cb_param.As<BufferParam>()) {
        if (buf->lifetime.kind == LifetimePolicy::Kind::kAliasHostPtr) {
          outer_param_name = buf->lifetime.aliased_host_param_name;
        }
      }
      if (!outer_param_name.has_value()) {
        continue;
      }
      Parameter* outer_param = func.FindParameter(*outer_param_name);
      if (!outer_param) {
        return absl::InvalidArgumentError(absl::Substitute(
            "callback $0 parameter $1: alias_ptr references non-existent "
            "parameter $2",
            param.name, cb_param.name, *outer_param_name));
      }
      if (!outer_param->type.is_pointer()) {
        return absl::InvalidArgumentError(absl::Substitute(
            "callback $0 parameter $1: alias_ptr references non-pointer "
            "parameter $2",
            param.name, cb_param.name, *outer_param_name));
      }
      if (!cb_param.Is<OpaquePointerParam>() ||
          cb_param.As<OpaquePointerParam>()->direction !=
              PointerDir::kHostOpaque) {
        return absl::InvalidArgumentError(absl::Substitute(
            "callback $0 parameter $1: alias_ptr is only supported for host "
            "opaque callback parameters",
            param.name, cb_param.name));
      }
      if (!outer_param->Is<OpaquePointerParam>() ||
          outer_param->As<OpaquePointerParam>()->direction !=
              PointerDir::kHostOpaque) {
        return absl::InvalidArgumentError(absl::Substitute(
            "callback $0 parameter $1: alias_ptr references parameter $2 "
            "which is not a host opaque pointer",
            param.name, cb_param.name, *outer_param_name));
      }
      auto* outer_opaque = outer_param->As<OpaquePointerParam>();
      size_t handle;
      if (outer_opaque->host_opaque_handle.has_value()) {
        handle = *outer_opaque->host_opaque_handle;
      } else {
        handle = next_handle++;
        outer_opaque->host_opaque_handle = handle;
      }
      cb_param.As<OpaquePointerParam>()->host_opaque_handle = handle;
    }
  }
  return absl::OkStatus();
}

}  // namespace

std::string BoundsSizeExpr(const BufferBounds& buffer_bounds) {
  return std::visit(
      absl::Overload{
          [](const bounds::Singleton&) { return std::string(); },
          [](const bounds::NullTerminated&) { return std::string(); },
          [](const bounds::ElemCount& elem) { return elem.size_expr; },
          [](const bounds::ByteCount& byte) { return byte.size_expr; },
          [](const bounds::ElemSizedByOutparam& elem) {
            return absl::StrCat("*", elem.outparam_name);
          },
          [](const bounds::ByteSizedByOutparam& byte) {
            return absl::StrCat("*", byte.outparam_name);
          },
          [](const bounds::SizedByBinding&) { return std::string(); },
      },
      buffer_bounds);
}

absl::Status ValidateAndLinkLibraryIR(Library& library) {
  for (auto& func : library.functions) {
    ABSL_RETURN_IF_ERROR(LinkAliasParamToCallbackParam(func));
    for (auto& param : func.parameters) {
      ABSL_RETURN_IF_ERROR(ValidateAndLinkParameter(func, param));
    }
    if (func.return_value) {
      ABSL_RETURN_IF_ERROR(ValidateAndLinkParameter(func, *func.return_value));
    }
  }

  return absl::OkStatus();
}

}  // namespace ir
}  // namespace sapi
