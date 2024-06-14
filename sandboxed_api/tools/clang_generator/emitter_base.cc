// Copyright 2024 Google LLC
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

#include "sandboxed_api/tools/clang_generator/emitter_base.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/QualTypeNames.h"
#include "clang/Format/Format.h"
#include "clang/Tooling/Core/Replacement.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"
#include "sandboxed_api/tools/clang_generator/generator.h"

namespace sapi {

// Text template arguments:
//   1. Include for embedded sandboxee objects
constexpr absl::string_view kEmbedInclude = R"(#include "%1$s_embed.h"

)";

// Text template arguments:
//   1. Class name
//   2. Embedded object identifier
constexpr absl::string_view kEmbedClassTemplate = R"(
// Sandbox with embedded sandboxee and default policy
class %1$s : public ::sapi::Sandbox {
 public:
  %1$s()
      : ::sapi::Sandbox([]() {
          static auto* fork_client_context =
              new ::sapi::ForkClientContext(%2$s_embed_create());
          return fork_client_context;
        }()) {}
};

)";

// Sandboxed API class template.
// Text template arguments:
//   1. Class name
constexpr absl::string_view kClassHeaderTemplate = R"(
// Sandboxed API
class %1$s {
 public:
  explicit %1$s(::sapi::Sandbox* sandbox) : sandbox_(sandbox) {}

  ABSL_DEPRECATED("Call sandbox() instead")
  ::sapi::Sandbox* GetSandbox() const { return sandbox(); }
  ::sapi::Sandbox* sandbox() const { return sandbox_; }
)";

// Sandboxed API class template footer.
constexpr absl::string_view kClassFooterTemplate = R"(
 private:
  ::sapi::Sandbox* sandbox_;
};
)";

namespace internal {

absl::StatusOr<std::string> ReformatGoogleStyle(const std::string& filename,
                                                const std::string& code,
                                                int column_limit) {
  // Configure code style based on Google style, but enforce pointer alignment
  clang::format::FormatStyle style =
      clang::format::getGoogleStyle(clang::format::FormatStyle::LK_Cpp);
  style.DerivePointerAlignment = false;
  style.PointerAlignment = clang::format::FormatStyle::PAS_Left;
  if (column_limit >= 0) {
    style.ColumnLimit = column_limit;
  }

  clang::tooling::Replacements replacements = clang::format::reformat(
      style, code, llvm::ArrayRef(clang::tooling::Range(0, code.size())),
      filename);

  llvm::Expected<std::string> formatted_header =
      clang::tooling::applyAllReplacements(code, replacements);
  if (!formatted_header) {
    return absl::InternalError(llvm::toString(formatted_header.takeError()));
  }
  return *formatted_header;
}

}  // namespace internal

namespace {

// Returns the namespace components of a declaration's qualified name.
std::vector<std::string> GetNamespacePath(const clang::TypeDecl* decl) {
  std::vector<std::string> comps;
  for (const auto* ctx = decl->getDeclContext(); ctx; ctx = ctx->getParent()) {
    if (const auto* nd = llvm::dyn_cast<clang::NamespaceDecl>(ctx)) {
      comps.push_back(nd->getName().str());
    }
  }
  std::reverse(comps.begin(), comps.end());
  return comps;
}

// Returns the template arguments for a given record.
std::string PrintRecordTemplateArguments(const clang::CXXRecordDecl* record) {
  const auto* template_inst_decl = record->getTemplateInstantiationPattern();
  if (!template_inst_decl) {
    return "";
  }
  const auto* template_decl = template_inst_decl->getDescribedClassTemplate();
  if (!template_decl) {
    return "";
  }
  const auto* template_params = template_decl->getTemplateParameters();
  if (!template_params) {
    return "";
  }
  clang::ASTContext& context = record->getASTContext();
  std::vector<std::string> params;
  params.reserve(template_params->size());
  for (const auto& template_param : *template_params) {
    if (const auto* p =
            llvm::dyn_cast<clang::NonTypeTemplateParmDecl>(template_param)) {
      // TODO(cblichmann): Should be included by CollectRelatedTypes().
      params.push_back(clang::TypeName::getFullyQualifiedName(
          p->getType().getDesugaredType(context), context,
          context.getPrintingPolicy()));
    } else {  // Also covers template template parameters
      params.push_back("typename");
    }
    absl::StrAppend(&params.back(), " /*",
                    std::string(template_param->getName()), "*/");
  }
  return absl::StrCat("template <", absl::StrJoin(params, ", "), ">");
}

// Serializes the given Clang AST declaration back into compilable source code.
std::string PrintDecl(const clang::Decl* decl) {
  std::string pretty;
  llvm::raw_string_ostream os(pretty);
  decl->print(os);
  return os.str();
}

// Returns the spelling for a given declaration will be emitted to the final
// header. This may rewrite declarations (like converting typedefs to using,
// etc.). Note that the resulting spelling will need to be wrapped inside a
// namespace if the original declaration was inside one.
std::string GetSpelling(const clang::Decl* decl) {
  // TODO(cblichmann): Make types nicer
  //   - Rewrite typedef to using
  //   - Rewrite function pointers using std::add_pointer_t<>;

  if (const auto* typedef_decl = llvm::dyn_cast<clang::TypedefNameDecl>(decl)) {
    // Special case: anonymous enum/struct
    if (auto* tag_decl = typedef_decl->getAnonDeclWithTypedefName()) {
      return absl::StrCat("typedef ", PrintDecl(tag_decl), " ",
                          ToStringView(typedef_decl->getName()));
    }
  }

  if (const auto* record_decl = llvm::dyn_cast<clang::CXXRecordDecl>(decl)) {
    if (record_decl->hasDefinition() &&
        // Aggregates capture all C-like structs, but also structs with
        // non-static members that have default initializers.
        record_decl->isAggregate() &&
        // Make sure to skip types with user-defined methods (including
        // constructors).
        record_decl->methods().empty()) {
      return PrintDecl(decl);
    }
    // For unsupported types or types with no definition, only emit a forward
    // declaration.
    return absl::StrCat(PrintRecordTemplateArguments(record_decl),
                        record_decl->isClass() ? "class " : "struct ",
                        ToStringView(record_decl->getName()));
  }
  return PrintDecl(decl);
}

}  // namespace

