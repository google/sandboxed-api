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

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
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
#include "clang/AST/Type.h"
#include "clang/Format/Format.h"
#include "clang/Tooling/Core/Replacement.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"
#include "sandboxed_api/tools/clang_generator/generator.h"
#include "sandboxed_api/tools/clang_generator/includes.h"
#include "sandboxed_api/tools/clang_generator/types.h"

namespace sapi {
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
  return pretty;
}

// Returns the spelling for a given declaration to be emitted to the final
// header. This may rewrite declarations (like converting typedefs to using,
// etc.). Note that the resulting spelling will need to be wrapped inside a
// namespace if the original declaration was inside one.
std::string GetSpelling(const clang::Decl* decl) {
  // TODO(cblichmann): Make types nicer
  //   - Rewrite typedef to using
  //   - Rewrite function pointers using std::add_pointer_t<>;

  // Handle typedef/alias declarations.
  if (const auto* typedef_name_decl =
          llvm::dyn_cast<clang::TypedefNameDecl>(decl)) {
    // Special case: anonymous enum/struct declarations.
    // We recreate how the anonymous declaration most likely looked in code
    // here. For example:
    // 'typedef enum { kRed, kGreen, kBlue } Color;'
    // will be spelled as is, and not as a separate anonymous enum declaration,
    // followed by a 'typedef enum Color Color;'
    if (auto* tag_decl = typedef_name_decl->getAnonDeclWithTypedefName()) {
      return absl::StrCat("typedef ", PrintDecl(tag_decl), " ",
                          ToStringView(typedef_name_decl->getName()));
    }

    // Special case: pointer/reference to anonymous struct/union.
    // For example, the declaration
    // 'typedef struct { void* opaque; } png_image, *png_imagep;'
    // will result in two typedef being emitted:
    //   typedef struct { void* opaque; } png_image;
    //   typedef png_image * png_imagep;
    // The first one will be emitted due to the case above.
    // TODO b/402658788 - This does not handle rare cases where a typedef is
    //                    only declaring a pointer:
    //   typedef struct { int member; } *MyStructPtr;
    if (clang::QualType canonical_type =
            typedef_name_decl->getUnderlyingType().getCanonicalType();
        IsPointerOrReference(canonical_type) &&
        // Skip function pointers/refs and array types. For arrays, we need to
        // check the final underlying pointee type.
        !canonical_type->isFunctionPointerType() &&
        !canonical_type->isFunctionReferenceType() &&
        !GetFinalPointeeType(canonical_type)->isArrayType()) {
      return absl::StrCat("typedef ", canonical_type.getAsString(),
                          ToStringView(typedef_name_decl->getName()));
    }

    // Regular case: any other typedef or alias declarations.
    return PrintDecl(typedef_name_decl);
  }

  // Handle enum/struct/class/union declarations.
  if (const auto* tag_decl = llvm::dyn_cast<clang::TagDecl>(decl)) {
    // Handle enum declarations.
    if (const auto* enum_decl = llvm::dyn_cast<clang::EnumDecl>(tag_decl)) {
      return PrintDecl(enum_decl);
    }

    // Handle struct/class/union declarations.
    if (const auto* record_decl = llvm::dyn_cast<clang::CXXRecordDecl>(decl)) {
      // Declarations that are:
      //  - not forward declarations
      //  - aggregates (C-like struct, or struct with default initializers)
      //  - Plain Old Data (POD) type
      //  - types without no user-defined methods (including constructors)
      if (record_decl->hasDefinition() && record_decl->isAggregate() &&
          (record_decl->isPOD() || record_decl->methods().empty())) {
        return PrintDecl(decl);
      }

      // Remaining declarations that are:
      //  - forward declarations
      //  - non-aggregate types
      //  - non-POD types with user-defined methods
      std::string spelling = PrintRecordTemplateArguments(record_decl);
      switch (record_decl->getTagKind()) {
        case clang::TagTypeKind::Struct:
          absl::StrAppend(&spelling, "struct ");
          break;
        case clang::TagTypeKind::Class:
          absl::StrAppend(&spelling, "class ");
          break;
        case clang::TagTypeKind::Union:
          absl::StrAppend(&spelling, "union ");
          break;
        case clang::TagTypeKind::Interface:
        default:
          llvm::errs() << "CXXRecordDecl has unexpected 'TagTypeKind' "
                       << static_cast<int>(record_decl->getTagKind()) << ".\n"
                       << PrintDecl(decl);
          return "";
      }
      return absl::StrCat(spelling, ToStringView(record_decl->getName()));
    }
  }

  // Fallback to cover any other case not individually handled above.
  return PrintDecl(decl);
}

}  // namespace

std::string GetIncludeGuard(absl::string_view filename) {
  if (filename.empty()) {
    static auto* bit_gen = new absl::BitGen();
    return absl::StrCat(
        // Copybara will transform this string. This is intentional.
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

absl::string_view MapCSystemHeaderToCpp(absl::string_view header) {
  // TODO(cblichmann): Once we're on C++20 everywhere, this function can be made
  //                   constexpr and use std::binary_search().
  static auto* c_to_cpp_system =
      new absl::flat_hash_map<std::string, std::string>({
          // go/keep-sorted start
          {"assert.h", "cassert"},     {"complex.h", "ccomplex"},
          {"ctype.h", "cctype"},       {"errno.h", "cerrno"},
          {"fenv.h", "cfenv"},         {"float.h", "cfloat"},
          {"inttypes.h", "cinttypes"}, {"iso646.h", "ciso646"},
          {"limits.h", "climits"},     {"locale.h", "clocale"},
          {"math.h", "cmath"},         {"setjmp.h", "csetjmp"},
          {"signal.h", "csignal"},     {"stdalign.h", "cstdalign"},
          {"stdarg.h", "cstdarg"},     {"stdbool.h", "cstdbool"},
          {"stddef.h", "cstddef"},     {"stdint.h", "cstdint"},
          {"stdio.h", "cstdio"},       {"stdlib.h", "cstdlib"},
          {"string.h", "cstring"},     {"tgmath.h", "ctgmath"},
          {"time.h", "ctime"},         {"uchar.h", "cuchar"},
          {"wchar.h", "cwchar"},       {"wctype.h", "cwctype"},
          // go/keep-sorted end
      });
  auto found = c_to_cpp_system->find(header);
  return found != c_to_cpp_system->end() ? found->second : header;
}

void EmitterBase::EmitType(absl::string_view ns_name,
                           clang::TypeDecl* type_decl) {
  if (!type_decl) {
    return;
  }

  std::string spelling = GetSpelling(type_decl);

  if (const auto& [it, inserted] = rendered_types_.emplace(ns_name, spelling);
      inserted) {
    rendered_types_ordered_.push_back(&*it);
  }
}

void EmitterBase::AddTypeDeclarations(
    const std::vector<NamespacedTypeDecl>& type_decls) {
  for (const auto& [ns_name, type_decl] : type_decls) {
    EmitType(ns_name, type_decl);
  }
}

std::string EmitSystemInclude(const IncludeInfo& info) {
  if (!info.is_system_header ||
      // Skip non-angled includes. These should occur rarely, if ever.
      !info.is_angled) {
    return "";
  }
  return absl::StrCat("#include <", MapCSystemHeaderToCpp(info.include), ">");
}

void EmitterBase::AddIncludes(const IncludeInfo* include) {
  if (std::string directive = EmitSystemInclude(*include); !directive.empty()) {
    rendered_includes_ordered_.insert(std::move(directive));
  }
}

}  // namespace sapi
