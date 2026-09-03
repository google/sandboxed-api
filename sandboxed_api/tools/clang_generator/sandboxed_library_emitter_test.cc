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

#include "sandboxed_api/tools/clang_generator/sandboxed_library_emitter.h"

#include <memory>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "sandboxed_api/tools/clang_generator/frontend_action_test_util.h"
#include "sandboxed_api/tools/clang_generator/generator.h"

namespace sapi {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::testing::HasSubstr;
using ::testing::Not;

class SandboxedLibraryEmitterTest : public FrontendActionTest {};

TEST_F(SandboxedLibraryEmitterTest, SandboxeeThunkNotUsed) {
  GeneratorOptions options;
  options.name = "MyLib";
  SandboxedLibraryEmitter emitter;
  ASSERT_THAT(
      RunFrontendAction(R"(
        extern "C" int func_with_thunk(int a);

        [[clang::annotate("sandbox", "sandboxee_thunk", "func_with_thunk")]]
        int func_with_thunk_thunk(int a) {
          return func_with_thunk(a) + 1;
        }
      )",
                        std::make_unique<GeneratorAction>(&emitter, &options)),
      IsOk());

  ASSERT_EQ(emitter.PostParseAllFiles(), absl::OkStatus());
  absl::StatusOr<std::string> host_src = emitter.EmitHostSrc(options);
  ASSERT_THAT(host_src, IsOk());
  EXPECT_THAT(*host_src, HasSubstr("api.sapi_wrapper_func_with_thunk("));

  absl::StatusOr<std::string> sandboxee_src = emitter.EmitSandboxeeSrc(options);
  ASSERT_THAT(sandboxee_src, IsOk());
  // Problem: wrapper for func_with_thunk is generated, and thunk is not used.
  EXPECT_THAT(*sandboxee_src, HasSubstr("sapi_wrapper_func_with_thunk"));
  // The wrapper should call func_with_thunk, not func_with_thunk_thunk
  EXPECT_THAT(*sandboxee_src,
              HasSubstr("int sapi_ret_val = func_with_thunk(a);"));
  EXPECT_THAT(*sandboxee_src, Not(HasSubstr("func_with_thunk_thunk")));
}

TEST_F(SandboxedLibraryEmitterTest, StdStringPointerSupport) {
  GeneratorOptions options;
  options.name = "MyLib";
  SandboxedLibraryEmitter emitter;
  ASSERT_THAT(
      RunFrontendAction(R"(
        namespace std {
        template<typename T> class basic_string {};
        using string = basic_string<char>;
        }
        extern "C" void func_with_str_ptr(std::string* str [[clang::annotate("sandbox", "inout_ptr")]]);
      )",
                        std::make_unique<GeneratorAction>(&emitter, &options)),
      IsOk());

  ASSERT_EQ(emitter.PostParseAllFiles(), absl::OkStatus());
  absl::StatusOr<std::string> host_src = emitter.EmitHostSrc(options);
  ASSERT_THAT(host_src, IsOk());
  EXPECT_THAT(*host_src,
              HasSubstr("std::unique_ptr<sapi::v::LenVal> sapi_tmp_str;"));
  EXPECT_THAT(*host_src, HasSubstr("if (str != nullptr)"));
  EXPECT_THAT(
      *host_src,
      HasSubstr("sapi_tmp_str = std::make_unique<sapi::v::LenVal>(str->data(), "
                "str->size());"));

  absl::StatusOr<std::string> sandboxee_src = emitter.EmitSandboxeeSrc(options);
  ASSERT_THAT(sandboxee_src, IsOk());
  EXPECT_THAT(*sandboxee_src,
              HasSubstr("std::string* sapi_tmp_ptr_str = nullptr;"));
  EXPECT_THAT(*sandboxee_src, HasSubstr("std::string sapi_tmp_str;"));
  EXPECT_THAT(
      *sandboxee_src,
      HasSubstr("sapi_tmp_str = "
                "std::string(reinterpret_cast<char*>(str->data), str->size);"));
  EXPECT_THAT(*sandboxee_src, HasSubstr("sapi_tmp_ptr_str = &sapi_tmp_str;"));
  EXPECT_THAT(*sandboxee_src,
              HasSubstr("func_with_str_ptr(sapi_tmp_ptr_str);"));
}

using SandboxedLibraryEmitterErrorTest = SandboxedLibraryEmitterTest;

TEST_F(SandboxedLibraryEmitterErrorTest, ReturnsIntAnnotatedUninitialized) {
  GeneratorOptions options;
  options.name = "MyLib";
  SandboxedLibraryEmitter emitter;
  EXPECT_THAT(RunFrontendAction(
                  R"cc(
                    extern "C"
                        [[clang::annotate("sandbox", "uninitialized")]] int
                        returns_int_uninitialized(int);
                  )cc",
                  std::make_unique<GeneratorAction>(&emitter, &options)),
              // TODO: update `RunFrontendAction` to include diagnostic detail
              // with a DiagnosticConsumer.
              StatusIs(absl::StatusCode::kUnknown,
                       HasSubstr("Tool invocation failed")));
}