std::string GetIncludeGuard(absl::string_view filename) {
  if (filename.empty()) {
    static auto* bit_gen = new absl::BitGen();
    return absl::StrCat(
        // Copybara will transform the string. This is intentional.
        "SANDBOXED_API_GENERATED_HEADER_",
        absl::AsciiStrToUpper(absl::StrCat(
            absl::Hex(absl::Uniform<uint64_t>(*bit_gen), absl::kZeroPad16))),
        "_");
  }

  constexpr absl::string_view kUnderscorePrefix = "SAPI_";
  std::string guard;
  guard.reserve(filename.size() + kUnderscorePrefix.size() + 1);
  for (auto c : filename) {
    if (absl::ascii_isalpha(c)) {
      guard += absl::ascii_toupper(c);
      continue;
    }
    if (guard.empty()) {
      guard = kUnderscorePrefix;
    }
    if (absl::ascii_isdigit(c)) {
      guard += c;
    } else if (guard.back() != '_') {
      guard += '_';
    }
  }
  if (!absl::EndsWith(guard, "_")) {
    guard += '_';
  }
  return guard;
}

void EmitterBase::EmitType(clang::TypeDecl* type_decl) {
  if (!type_decl) {
    return;
  }

  // Skip types defined in system headers.
  // TODO(cblichmann): Instead of this and the hard-coded entities below, we
  //                   should map types and add the correct (system) headers to
  //                   the generated output.
  if (type_decl->getASTContext().getSourceManager().isInSystemHeader(
          type_decl->getBeginLoc())) {
    return;
  }

  const std::vector<std::string> ns_path = GetNamespacePath(type_decl);
  std::string ns_name;
  if (!ns_path.empty()) {
    const auto& ns_root = ns_path.front();
    // Filter out declarations from the C++ standard library, from SAPI itself
    // and from other well-known namespaces.
    if (ns_root == "std" || ns_root == "__gnu_cxx" || ns_root == "sapi") {
      return;
    }
    if (ns_root == "absl") {
      // Skip Abseil internal namespaces
      if (ns_path.size() > 1 && absl::EndsWith(ns_path[1], "_internal")) {
        return;
      }
      // Skip types from Abseil that will already be included in the generated
      // header.
      if (auto name = ToStringView(type_decl->getName());
          name == "CordMemoryAccounting" || name == "Duration" ||
          name == "LogEntry" || name == "LogSeverity" || name == "Span" ||
          name == "StatusCode" || name == "StatusToStringMode" ||
          name == "SynchLocksHeld" || name == "SynchWaitParams" ||
          name == "Time" || name == "string_view" || name == "tid_t") {
        return;
      }
    }
    // Skip Protocol Buffers namespaces
    if (ns_root == "google" && ns_path.size() > 1 && ns_path[1] == "protobuf") {
      return;
    }
    ns_name = absl::StrJoin(ns_path, "::");
  }

  std::string spelling = GetSpelling(type_decl);
  if (const auto& [it, inserted] = rendered_types_.emplace(ns_name, spelling);
      inserted) {
    rendered_types_ordered_.push_back(&*it);
  }
}

void EmitterBase::AddTypeDeclarations(
    const std::vector<clang::TypeDecl*>& type_decls) {
  for (clang::TypeDecl* type_decl : type_decls) {
    EmitType(type_decl);
  }
}

}  // namespace sapi
