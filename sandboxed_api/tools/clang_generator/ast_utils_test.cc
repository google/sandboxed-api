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

// Tests for the helpers that turn hand-written annotated source into the text
// that gets pasted into the generated output.
//
// `GetThunkSource` captures a thunk definition, removing the SANDBOX_*
// annotations and renaming it. `GetSourceWithoutAnnotations` does the same
// removal for a host state variable, without the rename. Both are driven by
// the attributes clang parsed: annotation names in comments and string
// literals and macro arguments that contain parentheses are handled
// syntactically, and references to renamed thunks or recursive self-references
// are rejected via the AST.
//
// `GetFunctionDeclaration` re-prints the signature from the AST instead of
// copying source, so it is covered separately.

#include "sandboxed_api/tools/clang_generator/ast_utils.h"

#include <memory>
#include <string>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/status_macros.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/LLVM.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"
#include "sandboxed_api/testing.h"
#include "sandboxed_api/tools/clang_generator/diagnostics.h"
#include "sandboxed_api/tools/clang_generator/frontend_action_test_util.h"

namespace sapi {
namespace {

using ::absl_testing::StatusIs;
using ::testing::HasSubstr;

// Subset of the public annotation macros from `sandboxed_api/annotations.h`.
// Defining them inline keeps test snippets self-contained while still
// exercising macro expansion ranges.
constexpr absl::string_view kAnnotationMacros = R"(
#define SANDBOX_IN_PTR [[clang::annotate("sandbox", "in_ptr")]]
#define SANDBOX_ELEM_SIZED_BY(elem_size_arg) \
  [[clang::annotate("sandbox", "elem_sized_by", #elem_size_arg)]]
#define SANDBOX_HOST_THUNK(func_name) \
  [[clang::annotate("sandbox", "host_thunk", #func_name)]]
#define SANDBOX_HOST_STATE_VAR [[clang::annotate("sandbox", "host_state_var")]]
)";

template <typename DeclT, typename Callback>
class ExtractConsumer
    : public clang::ASTConsumer,
      public clang::RecursiveASTVisitor<ExtractConsumer<DeclT, Callback>> {
 public:
  ExtractConsumer(absl::string_view target_name, Callback& callback,
                  bool& found, absl::Status& callback_status)
      : target_name_(target_name),
        callback_(callback),
        found_(found),
        callback_status_(callback_status) {}

  void HandleTranslationUnit(clang::ASTContext& context) override {
    this->TraverseDecl(context.getTranslationUnitDecl());
  }

  bool VisitNamedDecl(clang::NamedDecl* decl) {
    const auto* typed = llvm::dyn_cast<DeclT>(decl);
    if (typed == nullptr || typed->getNameAsString() != target_name_) {
      return true;
    }
    callback_status_ = callback_(typed);
    found_ = true;
    return false;
  }

 private:
  absl::string_view target_name_;
  Callback& callback_;
  bool& found_;
  absl::Status& callback_status_;
};

template <typename DeclT, typename Callback>
class ExtractAction : public clang::ASTFrontendAction {
 public:
  ExtractAction(absl::string_view target_name, Callback& callback, bool& found,
                absl::Status& callback_status)
      : target_name_(target_name),
        callback_(callback),
        found_(found),
        callback_status_(callback_status) {}

  std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
      clang::CompilerInstance&, llvm::StringRef) override {
    return std::make_unique<ExtractConsumer<DeclT, Callback>>(
        target_name_, callback_, found_, callback_status_);
  }

 private:
  absl::string_view target_name_;
  Callback& callback_;
  bool& found_;
  absl::Status& callback_status_;
};

class AstCaptureTest : public ::sapi::FrontendActionTest {
 protected:
  struct Captured {
    std::string source;
    std::string declaration;
  };

  template <typename DeclT, typename Callback>
  absl::Status RunOnDecl(absl::string_view code, absl::string_view target_name,
                         Callback callback) {
    bool found = false;
    absl::Status callback_status;

    ABSL_RETURN_IF_ERROR(
        RunFrontendAction(absl::StrCat(kAnnotationMacros, code),
                          std::make_unique<ExtractAction<DeclT, Callback>>(
                              target_name, callback, found, callback_status)));
    if (!found) {
      return absl::NotFoundError(
          absl::StrCat("Declaration '", target_name, "' not found"));
    }
    return callback_status;
  }

  absl::StatusOr<Captured> CaptureThunk(absl::string_view target_name,
                                        absl::string_view new_name,
                                        absl::string_view code) {
    Captured captured;
    ABSL_RETURN_IF_ERROR(RunOnDecl<clang::FunctionDecl>(
        code, target_name, [&](const clang::FunctionDecl* decl) {
          ABSL_ASSIGN_OR_RETURN(std::string source,
                                ast::GetThunkSource(decl, new_name));
          captured = Captured{
              .source = std::move(source),
              .declaration = ast::GetFunctionDeclaration(decl),
          };
          return absl::OkStatus();
        }));
    return captured;
  }

  absl::StatusOr<Captured> CaptureThunk(absl::string_view target_name,
                                        absl::string_view code) {
    return CaptureThunk(target_name, /*new_name=*/"", code);
  }

  absl::StatusOr<std::string> CaptureVar(absl::string_view target_name,
                                         absl::string_view code) {
    std::string source;
    ABSL_RETURN_IF_ERROR(RunOnDecl<clang::VarDecl>(
        code, target_name, [&](const clang::VarDecl* decl) {
          source = ast::GetSourceWithoutAnnotations(decl);
          return absl::OkStatus();
        }));
    return source;
  }
};

TEST_F(AstCaptureTest, RenamesTheDefinitionAndExcludesLeadingThunkAttribute) {
  // SANDBOX_HOST_THUNK expands to a leading attribute outside the function's
  // source range, the definition is renamed to `new_name`, and the call to the
  // sandboxee thunk (`decode_sandbox`) is left untouched.
  SAPI_ASSERT_OK_AND_ASSIGN(Captured captured,
                            CaptureThunk("decode_host", "decode", R"(
int decode_sandbox(int n);
SANDBOX_HOST_THUNK(decode)
int decode_host(int n) {
  return decode_sandbox(n);
})"));
  EXPECT_EQ(captured.source, R"(int decode(int n) {
  return decode_sandbox(n);
})");
}

TEST_F(AstCaptureTest, RejectsSelfReferenceInRenamedThunk) {
  absl::StatusOr<Captured> captured = CaptureThunk("fact_host", "fact", R"(
SANDBOX_HOST_THUNK(fact)
int fact_host(int n) {
  return n <= 1 ? 1 : n * fact_host(n - 1);
})");
  EXPECT_THAT(captured, StatusIs(absl::StatusCode::kInvalidArgument,
                                 HasSubstr("Thunk 'fact_host' cannot be "
                                           "referenced")));
  EXPECT_TRUE(GetDiagnosticLocationFromStatus(captured.status()).has_value());
}

TEST_F(AstCaptureTest, RejectsExternalReferenceToRenamedThunk) {
  // Referencing a renamed thunk from anywhere in the translation unit would
  // leave a dangling symbol once the definition is renamed to `new_name`.
  absl::StatusOr<Captured> captured = CaptureThunk("decode_host", "decode", R"(
SANDBOX_HOST_THUNK(decode)
int decode_host(int n) {
  return n;
}
int helper(int n) {
  return decode_host(n);
})");
  EXPECT_THAT(captured, StatusIs(absl::StatusCode::kInvalidArgument,
                                 HasSubstr("Thunk 'decode_host' cannot be "
                                           "referenced")));
  EXPECT_TRUE(GetDiagnosticLocationFromStatus(captured.status()).has_value());
}

TEST_F(AstCaptureTest, RejectsSelfReferenceInUnrenamedThunk) {
  absl::StatusOr<Captured> captured = CaptureThunk("fact_sandbox", R"(
int fact_sandbox(int n) {
  return n <= 1 ? 1 : n * fact_sandbox(n - 1);
})");
  EXPECT_THAT(captured, StatusIs(absl::StatusCode::kInvalidArgument,
                                 HasSubstr("Thunk 'fact_sandbox' cannot be "
                                           "referenced")));
  EXPECT_TRUE(GetDiagnosticLocationFromStatus(captured.status()).has_value());
}

TEST_F(AstCaptureTest, LeavesTheNameInACommentAlone) {
  SAPI_ASSERT_OK_AND_ASSIGN(Captured captured,
                            CaptureThunk("decode_host", "decode", R"(
int decode_host(int n) {
  // decode_host( is mentioned here.
  return 0;
})"));
  EXPECT_EQ(captured.source, R"(int decode(int n) {
  // decode_host( is mentioned here.
  return 0;
})");
}

TEST_F(AstCaptureTest, KeepsTheNameWhenNoNewNameIsGiven) {
  // Sandboxee thunks are captured without renaming and may be called by the
  // corresponding host thunk.
  SAPI_ASSERT_OK_AND_ASSIGN(Captured captured,
                            CaptureThunk("decode_sandbox", R"(
int decode_sandbox(int n) {
  return 0;
}
int decode_host(int n) {
  return decode_sandbox(n);
})"));
  EXPECT_EQ(captured.source, R"(int decode_sandbox(int n) {
  return 0;
})");
}

TEST_F(AstCaptureTest, ReturnsEmptyForADeclarationWithoutABody) {
  SAPI_ASSERT_OK_AND_ASSIGN(Captured captured,
                            CaptureThunk("decode_sandbox", R"(
int decode_sandbox(int n);
)"));
  EXPECT_EQ(captured.source, "");
}

TEST_F(AstCaptureTest, RemovesParameterAndInlineFunctionAnnotations) {
  SAPI_ASSERT_OK_AND_ASSIGN(Captured captured,
                            CaptureThunk("decode_host", "decode", R"(
int decode_host SANDBOX_HOST_THUNK(decode)(
    const char* src SANDBOX_IN_PTR SANDBOX_ELEM_SIZED_BY(n), int n) {
  return 0;
})"));
  EXPECT_EQ(captured.source, R"(int decode (
    const char* src  , int n) {
  return 0;
})");
}

TEST_F(AstCaptureTest, RemovesAnAnnotationWithANestedCallArgument) {
  // Removing the macro's expansion range strips the entire macro invocation
  // even when its argument contains nested parentheses.
  SAPI_ASSERT_OK_AND_ASSIGN(Captured captured,
                            CaptureThunk("decode_sandbox", R"(
int count(int n);
int decode_sandbox(const char* src SANDBOX_ELEM_SIZED_BY(count(n)), int n) {
  return 0;
})"));
  EXPECT_EQ(captured.source,
            R"(int decode_sandbox(const char* src , int n) {
  return 0;
})");
}

TEST_F(AstCaptureTest, PreservesNonSandboxAttributes) {
  SAPI_ASSERT_OK_AND_ASSIGN(Captured captured,
                            CaptureThunk("decode_sandbox", R"(
int decode_sandbox([[maybe_unused]] const char* src SANDBOX_IN_PTR,
                   [[clang::annotate("other")]] int n) {
  return 0;
})"));
  EXPECT_EQ(captured.source,
            R"(int decode_sandbox([[maybe_unused]] const char* src ,
                   [[clang::annotate("other")]] int n) {
  return 0;
})");
}

TEST_F(AstCaptureTest, LeavesAnnotationNamesInCommentsAndLiteralsAlone) {
  // Only parsed sandbox attributes are removed, so prose and string literals
  // survive.
  SAPI_ASSERT_OK_AND_ASSIGN(Captured captured,
                            CaptureThunk("decode_sandbox", R"(
void log(const char* s);
int decode_sandbox(int n) {
  // Callers must pass SANDBOX_IN_PTR here.
  log("SANDBOX_IN_PTR");
  return 0;
})"));
  EXPECT_EQ(captured.source, R"(int decode_sandbox(int n) {
  // Callers must pass SANDBOX_IN_PTR here.
  log("SANDBOX_IN_PTR");
  return 0;
})");
}

TEST_F(AstCaptureTest, DeclarationHasTheAnnotateAttributesRemoved) {
  SAPI_ASSERT_OK_AND_ASSIGN(Captured captured, CaptureThunk("decode", R"(
int decode(const char* src SANDBOX_IN_PTR SANDBOX_ELEM_SIZED_BY(n), int n) {
  return 0;
})"));
  // Note that `extern "C"` is not part of the re-printed signature; the
  // generator adds the linkage itself.
  EXPECT_EQ(captured.declaration, "int decode(const char *src, int n)");
}

TEST_F(AstCaptureTest, CapturesAVariableWithALeadingAnnotation) {
  // SANDBOX_HOST_STATE_VAR written before the declaration lies outside the
  // declaration's source range. The emitter appends the trailing semicolon.
  SAPI_ASSERT_OK_AND_ASSIGN(std::string source, CaptureVar("params", R"(
template <typename K, typename V> struct Map {};
SANDBOX_HOST_STATE_VAR
Map<unsigned long, int> params;
)"));
  EXPECT_EQ(source, "Map<unsigned long, int> params");
}

TEST_F(AstCaptureTest, RemovesInlineVariableAnnotation) {
  // When SANDBOX_HOST_STATE_VAR is placed after the variable name, it lies
  // inside `decl->getSourceRange()` and is stripped via its expansion range.
  SAPI_ASSERT_OK_AND_ASSIGN(std::string source, CaptureVar("counter", R"(
int counter SANDBOX_HOST_STATE_VAR = 7;
)"));
  EXPECT_EQ(source, "int counter  = 7");
}

}  // namespace
}  // namespace sapi
