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

#include "sandboxed_api/tools/clang_generator/annotations.h"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/status_macros.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/strings/substitute.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclarationName.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Type.h"
#include "clang/Basic/LLVM.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"
#include "re2/re2.h"
#include "sandboxed_api/tools/clang_generator/ast_utils.h"
#include "sandboxed_api/tools/clang_generator/sandboxed_library_emitter.h"

namespace sapi {

std::string StripAnnotations(const std::string& input) {
  static const auto* macros_no_args = new std::vector<std::string>{
      "SANDBOX_IN_PTR",          "SANDBOX_OUT_PTR",
      "SANDBOX_INOUT_PTR",       "SANDBOX_OPAQUE_PTR",
      "SANDBOX_HOST_OPAQUE_PTR", "SANDBOX_HOST_STATE_VAR",
      "SANDBOX_NULL_TERMINATED", "SANDBOX_LIFETIME_GLOBAL",
      "SANDBOX_CLEAR_BINDINGS",  "SANDBOX_SHALLOW_SYNC"};
  std::string output = input;
  for (const auto& macro : *macros_no_args) {
    // We use a regex to match word boundaries
    RE2::GlobalReplace(&output, "\\b" + macro + "\\b", "");
  }

  static const auto* macros_args = new std::vector<std::string>{
      "SANDBOX_SANDBOXEE_THUNK",
      "SANDBOX_HOST_THUNK",
      "SANDBOX_ALIAS_PTR",
      "SANDBOX_ELEM_SIZED_BY",
      "SANDBOX_BYTE_SIZED_BY",
      "SANDBOX_ELEM_SIZED_BY_OUTPARAM",
      "SANDBOX_BYTE_SIZED_BY_OUTPARAM",
      "SANDBOX_BYTE_SIZED_BY_BINDING",
      "SANDBOX_BIND_DATA",
      "SANDBOX_BIND_SIZE",
      "SANDBOX_COPY_FROM_AND_BIND_OUT_PTR",
      "SANDBOX_RETAIN_AND_BIND",
      "SANDBOX_STRUCT_SYNC",
  };
  // We also remove the full argument to the macro, e.g.
  // SANDBOX_ELEM_SIZED_BY(foo) will be removed entirely.
  for (const auto& macro : *macros_args) {
    RE2::GlobalReplace(&output, "\\b" + macro + "\\([^\\)]*\\)", "");
  }
  return output;
}
std::string StripQuotes(absl::string_view str) {
  if (str.size() >= 2 && str.front() == '"' && str.back() == '"') {
    return std::string(str.substr(1, str.size() - 2));
  }
  return std::string(str);
}

absl::StatusOr<std::vector<SandboxAnnotation>> GetSandboxAnnotations(
    const clang::Decl* decl) {
  std::vector<SandboxAnnotation> annotations;
  for (const auto& attr : decl->attrs()) {
    const auto* ann_attr = llvm::dyn_cast<clang::AnnotateAttr>(attr);
    if (!ann_attr || ann_attr->getAnnotation() != "sandbox") {
      continue;
    }
    if (ann_attr->args_size() == 0) {
      continue;
    }
    SandboxAnnotation annotation;
    std::optional<std::string> arg_str =
        ann_attr->args_begin()[0]->tryEvaluateString(decl->getASTContext());
    if (!arg_str) {
      return absl::InvalidArgumentError(absl::Substitute(
          "arg $0: invalid sandbox annotation",
          clang::dyn_cast<clang::NamedDecl>(decl)->getNameAsString()));
    }
    annotation.name = *arg_str;

    for (size_t i = 1; i < ann_attr->args_size(); ++i) {
      arg_str =
          ann_attr->args_begin()[i]->tryEvaluateString(decl->getASTContext());
      if (arg_str) {
        annotation.args.push_back(*arg_str);
      }
    }
    annotations.push_back(annotation);
  }
  return annotations;
}

std::vector<std::string> GetAnnotations(clang::Decl* decl) {
  auto result = GetSandboxAnnotations(decl);
  if (!result.ok()) {
    return {};
  }
  std::vector<std::string> annotations;
  for (const auto& ann : *result) {
    annotations.push_back(ann.name);
    for (const auto& arg : ann.args) {
      annotations.push_back(arg);
    }
  }
  return annotations;
}

namespace {

// Check that the shadow record declaration with annotations matches a real
// record declaration. This is a real record declaration has the same name,
// but should be declared in a parent declaration context.
absl::Status CheckShadowRecordMatchesRealRecord(
    const clang::RecordDecl& shadow_decl) {
  llvm::StringRef record_name = shadow_decl.getName();
  clang::ASTContext& ast_ctx = shadow_decl.getASTContext();
  clang::IdentifierInfo& id_info = ast_ctx.Idents.get(record_name);
  clang::DeclarationName dec_name(&id_info);

  const clang::RecordDecl* real_record = nullptr;
  // Search parent declaration contexts for a matching record declaration.
  for (const clang::DeclContext* ctx = shadow_decl.getDeclContext();
       ctx != nullptr; ctx = ctx->getParent()) {
    auto lookup_result = ctx->lookup(dec_name);
    for (clang::NamedDecl* nd : lookup_result) {
      if (const auto* rd = clang::dyn_cast<clang::RecordDecl>(nd)) {
        if (rd->getCanonicalDecl() != shadow_decl.getCanonicalDecl()) {
          real_record = rd;
          break;
        }
      }
    }
    if (real_record != nullptr) {
      break;
    }
  }

  if (real_record == nullptr) {
    return absl::NotFoundError(absl::Substitute(
        "Could not find matching real struct definition for $0",
        record_name.str()));
  }

  real_record = real_record->getDefinition();
  if (real_record == nullptr) {
    return absl::NotFoundError(absl::Substitute(
        "Real struct $0 is declared but not defined", record_name.str()));
  }

  auto real_it = real_record->fields().begin();
  auto real_end = real_record->fields().end();

  // It is okay to skip some fields in the shadow struct, but the ones that are
  // present should be in the same order and have matching names and types.
  for (const clang::FieldDecl* shadow_field : shadow_decl.fields()) {
    llvm::StringRef field_name = shadow_field->getName();
    if (field_name.empty()) {
      continue;
    }

    const clang::FieldDecl* matched_field = nullptr;
    for (; real_it != real_end; ++real_it) {
      if ((*real_it)->getName() == field_name) {
        matched_field = *real_it;
        ++real_it;
        break;
      }
    }

    if (matched_field == nullptr) {
      return absl::InvalidArgumentError(absl::Substitute(
          "field $0 not found in real record $1 (or is out of relative order)",
          field_name.str(), record_name.str()));
    }

    clang::QualType shadow_type = shadow_field->getType().getCanonicalType();
    clang::QualType real_type = matched_field->getType().getCanonicalType();
    if (shadow_type != real_type) {
      return absl::InvalidArgumentError(
          absl::Substitute("Type mismatch for field $0 in record $1: "
                           "shadow type is '$2', but real type is '$3'",
                           field_name.str(), record_name.str(),
                           shadow_type.getAsString(), real_type.getAsString()));
    }
  }
  return absl::OkStatus();
}

}  // namespace


absl::StatusOr<SandboxedLibraryEmitter::Annotations>
SandboxedLibraryEmitter::ParseAnnotations(absl::string_view name,
                                          const clang::FunctionDecl* funcDecl) {
  Annotations annotations;
  ABSL_ASSIGN_OR_RETURN(auto parsed_annotations,
                        GetSandboxAnnotations(funcDecl));

  for (const auto& ann : parsed_annotations) {
    size_t num_args = 1;

    // We can only have either opaque pointers, or out pointers as return
    // values. I.e. either the returned pointer is just a sandbox-internal
    // opaque handle, or it is a pointer to some sandbox structure that needs
    // copying back to the host.
    if (ann.name == "sandbox_opaque_ptr") {
      annotations.ptr_dir = PointerDir::kSandboxOpaque;
    } else if (ann.name == "out_ptr") {
      annotations.ptr_dir = PointerDir::kOut;
    } else if (ann.name == "sandboxee_thunk" || ann.name == "host_thunk") {
      // Ignore these here, they are handled in AddFunction.
      num_args = 2;  // (name, func_name)
    } else if (ann.name == "elem_sized_by") {
      if (ann.args.size() != 1) {
        return absl::InvalidArgumentError(
            absl::Substitute("function return $0: `elem_sized_by` annotation "
                             "requires one argument",
                             name));
      }
      num_args = 2;
      absl::Status status = annotations.SetElemSizedBy(ann.args[0]);
      ABSL_RETURN_IF_ERROR(status);
    } else if (ann.name == "byte_sized_by") {
      if (ann.args.size() != 1) {
        return absl::InvalidArgumentError(
            absl::Substitute("function return $0: `byte_sized_by` annotation "
                             "requires one argument",
                             name));
      }
      num_args = 2;
      absl::Status status = annotations.SetByteSizedBy(ann.args[0]);
      ABSL_RETURN_IF_ERROR(status);
    } else if (ann.name == "null_terminated") {
      ABSL_RETURN_IF_ERROR(annotations.SetNullTerminated());
    } else if (ann.name == "sized_by_binding") {
      if (ann.args.size() != 2) {
        return absl::InvalidArgumentError(
            absl::Substitute("function return $0: `sized_by_binding` "
                             "annotation requires two arguments",
                             name));
      }
      num_args = 3;
      absl::Status status = annotations.SetSizedByBinding(
          StripQuotes(ann.args[0]), StripQuotes(ann.args[1]));
      ABSL_RETURN_IF_ERROR(status);
    } else if (ann.name == "lifetime_sandbox_global") {
      ABSL_RETURN_IF_ERROR(annotations.SetSandboxGlobalLifetime());
    } else if (ann.name == "alias_ptr") {
      num_args = 2;
      if (ann.args.empty()) {
        return absl::InvalidArgumentError(absl::Substitute(
            "function return $0: alias_ptr requires a parameter name", name));
      }
      ABSL_RETURN_IF_ERROR(annotations.SetAliasHostPtrLifetime(ann.args[0]));
      if (annotations.ptr_dir.has_value()) {
        return absl::InvalidArgumentError(
            absl::Substitute("function return $0: alias_ptr implies out_ptr, "
                             "so no direction needs to be specified",
                             name));
      }
      annotations.ptr_dir = PointerDir::kOut;
    } else if (ann.name == "alias_callback_return") {
      num_args = 2;
      if (ann.args.size() != 1) {
        return absl::InvalidArgumentError(
            absl::Substitute("function return $0: alias_callback_return "
                             "requires exactly one parameter name",
                             name));
      }
      ABSL_RETURN_IF_ERROR(
          annotations.SetAliasCallbackReturnLifetime(ann.args[0]));
      if (annotations.ptr_dir.has_value() &&
          annotations.ptr_dir != PointerDir::kOut) {
        return absl::InvalidArgumentError(
            absl::Substitute("function return $0: alias_callback_return implies"
                             " out_ptr, so no direction needs to be specified",
                             name));
      }
      annotations.ptr_dir = PointerDir::kOut;
    } else if (ann.name == "bind_data") {
      // For now, we only support size_t typed primitives.
      if (StripQuotes(ann.args[1]) != "size_t") {
        return absl::InvalidArgumentError(
            absl::Substitute("function return $0: `bind_data` annotation only "
                             "supports `size_t` typed primitives for now.",
                             name));
      }
      annotations.context_bound.bind_data.push_back(
          BindData{StripQuotes(ann.args[0]), StripQuotes(ann.args[1]),
                   StripQuotes(ann.args[2]), StripQuotes(ann.args[3])});
      num_args = 5;
    } else if (ann.name == "copy_from_and_bind_out_ptr") {
      if (ann.args.size() != 2) {
        return absl::InvalidArgumentError(
            absl::Substitute("function return $0: `copy_from_and_bind_out_ptr` "
                             "annotation requires two arguments",
                             name));
      }
      num_args = 3;
      annotations.ptr_dir = PointerDir::kOut;
      annotations.context_bound.copy_from_and_bind = CopyFromAndBindOutPtr{
          StripQuotes(ann.args[0]), StripQuotes(ann.args[1])};
    } else {
      return absl::InvalidArgumentError(
          absl::Substitute("function return $0: $1 annotation is not supported "
                           "for function declarations",
                           name, ann.name));
    }
    if (ann.args.size() != num_args - 1) {
      return absl::InvalidArgumentError(absl::Substitute(
          "function return $0: invalid sandbox annotation", name));
    }
  }
  ABSL_RETURN_IF_ERROR(CheckParsedAnnotations(
      name, annotations, funcDecl->getReturnType().getCanonicalType()));
  return annotations;
}

absl::StatusOr<SandboxedLibraryEmitter::Annotations>
SandboxedLibraryEmitter::ParseAnnotations(absl::string_view name,
                                          const clang::ParmVarDecl* param) {
  Annotations annotations;
  // TODO(dvyukov): add more error checking with good error messages
  // (duplicate/conflicting/inapplicable annotations, non-existent arg names,
  // etc).
  ABSL_ASSIGN_OR_RETURN(auto parsed_annotations, GetSandboxAnnotations(param));

  for (const auto& ann : parsed_annotations) {
    size_t num_args = 1;
    if (ann.name == "in_ptr") {
      annotations.ptr_dir = PointerDir::kIn;
    } else if (ann.name == "out_ptr") {
      annotations.ptr_dir = PointerDir::kOut;
    } else if (ann.name == "inout_ptr") {
      annotations.ptr_dir = PointerDir::kInOut;
    } else if (ann.name == "sandbox_opaque_ptr") {
      annotations.ptr_dir = PointerDir::kSandboxOpaque;
    } else if (ann.name == "host_opaque_ptr") {
      annotations.ptr_dir = PointerDir::kHostOpaque;
    } else if (ann.name == "elem_sized_by") {
      num_args = 2;
      if (!ann.args.empty()) {
        absl::Status status = annotations.SetElemSizedBy(ann.args[0]);
        ABSL_RETURN_IF_ERROR(status);
      }
    } else if (ann.name == "byte_sized_by") {
      num_args = 2;
      if (!ann.args.empty()) {
        absl::Status status = annotations.SetByteSizedBy(ann.args[0]);
        ABSL_RETURN_IF_ERROR(status);
      }
    } else if (ann.name == "elem_sized_by_outparam") {
      num_args = 3;
      if (!ann.args.empty()) {
        absl::Status status =
            annotations.SetElemSizedByOutparam(ann.args[0], ann.args[1]);
        ABSL_RETURN_IF_ERROR(status);
      }
    } else if (ann.name == "byte_sized_by_outparam") {
      num_args = 3;
      if (!ann.args.empty()) {
        absl::Status status =
            annotations.SetByteSizedByOutparam(ann.args[0], ann.args[1]);
        ABSL_RETURN_IF_ERROR(status);
      }
    } else if (ann.name == "null_terminated") {
      absl::Status status = annotations.SetNullTerminated();
      ABSL_RETURN_IF_ERROR(status);
    } else if (ann.name == "sized_by_binding") {
      if (ann.args.size() != 2) {
        return absl::InvalidArgumentError(
            absl::Substitute("param $0: `sized_by_binding` annotation "
                             "requires two arguments",
                             name));
      }
      num_args = 3;
      absl::Status status = annotations.SetSizedByBinding(
          StripQuotes(ann.args[0]), StripQuotes(ann.args[1]));
      ABSL_RETURN_IF_ERROR(status);
    } else if (ann.name == "lifetime_sandbox_global") {
      ABSL_RETURN_IF_ERROR(annotations.SetSandboxGlobalLifetime());
    } else if (ann.name == "copy_from_and_bind_out_ptr") {
      if (ann.args.size() != 2) {
        return absl::InvalidArgumentError(
            absl::Substitute("param $0: `copy_from_and_bind_out_ptr` "
                             "annotation requires two arguments",
                             name));
      }
      num_args = 3;
      annotations.ptr_dir = PointerDir::kOut;
      annotations.context_bound.copy_from_and_bind = CopyFromAndBindOutPtr{
          StripQuotes(ann.args[0]), StripQuotes(ann.args[1])};
    } else if (ann.name == "retain_and_bind") {
      if (ann.args.size() != 2) {
        return absl::InvalidArgumentError(
            absl::Substitute("param $0: `retain_and_bind` annotation "
                             "requires two arguments",
                             name));
      }
      num_args = 3;
      annotations.context_bound.retain_and_bind =
          RetainAndBind{StripQuotes(ann.args[0]), StripQuotes(ann.args[1])};
    } else if (ann.name == "clear_bindings") {
      annotations.context_bound.clear_bindings = true;
      num_args = 1;
    } else if (ann.name == "struct_sync") {
      num_args = ann.args.size() + 1;
      ABSL_RETURN_IF_ERROR(
          ParseStructSyncAccessPathAnnotations(ann.args, annotations));
    } else if (ann.name == "shallow_struct_sync") {
      annotations.shallow_struct_sync = true;
      num_args = 1;
    } else {
      num_args = 0;
    }
    if (ann.args.size() != num_args - 1) {
      return absl::InvalidArgumentError(
          absl::Substitute("arg $0: invalid sandbox annotation", name));
    }
  }
  ABSL_RETURN_IF_ERROR(CheckParsedAnnotations(
      name, annotations, param->getType().getCanonicalType()));
  return annotations;
}

absl::Status SandboxedLibraryEmitter::CheckParsedAnnotations(
    absl::string_view name, const Annotations& annotations,
    clang::QualType type) const {
  if (annotations.context_bound.copy_from_and_bind.has_value() &&
      std::holds_alternative<std::monostate>(annotations.size_type)) {
    return absl::InvalidArgumentError(absl::Substitute(
        "$0: copy_from_and_bind_out_ptr annotation requires a sized_by "
        "annotation",
        name));
  }
  if (annotations.context_bound.retain_and_bind.has_value() &&
      std::holds_alternative<std::monostate>(annotations.size_type)) {
    return absl::InvalidArgumentError(absl::Substitute(
        "$0: retain_and_bind annotation requires a sized_by annotation", name));
  }

  if (!annotations.struct_sync.empty()) {
    if (!clang::isa<clang::PointerType>(type) ||
        !clang::isa<clang::RecordType>(
            type->getPointeeType().getCanonicalType())) {
      return absl::InvalidArgumentError(
          absl::Substitute("$0: struct_sync annotation should only be used "
                           "for pointers to structs",
                           name));
    }

    if (annotations.shallow_struct_sync) {
      return absl::InvalidArgumentError(
          absl::Substitute("$0: `shallow_struct_sync` annotation does not "
                           "need to be combined with `struct_sync` annotation",
                           name));
    }

    // We don't yet support an array of struct pointers plus struct_sync.
    if (!std::holds_alternative<std::monostate>(annotations.size_type)) {
      return absl::InvalidArgumentError(absl::Substitute(
          "$0: array of struct pointers with struct_sync is not supported",
          name));
    }
  }

  if (annotations.shallow_struct_sync) {
    if (!clang::isa<clang::PointerType>(type) ||
        !clang::isa<clang::RecordType>(
            type->getPointeeType().getCanonicalType())) {
      return absl::InvalidArgumentError(
          absl::Substitute("$0: shallow_struct_sync annotation should only be "
                           "used for pointers to structs",
                           name));
    }
  }

  return absl::OkStatus();
}

absl::Status SandboxedLibraryEmitter::ParseStructSyncAccessPathAnnotations(
    const std::vector<std::string>& annotation_args,
    Annotations& annotations) const {
  if (annotation_args.empty()) {
    return absl::InvalidArgumentError(
        "struct_sync annotation requires a binding label prefix argument");
  }
  absl::string_view binding_prefix = annotation_args[0];
  size_t i = 1;
  while (i < annotation_args.size()) {
    std::string item = StripQuotes(annotation_args[i]);
    i++;
    // Check if we hit the end of the list of access paths.
    if (item == "$") {
      break;
    }
    if (item.empty()) {
      return absl::InvalidArgumentError(
          "struct_sync access path group cannot be empty");
    }
    // Otherwise, we should parse an access path group "{...}"
    if (item.front() != '{') {
      return absl::InvalidArgumentError(
          "struct_sync access path group must start with '{'");
    }
    bool group_ends = false;
    // Check if it's a single item group, like "{s->field}".
    if (item.back() == '}') {
      group_ends = true;
      item.pop_back();
    }
    StructSync sync;
    sync.access_path = item.substr(1);
    if (sync.access_path.empty()) {
      return absl::InvalidArgumentError(
          "struct_sync access path cannot be empty");
    }
    std::optional<std::string> parent_prefix =
        ParentPrefixOfAccessPath(sync.access_path);
    std::optional<std::string> member =
        MemberNameOfAccessPath(sync.access_path);
    if (!parent_prefix.has_value() || !member.has_value()) {
      return absl::InvalidArgumentError(
          absl::Substitute("struct_sync access path format $0 is not supported",
                           sync.access_path));
    }
    // Parse any annotations (pointer direction, binding, etc.)
    // for this access path.
    while (!group_ends) {
      if (i >= annotation_args.size()) {
        return absl::InvalidArgumentError(
            "Unexpected end of struct_sync arguments");
      }
      std::string annotation = annotation_args[i];
      i++;
      if (annotation.empty()) {
        return absl::InvalidArgumentError(
            "struct_sync access path attribute cannot be empty");
      }
      if (annotation.back() == '}') {
        group_ends = true;
        annotation.pop_back();
      }

      // Parse a few annotations. Others we expect to be annotated
      // at the struct level (see ParseRecordAnnotations).
      if (annotation == "in_ptr") {
        sync.ptr_dir = PointerDir::kIn;
      } else if (annotation == "out_ptr") {
        sync.ptr_dir = PointerDir::kOut;
      } else if (annotation == "inout_ptr") {
        sync.ptr_dir = PointerDir::kInOut;
      } else if (absl::StartsWith(annotation, "retain_and_bind(")) {
        absl::string_view annotation_view = annotation;
        if (!absl::ConsumePrefix(&annotation_view, "retain_and_bind(") ||
            !absl::ConsumeSuffix(&annotation_view, ")")) {
          return absl::InvalidArgumentError(absl::Substitute(
              "struct_sync access path $0 attribute `retain_and_bind` "
              "requires a context argument: $1",
              sync.access_path, annotation));
        }
        std::string context = StripQuotes(annotation_view);
        std::string binding_name =
            absl::StrCat(binding_prefix, "_", sync.access_path);
        for (char& c : binding_name) {
          if (!absl::ascii_isalnum(c)) {
            c = '_';
          }
        }
        sync.context_bound.retain_and_bind =
            RetainAndBind{context, binding_name};
      } else {
        return absl::InvalidArgumentError(
            absl::Substitute("struct_sync access path attribute $0 is not "
                             "supported",
                             annotation));
      }
    }
    annotations.struct_sync.push_back(std::move(sync));
  }
  return absl::OkStatus();
}

absl::Status SandboxedLibraryEmitter::ParseRecordAnnotations(
    const clang::RecordDecl& decl) {
  llvm::StringRef record_name = decl.getName();
  if (record_name.empty()) {
    return absl::InvalidArgumentError("Not expecting an empty Record name");
  }

  RecordAnnotations record_annotations;
  record_annotations.name = record_name;

  for (const clang::FieldDecl* field : decl.fields()) {
    ABSL_ASSIGN_OR_RETURN(std::vector<SandboxAnnotation> annotations,
                          GetSandboxAnnotations(field));
    if (annotations.empty()) {
      continue;
    }

    DataMemberAnnotations member;
    member.name = field->getName();
    member.ptr_dir = std::nullopt;
    for (const auto& ann : annotations) {
      if (ann.name == "sandbox_opaque_ptr") {
        member.ptr_dir = PointerDir::kSandboxOpaque;
      } else if (ann.name == "elem_sized_by") {
        if (ann.args.size() != 1) {
          return absl::InvalidArgumentError(absl::Substitute(
              "field $0: elem_sized_by annotation requires a size expression",
              member.name));
        }
        member.size_type = ElemSizedBy{ann.args[0]};
      } else if (ann.name == "byte_sized_by") {
        if (ann.args.size() != 1) {
          return absl::InvalidArgumentError(absl::Substitute(
              "field $0: byte_sized_by annotation requires a size expression",
              member.name));
        }
        member.size_type = ByteSizedBy{ann.args[0]};
      } else if (ann.name == "null_terminated") {
        member.size_type = NullTerminated{};
      } else {
        return absl::InvalidArgumentError(absl::Substitute(
            "field $0: annotation $1 is not supported for record members",
            member.name, ann.name));
      }
    }

    // We only expect ptr_dir to be kSandboxOpaque, if specified at all.
    if (member.ptr_dir.has_value() &&
        member.ptr_dir != PointerDir::kSandboxOpaque) {
      return absl::InvalidArgumentError(absl::Substitute(
          "field $0: only no pointer direction, or kSandboxOpaque, is supported"
          " for record members",
          member.name));
    }

    record_annotations.member_annotations.push_back(std::move(member));
  }

  if (record_annotations.member_annotations.empty()) {
    return absl::InvalidArgumentError(absl::Substitute(
        "No sandbox annotations found for record $0", record_name.str()));
  }

  ABSL_RETURN_IF_ERROR(CheckShadowRecordMatchesRealRecord(decl));

  record_annotations_[record_name] = std::move(record_annotations);
  return absl::OkStatus();
}


}  // namespace sapi