TEST_F(SandboxedLibraryEmitterErrorTest, PointerParamAnnotatedUninitialized) {
  GeneratorOptions options;
  options.name = "MyLib";
  SandboxedLibraryEmitter emitter;
  EXPECT_THAT(RunFrontendAction(
                  R"cc(
                    extern "C" void pointer_param_uninitialized(
                        // raise an error for this case, since the user could
                        // have just changed inout -> out, instead of using
                        // uninitialized
                        int* p [[clang::annotate("sandbox", "inout_ptr")]]
                        [[clang::annotate("sandbox", "uninitialized")]]);
                  )cc",
                  std::make_unique<GeneratorAction>(&emitter, &options)),
              StatusIs(absl::StatusCode::kUnknown,
                       HasSubstr("Tool invocation failed")));
}

TEST_F(SandboxedLibraryEmitterErrorTest,
       CallbackReturnsIntAnnotatedUninitialized) {
  GeneratorOptions options;
  options.name = "MyLib";
  SandboxedLibraryEmitter emitter;
  EXPECT_THAT(
      RunFrontendAction(
          R"cc(
            extern "C" void fp_callback_returns_int_uninitialized(
                [[clang::annotate("sandbox",
                                  "uninitialized")]] int (*callback)(int));
          )cc",
          std::make_unique<GeneratorAction>(&emitter, &options)),
      StatusIs(absl::StatusCode::kUnknown,
               HasSubstr("Tool invocation failed")));
}

TEST_F(SandboxedLibraryEmitterErrorTest,
       CallbackParamAliasesNonExistentOuterParam) {
  GeneratorOptions options;
  options.name = "MyLib";
  SandboxedLibraryEmitter emitter;
  EXPECT_THAT(
      RunFrontendAction(
          R"cc(
            extern "C" void callback_param_aliases_non_existent(
                void (*cb)(void* cb_closure
                           [[clang::annotate("sandbox", "host_opaque_ptr")]]
                           [[clang::annotate("sandbox", "alias_ptr",
                                             "non_existent")]],
                           int val),
                int val);
          )cc",
          std::make_unique<GeneratorAction>(&emitter, &options)),
      StatusIs(absl::StatusCode::kUnknown,
               HasSubstr("Tool invocation failed")));
}

TEST_F(SandboxedLibraryEmitterErrorTest,
       CallbackParamAliasesNonPointerOuterParam) {
  GeneratorOptions options;
  options.name = "MyLib";
  SandboxedLibraryEmitter emitter;
  EXPECT_THAT(
      RunFrontendAction(
          R"cc(
            extern "C" void callback_param_aliases_non_pointer(
                void (*cb)(void* cb_closure
                           [[clang::annotate("sandbox", "host_opaque_ptr")]]
                           [[clang::annotate("sandbox", "alias_ptr",
                                             "not_a_pointer")]],
                           int val),
                int not_a_pointer);
          )cc",
          std::make_unique<GeneratorAction>(&emitter, &options)),
      StatusIs(absl::StatusCode::kUnknown,
               HasSubstr("Tool invocation failed")));
}

TEST_F(SandboxedLibraryEmitterErrorTest, CallbackParamAliasesGlobalVariable) {
  GeneratorOptions options;
  options.name = "MyLib";
  SandboxedLibraryEmitter emitter;
  EXPECT_THAT(
      RunFrontendAction(
          R"cc(
            extern char* optarg;

            extern "C" void callback_param_aliases_libc_global(void (*cb)(
                char* optarg_alias [[clang::annotate("sandbox", "in_ptr")]]
                [[clang::annotate("sandbox", "alias_ptr", "optarg")]],
                int val));
          )cc",
          std::make_unique<GeneratorAction>(&emitter, &options)),
      StatusIs(absl::StatusCode::kUnknown,
               HasSubstr("Tool invocation failed")));
}

TEST_F(SandboxedLibraryEmitterErrorTest,
       CallbackParamAliasesNonHostOpaqueOuterParam) {
  GeneratorOptions options;
  options.name = "MyLib";
  SandboxedLibraryEmitter emitter;
  EXPECT_THAT(
      RunFrontendAction(
          R"cc(
            extern "C" void callback_alias_non_host_opaque(
                void (*cb)(void* cb_closure
                           [[clang::annotate("sandbox", "host_opaque_ptr")]]
                           [[clang::annotate("sandbox", "alias_ptr",
                                             "input_ptr")]],
                           int val),
                const int* input_ptr [[clang::annotate("sandbox", "in_ptr")]]);
          )cc",
          std::make_unique<GeneratorAction>(&emitter, &options)),
      StatusIs(absl::StatusCode::kUnknown,
               HasSubstr("Tool invocation failed")));
}

}  // namespace
}  // namespace sapi
