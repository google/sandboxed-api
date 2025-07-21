// Copyright 2020 Google LLC
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

#include "sandboxed_api/tools/clang_generator/generator.h"

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/btree_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Type.h"
#include "clang/AST/TypeLoc.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/FileEntry.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/CompilerInvocation.h"
#include "clang/Lex/Lexer.h"
#include "clang/Lex/PPCallbacks.h"
#include "clang/Lex/PreprocessorOptions.h"
#include "clang/Serialization/PCHContainerOperations.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Config/llvm-config.h"
#include "sandboxed_api/tools/clang_generator/diagnostics.h"
#include "sandboxed_api/tools/clang_generator/emitter_base.h"
#include "sandboxed_api/tools/clang_generator/includes.h"

namespace sapi {
namespace {

// Replaces the file extension of a path name.
std::string ReplaceFileExtension(absl::string_view path,
                                 absl::string_view new_extension) {
  size_t last_slash = path.rfind('/');
  size_t pos = path.rfind('.', last_slash);
  if (pos != absl::string_view::npos && last_slash != absl::string_view::npos) {
    pos += last_slash;
  }
  return absl::StrCat(path.substr(0, pos), new_extension);
}

}  // namespace

// IncludeRecorder is a clang preprocessor callback that records includes from
// the input files.
class IncludeRecorder : public clang::PPCallbacks {
 public:
  IncludeRecorder(std::string current_file,
                  clang::SourceManager& source_manager,
                  absl::btree_map<std::string, std::vector<IncludeInfo>>&
                      collected_includes)
      : current_file_(std::move(current_file)),
        source_manager_(source_manager),
        collected_includes_(collected_includes) {}

  // Will only record direct includes from the input file.
  void InclusionDirective(
      clang::SourceLocation hash_loc, const clang::Token& include_tok,
      clang::StringRef filename, bool is_angled,
      clang::CharSourceRange filename_range, clang::OptionalFileEntryRef file,
      clang::StringRef search_path, clang::StringRef relative_path,
#if LLVM_VERSION_MAJOR >= 19
      const clang::Module* suggested_module, bool module_imported,
#else
      const clang::Module* imported,
#endif
      clang::SrcMgr::CharacteristicKind file_type) override;

 private:
  // The input file which is currently being processed.
  std::string current_file_;

  // The source manager for the current file.
  clang::SourceManager& source_manager_;

