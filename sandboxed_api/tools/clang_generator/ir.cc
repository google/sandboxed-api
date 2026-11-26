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
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/strings/substitute.h"
#include "absl/types/span.h"
#include "sandboxed_api/call.h"
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
              static_assert(
                  IsOneOf<T, VoidParam, ScalarParam, SandboxeeCallbackParam>,
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
                                  OpaquePointerParam, StructSyncParam,
                                  SandboxeeCallbackParam>,
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
                          CallbackParam, SandboxeeCallbackParam>,
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

// Invokes `visit(ident)` on every evaluated identifier in `expr`, skipping
// string/character literals, struct member names after `.` or `->`, and
// unevaluated `sizeof`, `alignof`, and `decltype` operands.
template <typename VisitFn>
absl::Status ForEachEvaluatedIdentifier(absl::string_view expr,
                                        VisitFn&& visit) {
  size_t i = 0;
  const size_t n = expr.size();
  auto skip_ws = [&]() {
    while (i < n && absl::ascii_isspace(expr[i])) {
      ++i;
    }
  };
  bool after_member_op = false;
  while (i < n) {
    skip_ws();
    if (i >= n) break;
    char c = expr[i];
    if (c == '\'' || c == '"') {
      char quote = c;
      ++i;
      while (i < n && expr[i] != quote) {
        if (expr[i] == '\\' && i + 1 < n) ++i;
        ++i;
      }
      if (i < n) ++i;
      after_member_op = false;
      continue;
    }
    if (c == '.') {
      ++i;
      after_member_op = true;
      continue;
    }
    if (c == '-' && i + 1 < n && expr[i + 1] == '>') {
      i += 2;
      after_member_op = true;
      continue;
    }
    if (absl::ascii_isalpha(c) || c == '_') {
      size_t start = i;
      while (i < n && (absl::ascii_isalnum(expr[i]) || expr[i] == '_')) {
        ++i;
      }
      absl::string_view ident = expr.substr(start, i - start);
      if (!after_member_op &&
          (ident == "sizeof" || ident == "alignof" || ident == "decltype")) {
        skip_ws();
        if (i < n && expr[i] == '(') {
          int depth = 1;
          ++i;
          while (i < n && depth > 0) {
            if (expr[i] == '\'' || expr[i] == '"') {
              char q = expr[i++];
              while (i < n && expr[i] != q) {
                if (expr[i] == '\\' && i + 1 < n) ++i;
                ++i;
              }
            } else if (expr[i] == '(') {
              ++depth;
            } else if (expr[i] == ')') {
              --depth;
            }
            if (i < n) ++i;
          }
        } else {
          while (i < n && (expr[i] == '*' || expr[i] == '&' ||
                           absl::ascii_isspace(expr[i]))) {
            ++i;
          }
          while (i < n &&
                 (absl::ascii_isalnum(expr[i]) || expr[i] == '_' ||
                  expr[i] == '.' ||
                  (expr[i] == '-' && i + 1 < n && expr[i + 1] == '>'))) {
            if (expr[i] == '-') ++i;
            ++i;
          }
        }
        after_member_op = false;
        continue;
      }
      if (!after_member_op) {
        ABSL_RETURN_IF_ERROR(visit(ident));
      }
      after_member_op = false;
      continue;
    }
    ++i;
    after_member_op = false;
  }
  return absl::OkStatus();
}

// Checks that a sizing expression which names a single sibling parameter names
// one that exists, never reads an output-only (`SANDBOX_OUT_PTR`) parameter
// before the call, and in callbacks only references scalar parameters.
//
// In host callback trampolines, non-scalar lambda parameters hold raw untrusted
// sandboxee addresses or tokens rather than host values, so evaluating
// expressions such as `*len` or `hdr->len` directly in the lambda would
// dereference an untrusted sandboxee pointer in host memory.
absl::Status ValidateSizeExpr(absl::Span<const Parameter> scope,
                              absl::string_view scope_desc,
                              absl::string_view role,
                              absl::string_view size_expr,
                              bool in_callback = false,
                              bool evaluated_pre_call = true) {
  std::optional<absl::string_view> sibling = SoleReferencedParam(size_expr);
  if (sibling.has_value() && FindInScope(scope, *sibling) == nullptr) {
    return absl::InvalidArgumentError(absl::Substitute(
        "$0 $1 references non-existent sibling parameter $2 in "
        "its sizing expression.",
        scope_desc, role, *sibling));
  }
  return ForEachEvaluatedIdentifier(
      size_expr, [&](absl::string_view ident) -> absl::Status {
        const Parameter* sib = FindInScope(scope, ident);
        if (sib == nullptr) {
          return absl::OkStatus();
        }
        if (in_callback && !sib->Is<ScalarParam>()) {
          return absl::InvalidArgumentError(absl::Substitute(
              "$0 $1: callback sizing expression '$2' cannot reference "
              "non-scalar parameter $3.",
              scope_desc, role, size_expr, ident));
        }
        if (evaluated_pre_call && sib->direction() == PointerDir::kOut) {
          return absl::InvalidArgumentError(absl::Substitute(
              "$0 $1: sizing expression '$2' cannot read SANDBOX_OUT_PTR "
              "parameter $3 before the call; use "
              "SANDBOX_ELEM_SIZED_BY_OUTPARAM / SANDBOX_BYTE_SIZED_BY_OUTPARAM "
              "or SANDBOX_INOUT_PTR.",
              scope_desc, role, size_expr, ident));
        }
        return absl::OkStatus();
      });
}

// Checks that the sibling an outparam-sized buffer is sized by exists and is
// actually an output, and that the host-side capacity expression is non-empty.
absl::Status ValidateOutparamSize(absl::Span<const Parameter> scope,
                                  absl::string_view scope_desc,
                                  absl::string_view role,
                                  absl::string_view out_name,
                                  absl::string_view capacity_expr,
                                  absl::string_view kind_name,
                                  bool in_callback = false) {
  if (absl::StripAsciiWhitespace(capacity_expr).empty()) {
    return absl::InvalidArgumentError(
        absl::Substitute("$0 $1 $2 requires a non-empty capacity expression.",
                         scope_desc, role, kind_name));
  }
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
  return ValidateSizeExpr(scope, scope_desc, role, capacity_expr, in_callback);
}

// `scope_desc` names the enclosing entity for diagnostics, e.g.
// "Function foo" or "callback on_data".
absl::Status ValidateBufferBounds(absl::Span<const Parameter> scope,
                                  absl::string_view scope_desc,
                                  absl::string_view role,
                                  const BufferBounds& buffer_bounds,
                                  bool in_callback = false,
                                  bool evaluated_pre_call = true) {
  return std::visit(
      absl::Overload{
          [](const bounds::Singleton&) { return absl::OkStatus(); },
          [](const bounds::NullTerminated&) { return absl::OkStatus(); },
          [&](const bounds::ElemCount& elem) {
            return ValidateSizeExpr(scope, scope_desc, role, elem.size_expr,
                                    in_callback, evaluated_pre_call);
          },
          [&](const bounds::ByteCount& byte) {
            return ValidateSizeExpr(scope, scope_desc, role, byte.size_expr,
                                    in_callback, evaluated_pre_call);
          },
          [&](const bounds::ElemSizedByOutparam& elem) {
            return ValidateOutparamSize(scope, scope_desc, role,
                                        elem.outparam_name, elem.capacity_expr,
                                        "ELEM_SIZED_BY_OUTPARAM", in_callback);
          },
          [&](const bounds::ByteSizedByOutparam& byte) {
            return ValidateOutparamSize(scope, scope_desc, role,
                                        byte.outparam_name, byte.capacity_expr,
                                        "BYTE_SIZED_BY_OUTPARAM", in_callback);
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
    if (const auto* sync = cb_param.As<StructSyncParam>()) {
      if (!sync->members.empty() || !sync->callbacks.empty()) {
        return absl::InvalidArgumentError(absl::Substitute(
            "$0 parameter $1: deeply synced or callback-bearing struct "
            "pointers are not supported in callbacks",
            scope_desc, cb_param.name));
      }
    }
    if (const auto* buf = cb_param.As<BufferParam>()) {
      ABSL_RETURN_IF_ERROR(ValidateBufferBounds(
          cb.callback_parameters, scope_desc,
          absl::StrCat("parameter ", cb_param.name), buf->bounds,
          /*in_callback=*/true));
      if (buf->pointee != nullptr) {
        if (const auto* inner_buf = buf->pointee->As<BufferParam>()) {
          if (inner_buf->pointee != nullptr) {
            return absl::InvalidArgumentError(absl::Substitute(
                "$0 parameter $1: multi-level pointers deeper than two levels "
                "are not supported for callbacks",
                scope_desc, cb_param.name));
          }
          ABSL_RETURN_IF_ERROR(ValidateBufferBounds(
              cb.callback_parameters, scope_desc,
              absl::StrCat("parameter ", buf->pointee->name), inner_buf->bounds,
              /*in_callback=*/true));
        }
      }
    }
  }
  if (cb.return_value != nullptr) {
    if (const auto* buf = cb.return_value->As<BufferParam>()) {
      ABSL_RETURN_IF_ERROR(ValidateBufferBounds(
          cb.callback_parameters, scope_desc, "return value", buf->bounds,
          /*in_callback=*/true));
    }
  }
  return absl::OkStatus();
}

bool IsByteLikePointee(absl::string_view type_name) {
  return type_name == "char" || type_name == "unsigned char" ||
         type_name == "signed char";
}

bool IsCompatibleAliasPointee(absl::string_view ret_pointee,
                              absl::string_view src_pointee) {
  if (ret_pointee == src_pointee || ret_pointee == "void") {
    return true;
  }
  return IsByteLikePointee(ret_pointee) &&
         (src_pointee == "void" || IsByteLikePointee(src_pointee));
}

size_t CountLoweredRpcArgs(const Function& func) {
  size_t count = 0;
  for (const Parameter& p : func.parameters) {
    if (const auto* str = p.As<CppStringParam>()) {
      if (str->form == CppStringParam::Form::kView ||
          str->form == CppStringParam::Form::kValue ||
          (str->form == CppStringParam::Form::kReference && str->is_const)) {
        count += 2;
        continue;
      }
    }
    count += 1;
  }
  if (func.return_value.has_value() &&
      func.return_value->type.kind != TypeKind::kVoid) {
    count += 1;
  }
  return count;
}

// Resolves the cross-parameter references on `param` against `func`. Rules
// that concern a single parameter in isolation are enforced by the frontend
// when the IR is built (see arg_converter.cc); everything here needs the whole
// parameter list, either to look a sibling up or to write back to it.
absl::Status ValidateAndLinkParameter(Function& func, Parameter& param,
                                      bool is_top_level = true) {
  std::string role = param.is_return_value
                         ? "return value"
                         : absl::StrCat("parameter ", param.name);

  if (func.invoker_for_typedef.has_value()) {
    if (const auto* sync = param.As<StructSyncParam>()) {
      if (!sync->members.empty() || !sync->callbacks.empty()) {
        return absl::InvalidArgumentError(absl::Substitute(
            "callback typedef $0 $1: deeply synced or callback-bearing struct "
            "pointers are not supported in callbacks",
            *func.invoker_for_typedef, role));
      }
    }
  }

  const LifetimePolicy* policy = nullptr;
  if (auto* buf = param.As<BufferParam>()) {
    policy = &buf->lifetime;
    absl::Span<const Parameter> scope = func.parameters;
    std::string scope_desc = absl::StrCat("Function ", func.name);
    const bool in_callback = func.invoker_for_typedef.has_value();
    if (in_callback) {
      scope = scope.subspan(1);
      scope_desc = absl::StrCat("callback typedef ", *func.invoker_for_typedef);
    }
    const bool evaluated_pre_call =
        !param.is_return_value &&
        (buf->direction != PointerDir::kOut ||
         (!buf->context_bound.copy_from_and_bind.has_value() &&
          !std::holds_alternative<lifetime::Malloc>(buf->lifetime) &&
          !std::holds_alternative<lifetime::SandboxGlobal>(buf->lifetime)));
    ABSL_RETURN_IF_ERROR(ValidateBufferBounds(
        scope, scope_desc, role, buf->bounds, in_callback, evaluated_pre_call));
    if (is_top_level && buf->pointee == nullptr && param.IsOutput() &&
        !param.is_return_value &&
        !buf->context_bound.copy_from_and_bind.has_value()) {
      if (const auto* elem = std::get_if<bounds::ElemCount>(&buf->bounds)) {
        absl::string_view trimmed = absl::StripAsciiWhitespace(elem->size_expr);
        std::optional<absl::string_view> sibling =
            SoleReferencedParam(elem->size_expr);
        if (absl::StartsWith(trimmed, "*") && sibling.has_value()) {
          const Parameter* sib = FindInScope(scope, *sibling);
          if (sib != nullptr && sib != &param && sib->IsInOut()) {
            buf->bounds = bounds::ElemSizedByOutparam{
                .outparam_name = std::string(*sibling),
                .capacity_expr =
                    absl::Substitute("($0 == nullptr ? 0 : "
                                     "sandbox->CheckedMultiply(sizeof(*$1), "
                                     "sapi::lwbox::CheckedToSizeT(*$0)))",
                                     *sibling, param.name),
            };
          }
        }
      } else if (const auto* byte =
                     std::get_if<bounds::ByteCount>(&buf->bounds)) {
        absl::string_view trimmed = absl::StripAsciiWhitespace(byte->size_expr);
        std::optional<absl::string_view> sibling =
            SoleReferencedParam(byte->size_expr);
        if (absl::StartsWith(trimmed, "*") && sibling.has_value()) {
          const Parameter* sib = FindInScope(scope, *sibling);
          if (sib != nullptr && sib != &param && sib->IsInOut()) {
            buf->bounds = bounds::ByteSizedByOutparam{
                .outparam_name = std::string(*sibling),
                .capacity_expr = absl::Substitute(
                    "($0 == nullptr ? 0 : sapi::lwbox::CheckedToSizeT(*$0))",
                    *sibling),
            };
          }
        }
      }

      // Any remaining ElemCount/ByteCount on a single-level output buffer
      // (excluding callee-allocated copy_from_and_bind) was not rewritten to
      // *SizedByOutparam (for example, a compound expression such as
      // '*inout_len * 2' or missing '*' on an in-out pointer sibling). Reject
      // references to SANDBOX_INOUT_PTR siblings so output sizing is never
      // silently fixed at the pre-call value while ignoring post-call updates
      // to the sibling.
      const std::string* unrewritten_expr = nullptr;
      if (const auto* elem = std::get_if<bounds::ElemCount>(&buf->bounds)) {
        unrewritten_expr = &elem->size_expr;
      } else if (const auto* byte =
                     std::get_if<bounds::ByteCount>(&buf->bounds)) {
        unrewritten_expr = &byte->size_expr;
      }
      if (unrewritten_expr != nullptr) {
        ABSL_RETURN_IF_ERROR(ForEachEvaluatedIdentifier(
            *unrewritten_expr, [&](absl::string_view ident) -> absl::Status {
              const Parameter* sib = FindInScope(scope, ident);
              if (sib != nullptr && sib->IsInOut()) {
                return absl::InvalidArgumentError(absl::Substitute(
                    "$0 $1: sizing expression '$2' on an output buffer cannot "
                    "reference SANDBOX_INOUT_PTR parameter $3 unless written "
                    "as '*$3'; use SANDBOX_ELEM_SIZED_BY_OUTPARAM / "
                    "SANDBOX_BYTE_SIZED_BY_OUTPARAM or SANDBOX_IN_PTR.",
                    scope_desc, role, *unrewritten_expr, ident));
              }
              return absl::OkStatus();
            }));
      }
    }
    if (buf->pointee != nullptr) {
      if (in_callback) {
        if (const auto* inner_buf = buf->pointee->As<BufferParam>();
            inner_buf != nullptr && inner_buf->pointee != nullptr) {
          return absl::InvalidArgumentError(absl::Substitute(
              "$0 $1: multi-level pointers deeper than two levels are not "
              "supported for callbacks",
              scope_desc, role));
        }
      }
      ABSL_RETURN_IF_ERROR(ValidateAndLinkParameter(func, *buf->pointee,
                                                    /*is_top_level=*/false));
    }
  } else if (const auto* sync = param.As<StructSyncParam>()) {
    ABSL_RETURN_IF_ERROR(ValidateBufferBounds(
        func.parameters, absl::StrCat("Function ", func.name), role,
        sync->array_bounds));
    bool invalid_bounds =
        std::visit(absl::Overload{
                       [](const bounds::ElemCount& elem) {
                         return absl::StartsWith(
                             absl::StripAsciiWhitespace(elem.size_expr), "*");
                       },
                       [](const bounds::ByteCount& byte) {
                         return absl::StartsWith(
                             absl::StripAsciiWhitespace(byte.size_expr), "*");
                       },
                       [](const bounds::ElemSizedByOutparam&) { return true; },
                       [](const bounds::ByteSizedByOutparam&) { return true; },
                       [](const auto&) { return false; },
                   },
                   sync->array_bounds);
    if (invalid_bounds) {
      return absl::InvalidArgumentError(absl::Substitute(
          "Function $0 $1 does not support pointer-dereference or "
          "outparam-sized array bounds on synchronized struct arrays.",
          func.name, role));
    }
    // The struct-sync emitters copy the struct (or struct array) into sandbox
    // memory that is freed when the call returns, and they do not bind it to a
    // context. Reject the outer context bindings instead of ignoring them, so
    // that the sandboxee cannot keep a dangling pointer to the struct array.
    if (sync->context_bound.retain_and_bind.has_value() ||
        sync->context_bound.copy_from_and_bind.has_value()) {
      return absl::InvalidArgumentError(absl::Substitute(
          "Function $0 $1 does not support retain_and_bind or "
          "copy_from_and_bind_out_ptr on a synchronized struct pointer. Put "
          "retain_and_bind on struct_sync members instead.",
          func.name, role));
    }
  } else if (const auto* opaque = param.As<OpaquePointerParam>()) {
    policy = &opaque->lifetime;
  } else if (const auto* cb = param.As<CallbackParam>()) {
    ABSL_RETURN_IF_ERROR(ValidateCallbackScope(param.name, *cb));
  }

  if (policy != nullptr) {
    ABSL_RETURN_IF_ERROR(std::visit(
        absl::Overload{
            [](const lifetime::ScopedCall&) { return absl::OkStatus(); },
            [](const lifetime::SandboxGlobal&) { return absl::OkStatus(); },
            [](const lifetime::Malloc&) { return absl::OkStatus(); },
            [&](const lifetime::AliasHostPtr& alias) -> absl::Status {
              if (!param.is_return_value) {
                return absl::InvalidArgumentError(absl::Substitute(
                    "Function $0 $1: alias_ptr is only supported on function "
                    "return values and host_opaque_ptr callback parameters.",
                    func.name, role));
              }
              const Parameter* host_param =
                  func.FindParameter(alias.host_param_name);
              if (!host_param || !host_param->type.is_pointer()) {
                return absl::InvalidArgumentError(absl::Substitute(
                    "Function $0 $1 alias_ptr references non-existent or "
                    "non-pointer parameter $2.",
                    func.name, role, alias.host_param_name));
              }
              const auto* host_buf = host_param->As<BufferParam>();
              const auto* host_sync = host_param->As<StructSyncParam>();
              if ((host_buf == nullptr && host_sync == nullptr) ||
                  (host_buf != nullptr &&
                   (host_buf->pointee != nullptr ||
                    host_buf->context_bound.copy_from_and_bind.has_value() ||
                    !std::holds_alternative<lifetime::ScopedCall>(
                        host_buf->lifetime)))) {
                return absl::InvalidArgumentError(absl::Substitute(
                    "Function $0 $1 alias_ptr parameter $2 must be a "
                    "single-level synchronized buffer or struct parameter.",
                    func.name, role, alias.host_param_name));
              }
              if (host_param->type.pointee != nullptr &&
                  host_param->type.pointee->is_const &&
                  param.type.pointee != nullptr &&
                  !param.type.pointee->is_const) {
                return absl::InvalidArgumentError(absl::Substitute(
                    "Function $0 $1 alias_ptr cannot return non-const pointer "
                    "'$2' aliasing const parameter $3 ('$4').",
                    func.name, role, param.type.canonical_name,
                    alias.host_param_name, host_param->type.canonical_name));
              }
              const std::string ret_pointee =
                  param.type.unqualified_pointee_type_name();
              const std::string host_pointee =
                  host_param->type.unqualified_pointee_type_name();
              if (!IsCompatibleAliasPointee(ret_pointee, host_pointee)) {
                return absl::InvalidArgumentError(absl::Substitute(
                    "Function $0 $1 alias_ptr return pointee '$2' is "
                    "incompatible with parameter $3 pointee '$4'.",
                    func.name, role, ret_pointee, alias.host_param_name,
                    host_pointee));
              }
              return absl::OkStatus();
            },
            [&](const lifetime::AliasCallbackReturn& alias) -> absl::Status {
              if (!param.is_return_value) {
                return absl::InvalidArgumentError(absl::Substitute(
                    "Function $0 $1: alias_callback_return is only supported "
                    "on function return values.",
                    func.name, role));
              }
              Parameter* cb_param =
                  func.FindParameter(alias.callback_param_name);
              if (!cb_param || !cb_param->Is<CallbackParam>()) {
                return absl::InvalidArgumentError(absl::Substitute(
                    "Function $0 $1 alias_callback_return references "
                    "non-existent or non-callback parameter $2.",
                    func.name, role, alias.callback_param_name));
              }
              auto* cb = cb_param->As<CallbackParam>();
              if (cb->return_value == nullptr ||
                  !cb->return_value->Is<BufferParam>() ||
                  !cb->return_value->type.is_pointer()) {
                return absl::InvalidArgumentError(absl::Substitute(
                    "Function $0 $1 alias_callback_return references callback "
                    "$2 whose return value is not a buffer pointer.",
                    func.name, role, alias.callback_param_name));
              }
              if (cb->return_value->type.pointee != nullptr &&
                  cb->return_value->type.pointee->is_const &&
                  param.type.pointee != nullptr &&
                  !param.type.pointee->is_const) {
                return absl::InvalidArgumentError(absl::Substitute(
                    "Function $0 $1 alias_callback_return cannot strip const "
                    "from callback $2 return type '$3'.",
                    func.name, role, alias.callback_param_name,
                    cb->return_value->type.canonical_name));
              }
              const std::string ret_pointee =
                  param.type.unqualified_pointee_type_name();
              const std::string cb_ret_pointee =
                  cb->return_value->type.unqualified_pointee_type_name();
              if (!IsCompatibleAliasPointee(ret_pointee, cb_ret_pointee)) {
                return absl::InvalidArgumentError(absl::Substitute(
                    "Function $0 $1 alias_callback_return return pointee '$2' "
                    "is incompatible with callback $3 return pointee '$4'.",
                    func.name, role, ret_pointee, alias.callback_param_name,
                    cb_ret_pointee));
              }
              cb->is_callback_return_aliased = true;
              return absl::OkStatus();
            },
        },
        *policy));
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
        if (const auto* alias =
                std::get_if<lifetime::AliasHostPtr>(&opaque->lifetime)) {
          outer_param_name = alias->host_param_name;
        }
      } else if (const auto* buf = cb_param.As<BufferParam>()) {
        if (const auto* alias =
                std::get_if<lifetime::AliasHostPtr>(&buf->lifetime)) {
          outer_param_name = alias->host_param_name;
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

absl::Status ValidateAndLinkLibraryIR(Library& library) {
  for (auto& func : library.functions) {
    const size_t rpc_args = CountLoweredRpcArgs(func);
    // The emitters generate no sandboxee wrapper (and thus no RPC) for a
    // function with a sandboxee thunk, so its arguments never go through
    // FuncCall. A host thunk alone keeps the generated sandboxee wrapper.
    if (!func.sandboxee_thunk.has_value() &&
        rpc_args > ::sapi::FuncCall::kArgsMax) {
      return absl::InvalidArgumentError(
          absl::Substitute("Function $0 lowers to $1 RPC arguments, exceeding "
                           "FuncCall::kArgsMax ($2).",
                           func.name, rpc_args, ::sapi::FuncCall::kArgsMax));
    }
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