  // Reference to the map of collected includes, owned by the BaseEmitter.
  absl::btree_map<std::string, std::vector<IncludeInfo>>& collected_includes_;
};

void IncludeRecorder::InclusionDirective(
    clang::SourceLocation hash_loc, const clang::Token& include_tok,
    clang::StringRef filename, bool is_angled,
    clang::CharSourceRange filename_range, clang::OptionalFileEntryRef file,
    clang::StringRef search_path, clang::StringRef relative_path,
#if LLVM_VERSION_MAJOR >= 19
    const clang::Module* suggested_module, bool module_imported,
#else
    const clang::Module* imported,
#endif
    clang::SrcMgr::CharacteristicKind file_type) {

  // Filter out includes which are not directly included from the input files
  // and remove includes which have a path component (e.g. <foo/bar>).
  // TODO b/402670257 - Handle cases where a path component is present.
  if (current_file_ ==
          RemoveHashLocationMarker(hash_loc.printToString(source_manager_)) &&
      !relative_path.contains("/")) {
    // file is of type OptionalFileEntryRef, ensure it has a value, otherwise
    // skip the include.
    if (!file.has_value()) {
      return;
    }
    collected_includes_[current_file_].push_back({
        .include = filename.str(),
        .file = *file,
        .is_angled = is_angled,
        .is_system_header = (file_type == clang::SrcMgr::C_System),
    });
  }
}

std::string GetOutputFilename(absl::string_view source_file) {
  return ReplaceFileExtension(source_file, ".sapi.h");
}

// Called during HandleTranslationUnit
bool GeneratorASTVisitor::VisitTypeDecl(clang::TypeDecl* decl) {
  type_collector_.RecordOrderedTypeDeclarations(decl);
  return true;
}

bool GeneratorASTVisitor::VisitFunctionDecl(clang::FunctionDecl* decl) {
  if (decl->isCXXClassMember() ||  // Skip classes
      decl->isTemplated()          // Skip function templates
  ) {
    return true;
  }

  // Skip C++ functions unless generating a symbol list.
  if (!decl->isExternC() && !options_.symbol_list_gen) {
    return true;
  }

  // Process either all function or just the requested ones
  bool sandbox_all_functions = options_.function_names.empty();
  if (!sandbox_all_functions &&
      !options_.function_names.contains(ToStringView(decl->getName()))) {
    return true;
  }

  // Skip Abseil internal functions when all functions are requested. This still
  // allows them to be specified explicitly.
  if (sandbox_all_functions &&
      absl::StartsWith(decl->getQualifiedNameAsString(), "AbslInternal")) {
    return true;
  }

  clang::SourceManager& source_manager =
      decl->getASTContext().getSourceManager();
  clang::SourceLocation decl_start =
      source_manager.getExpansionLoc(decl->getBeginLoc());

  // Skip functions from system headers when all functions are requested. Like
  // above, they can still explicitly be specified.
  if (sandbox_all_functions && source_manager.isInSystemHeader(decl_start)) {
    return true;
  }

  if (sandbox_all_functions) {
    const std::string filename(absl::StripPrefix(
        ToStringView(source_manager.getFilename(decl_start)), "./"));
    if (options_.limit_scan_depth && !options_.in_files.contains(filename)) {
      return true;
    }
  }

  functions_.push_back(decl);

  // Store the return type and parameters for type collection.
  type_collector_.CollectRelatedTypes(decl->getDeclaredReturnType());

  for (const clang::ParmVarDecl* param : decl->parameters()) {
    type_collector_.CollectRelatedTypes(param->getType());
  }

  return true;
}

void GeneratorASTConsumer::HandleTranslationUnit(clang::ASTContext& context) {
  if (!visitor_.TraverseDecl(context.getTranslationUnitDecl())) {
    ReportFatalError(context.getDiagnostics(),
                     context.getTranslationUnitDecl()->getBeginLoc(),
                     "AST traversal exited early.");
    return;
  }

  for (auto& [parse_ctx, includes] : emitter_.collected_includes()) {
    for (auto& include : includes) {
      emitter_.AddIncludes(&include);
    }
  }

  emitter_.AddTypeDeclarations(visitor_.type_collector().GetTypeDeclarations());

  for (clang::FunctionDecl* func : visitor_.functions()) {
    absl::Status status = emitter_.AddFunction(func);
    if (!status.ok()) {
      clang::SourceLocation loc =
          GetDiagnosticLocationFromStatus(status).value_or(func->getBeginLoc());
      if (absl::IsCancelled(status)) {
        ReportWarning(context.getDiagnostics(), loc, status.message());
        continue;
      }
      ReportFatalError(context.getDiagnostics(), loc, status.message());
      break;
    }
  }
}

// Called at the start of processing an input file, before
// HandleTranslationUnit.
bool GeneratorAction::BeginSourceFileAction(clang::CompilerInstance& ci) {
  ci.getPreprocessor().addPPCallbacks(std::make_unique<IncludeRecorder>(
      ci.getSourceManager()
          .getFileEntryRefForID(ci.getSourceManager().getMainFileID())
          ->getName()
          .str(),
      ci.getSourceManager(), emitter_.collected_includes()));
  return true;
}

bool GeneratorFactory::runInvocation(
    std::shared_ptr<clang::CompilerInvocation> invocation,
    clang::FileManager* files,
    std::shared_ptr<clang::PCHContainerOperations> pch_container_ops,
    clang::DiagnosticConsumer* diag_consumer) {
  auto& options = invocation->getPreprocessorOpts();
  // Explicitly ask to define __clang_analyzer__ macro.
  options.SetUpStaticAnalyzer = true;
  for (const auto& def : {
           // Enable code to detect whether it is being SAPI-ized
           "__SAPI__",
           // TODO: b/222241644 - Figure out how to deal with intrinsics
           // properly.
           // Note: The definitions below just need to parse, they don't need to
           //       compile into useful code.
           // 3DNow!
           "__builtin_ia32_femms=[](){}",
           "__builtin_ia32_pavgusb=",
           "__builtin_ia32_pf2id=",
           "__builtin_ia32_pfacc=",
           "__builtin_ia32_pfadd=",
           "__builtin_ia32_pfcmpeq=",
           "__builtin_ia32_pfcmpge=",
           "__builtin_ia32_pfcmpgt=",
           "__builtin_ia32_pfmax=",
           "__builtin_ia32_pfmin=",
           "__builtin_ia32_pfmul=",
           "__builtin_ia32_pfrcp=",
           "__builtin_ia32_pfrcpit1=",
           "__builtin_ia32_pfrcpit2=",
           "__builtin_ia32_pfrsqrt=",
           "__builtin_ia32_pfrsqit1=",
           "__builtin_ia32_pfsub=",
           "__builtin_ia32_pfsubr=",
           "__builtin_ia32_pi2fd=",
           "__builtin_ia32_pmulhrw=",
           "__builtin_ia32_pf2iw=",
           "__builtin_ia32_pfnacc=",
           "__builtin_ia32_pfpnacc=",
           "__builtin_ia32_pi2fw=",
           "__builtin_ia32_pswapdsf=",
           "__builtin_ia32_pswapdsi=",
           // Intel
           "__builtin_ia32_cvtsbf162ss_32=[](auto)->long long{return 0;}",
           "__builtin_ia32_paddsb128=",
           "__builtin_ia32_paddsb256=",
           "__builtin_ia32_paddsb512=",
           "__builtin_ia32_paddsw128=",
           "__builtin_ia32_paddsw256=",
           "__builtin_ia32_paddsw512=",
           "__builtin_ia32_paddusb128=",
           "__builtin_ia32_paddusb256=",
           "__builtin_ia32_paddusb512=",
           "__builtin_ia32_paddusw128=",
           "__builtin_ia32_paddusw256=",
           "__builtin_ia32_paddusw512=",
           "__builtin_ia32_psubsb128=",
           "__builtin_ia32_psubsb256=",
           "__builtin_ia32_psubsb512=",
           "__builtin_ia32_psubsw128=",
           "__builtin_ia32_psubsw256=",
           "__builtin_ia32_psubsw512=",
           "__builtin_ia32_psubusb128=",
           "__builtin_ia32_psubusb256=",
           "__builtin_ia32_psubusb512=",
           "__builtin_ia32_psubusw128=",
           "__builtin_ia32_psubusw256=",
           "__builtin_ia32_psubusw512=",
           "__builtin_ia32_reduce_add_d512=[](auto)->long long{return 0;}",
           "__builtin_ia32_reduce_add_q512=[](auto)->long long{return 0;}",
           "__builtin_ia32_reduce_mul_d512=[](auto)->long long{return 0;}",
           "__builtin_ia32_reduce_mul_q512=[](auto)->long long{return 0;}",

           // SSE2
           "__builtin_ia32_cvtpd2pi=[](auto)->long long{return 0;}",
           "__builtin_ia32_cvtpi2pd=[](auto) -> __m128{return {0, 0, 0, 0};}",
           "__builtin_ia32_cvtpi2ps=[](auto, auto)->__m128{return {0, 0, 0, "
           "0};}",
           "__builtin_ia32_cvtps2pi=[](auto)->long long{return 0;}",
           "__builtin_ia32_cvttpd2pi=[](auto)->long long{return 0;}",
           "__builtin_ia32_cvttps2pi=[](auto)->long long{return 0;}",
           "__builtin_ia32_maskmovq=",
           "__builtin_ia32_movntq=",
           "__builtin_ia32_pabsb=",
           "__builtin_ia32_pabsd=",
           "__builtin_ia32_pabsw=",
           "__builtin_ia32_packssdw=",
           "__builtin_ia32_packsswb=",
           "__builtin_ia32_packuswb=",
           "__builtin_ia32_paddb=",
           "__builtin_ia32_paddd=",
           "__builtin_ia32_paddq=",
           "__builtin_ia32_paddsb=",
           "__builtin_ia32_paddsw=",
           "__builtin_ia32_paddusb=",
           "__builtin_ia32_paddusw=",
           "__builtin_ia32_paddw=",
           "__builtin_ia32_pand=",
           "__builtin_ia32_pandn=",
           "__builtin_ia32_pavgb=",
           "__builtin_ia32_pavgw=",
           "__builtin_ia32_pcmpeqb=",
           "__builtin_ia32_pcmpeqd=",
           "__builtin_ia32_pcmpeqw=",
           "__builtin_ia32_pcmpgtb=",
           "__builtin_ia32_pcmpgtd=",
           "__builtin_ia32_pcmpgtw=",
           "__builtin_ia32_phaddd=",
           "__builtin_ia32_phaddsw=",
           "__builtin_ia32_phaddw=",
           "__builtin_ia32_phsubd=",
           "__builtin_ia32_phsubsw=",
           "__builtin_ia32_phsubw=",
           "__builtin_ia32_pmaddubsw=",
           "__builtin_ia32_pmaddwd=",
           "__builtin_ia32_pmaxsw=",
           "__builtin_ia32_pmaxub=",
           "__builtin_ia32_pminsw=",
           "__builtin_ia32_pminub=",
           "__builtin_ia32_pmovmskb=[](auto)->long long{return 0;}",
           "__builtin_ia32_pmulhrsw=",
           "__builtin_ia32_pmulhuw=",
           "__builtin_ia32_pmulhw=",
           "__builtin_ia32_pmullw=",
           "__builtin_ia32_pmuludq=",
           "__builtin_ia32_por=",
           "__builtin_ia32_psadbw=",
           "__builtin_ia32_pshufb=",
           "__builtin_ia32_psignb=",
           "__builtin_ia32_psignd=",
           "__builtin_ia32_psignw=",
           "__builtin_ia32_pslld=",
           "__builtin_ia32_pslldi=[](auto, auto)->long long{return 0;}",
           "__builtin_ia32_psllq=",
           "__builtin_ia32_psllqi=[](auto, auto)->long long{return 0;}",
           "__builtin_ia32_psllw=",
           "__builtin_ia32_psllwi=[](auto, auto)->long long{return 0;}",
           "__builtin_ia32_psrad=",
           "__builtin_ia32_psradi=[](auto, auto)->long long{return 0;}",
           "__builtin_ia32_psraw=",
           "__builtin_ia32_psrawi=[](auto, auto)->long long{return 0;}",
           "__builtin_ia32_psrld=",
           "__builtin_ia32_psrldi=[](auto, auto)->long long{return 0;}",
           "__builtin_ia32_psrlq=",
           "__builtin_ia32_psrlqi=[](auto, auto)->long long{return 0;}",
           "__builtin_ia32_psrlw=",
           "__builtin_ia32_psrlwi=[](auto, auto)->long long{return 0;}",
           "__builtin_ia32_psubb=",
           "__builtin_ia32_psubd=",
           "__builtin_ia32_psubq=",
           "__builtin_ia32_psubsb=",
           "__builtin_ia32_psubsw=",
           "__builtin_ia32_psubusb=",
           "__builtin_ia32_psubusw=",
           "__builtin_ia32_psubw=",
           "__builtin_ia32_punpckhbw=",
           "__builtin_ia32_punpckhdq=",
           "__builtin_ia32_punpckhwd=",
           "__builtin_ia32_punpcklbw=",
           "__builtin_ia32_punpckldq=",
           "__builtin_ia32_punpcklwd=",
           "__builtin_ia32_pxor=",
           "__builtin_ia32_vec_ext_v2si=",
           "__builtin_ia32_vec_init_v2si=[](auto, auto)->long long{return 0;}",
           "__builtin_ia32_vec_init_v4hi=[](auto, auto, auto, auto)->long "
           "long{return 0;}",
           "__builtin_ia32_vec_init_v8qi=[](auto, auto, auto, auto, auto, "
           "auto, auto, auto)->long long{return 0;}",
           // AVX
           "__builtin_ia32_vpopcntb_128=",
           "__builtin_ia32_vpopcntb_256=",
           "__builtin_ia32_vpopcntb_512=",
           "__builtin_ia32_vpopcntd_128=",
           "__builtin_ia32_vpopcntd_256=",
           "__builtin_ia32_vpopcntd_512=",
           "__builtin_ia32_vpopcntq_128=",
           "__builtin_ia32_vpopcntq_256=",
           "__builtin_ia32_vpopcntq_512=",
           "__builtin_ia32_vpopcntw_128=",
           "__builtin_ia32_vpopcntw_256=",
           "__builtin_ia32_vpopcntw_512=",
           "__builtin_ia32_vcvttpd2dqs256_round_mask=[](auto, auto, auto, "
           "auto)->__m128i {return __m128i();}",
           "__builtin_ia32_vcvttpd2udqs256_round_mask=[](auto, auto, auto, "
           "auto)->__m128i {return __m128i();}",
           "__builtin_ia32_vcvttpd2qqs256_round_mask=[](auto, auto, auto, "
           "auto)->__m256i {return __m256i();}",
           "__builtin_ia32_vcvttpd2uqqs256_round_mask=[](auto, auto, auto, "
           "auto)->__m256i {return __m256i();}",
           "__builtin_ia32_vcvttps2dqs256_round_mask=[](auto, auto, auto, "
           "auto)->__m256i {return __m256i();}",
           "__builtin_ia32_vcvttps2udqs256_round_mask=[](auto, auto, auto, "
           "auto)->__m256i {return __m256i();}",
           "__builtin_ia32_vcvttps2qqs256_round_mask=[](auto, auto, auto, "
           "auto)->__m256i {return __m256i();}",
           "__builtin_ia32_vcvttps2uqqs256_round_mask=[](auto, auto, auto, "
           "auto)->__m256i {return __m256i();}",
           "__builtin_ia32_vcvttps2uqqs512_round_mask=[](auto, auto, auto, "
           "auto)->__m512i {return __m512i();}",

           // Trick <prfchwintrin.h> from being included by defining its header
           // guard.
           "__PRFCHWINTRIN_H",
       }) {
    options.addMacroDef(def);
    // To avoid code to include header with compiler intrinsics, undefine a few
    // key pre-defines.
    for (
        const auto& undef : {
            // ARM ISA (see
            // https://developer.arm.com/documentation/101028/0010/Feature-test-macros)
            "__ARM_NEON",
            "__ARM_NEON__",
            // Intel
            "__AVX__",
            "__AVX2__",
            "__AVX512BW__",
            "__AVX512CD__",
            "__AVX512DQ__",
            "__AVX512F__",
            "__AVX512VL__",
            "__SSE__",
            "__SSE2__",
            "__SSE2_MATH__",
            "__SSE3__",
            "__SSE4_1__",
            "__SSE4_2__",
            "__SSE_MATH__",
            "__SSSE3__",
        }) {
      options.addMacroUndef(undef);
    }
  }
  return FrontendActionFactory::runInvocation(std::move(invocation), files,
                                              std::move(pch_container_ops),
                                              diag_consumer);
}

}  // namespace sapi
