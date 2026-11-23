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

#include <cstddef>
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
#include "sandboxed_api/testing.h"
#include "sandboxed_api/tools/clang_generator/frontend_action_test_util.h"
#include "sandboxed_api/tools/clang_generator/generator.h"

namespace sapi {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::testing::AllOf;
using ::testing::HasSubstr;
using ::testing::Not;
using ::testing::StartsWith;

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
              HasSubstr("auto sapi_ret_val = func_with_thunk(a);"));
  EXPECT_THAT(*sandboxee_src, Not(HasSubstr("func_with_thunk_thunk")));
}

TEST_F(SandboxedLibraryEmitterTest,
       SandboxeeAndHostThunkWithoutSandboxFuncsOrExternC) {
  GeneratorOptions options;
  options.name = "MyLib";
  options.sandboxed_library_gen = true;
  SandboxedLibraryEmitter emitter;
  ASSERT_THAT(
      RunFrontendAction(R"(
        const char* sandbox_funcs_1[] = {"func_with_thunk"};

        extern "C" int func_with_thunk(int a);
        extern "C" int unlisted_func(int a);

        [[clang::annotate("sandbox", "sandboxee_thunk", "func_with_thunk")]]
        int func_with_thunk_sandbox(int a) {
          return func_with_thunk(a) + 1;
        }

        [[clang::annotate("sandbox", "host_thunk", "func_with_thunk")]]
        int func_with_thunk_host(int a) {
          return func_with_thunk_sandbox(a);
        }
      )",
                        std::make_unique<GeneratorAction>(&emitter, &options)),
      IsOk());

  ASSERT_THAT(emitter.PostParseAllFiles(), IsOk());

  absl::StatusOr<std::string> sandboxee_hdr = emitter.EmitSandboxeeHdr(options);
  ASSERT_THAT(sandboxee_hdr, IsOk());
  EXPECT_THAT(*sandboxee_hdr,
              HasSubstr("sapi_wrapper_func_with_thunk_sandbox("));
  EXPECT_THAT(*sandboxee_hdr, Not(HasSubstr("sapi_wrapper_func_with_thunk(")));
  EXPECT_THAT(*sandboxee_hdr, Not(HasSubstr("unlisted_func")));

  absl::StatusOr<std::string> sandboxee_src = emitter.EmitSandboxeeSrc(options);
  ASSERT_THAT(sandboxee_src, IsOk());
  EXPECT_THAT(*sandboxee_src,
              HasSubstr("extern \"C\" int func_with_thunk_sandbox(int a)"));
  EXPECT_THAT(*sandboxee_src,
              HasSubstr("auto sapi_ret_val = func_with_thunk_sandbox(a);"));

  absl::StatusOr<std::string> host_src = emitter.EmitHostSrc(options);
  ASSERT_THAT(host_src, IsOk());
  EXPECT_THAT(*host_src,
              HasSubstr("extern \"C\" int func_with_thunk_sandbox(int a);"));
  EXPECT_THAT(*host_src,
              HasSubstr("extern \"C\" int func_with_thunk(int a) { return "
                        "func_with_thunk_sandbox(a); }"));
  EXPECT_THAT(*host_src,
              HasSubstr("api.sapi_wrapper_func_with_thunk_sandbox("));
  EXPECT_THAT(*host_src, Not(HasSubstr("unlisted_func")));
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

// Runs `SandboxedLibraryEmitter` on `code` in a fresh virtual filesystem and
// returns the generated `(host_src, sandboxee_src)` pair.
absl::StatusOr<std::pair<std::string, std::string>> EmitHostAndSandboxeeSrc(
    absl::string_view code) {
  GeneratorOptions options;
  options.name = "MyLib";
  // Enable `sandboxed_library_gen` so the AST visitor traverses C++ wrapper
  // functions in addition to `extern "C"` declarations.
  options.sandboxed_library_gen = true;
  SandboxedLibraryEmitter emitter;
  ABSL_RETURN_IF_ERROR(RunClangTool(
      {"tool", "-fsyntax-only", "--std=c++20", "-I.", "-Wno-error", "input.cc"},
      {{"input.cc", std::string(code)}},
      std::make_unique<GeneratorAction>(&emitter, &options)));
  ABSL_RETURN_IF_ERROR(emitter.PostParseAllFiles());
  ABSL_ASSIGN_OR_RETURN(std::string host_src, emitter.EmitHostSrc(options));
  ABSL_ASSIGN_OR_RETURN(std::string sandboxee_src,
                        emitter.EmitSandboxeeSrc(options));
  return std::make_pair(std::move(host_src), std::move(sandboxee_src));
}

// Verifies that annotating callbacks via `typedef` declarations (including
// `host_opaque_ptr` closures, input/output buffers, `lifetime_outer_call`
// returned buffers, and typedef reuse across multiple library functions)
// generates the same sandboxee wrapper code as writing callback parameter
// annotations inline and using a `typedef` only for the pointer-returning
// `alloc` callback, while registering reversible `typedef` callbacks through
// `sapi_register_callback_*` on the host.
TEST_F(SandboxedLibraryEmitterTest,
       CallbackTypedefGeneratesSameCodeAsInlineForm) {
  SAPI_ASSERT_OK_AND_ASSIGN(const auto with_typedef, EmitHostAndSandboxeeSrc(R"(
    typedef int (*stream_write_cb)(
        void* user_data [[clang::annotate("sandbox", "host_opaque_ptr")]],
        const unsigned char* data
            [[clang::annotate("sandbox", "in_ptr")]]
            [[clang::annotate("sandbox", "elem_sized_by", "len")]],
        unsigned long len,
        unsigned long* written [[clang::annotate("sandbox", "out_ptr")]]);

    [[clang::annotate("sandbox", "lifetime_outer_call")]]
    [[clang::annotate("sandbox", "elem_sized_by", "size")]]
    [[clang::annotate("sandbox", "uninitialized")]]
    typedef unsigned char* (*chunk_alloc_cb)(unsigned long size);

    extern "C"
    [[clang::annotate("sandbox", "alias_callback_return", "alloc")]]
    unsigned char* compress_stream(
        stream_write_cb write,
        chunk_alloc_cb alloc,
        void* user_data [[clang::annotate("sandbox", "host_opaque_ptr")]]);

    extern "C" int decompress_stream(
        stream_write_cb write,
        void* user_data [[clang::annotate("sandbox", "host_opaque_ptr")]]);
  )"));
  SAPI_ASSERT_OK_AND_ASSIGN(const auto inline_form, EmitHostAndSandboxeeSrc(R"(
    [[clang::annotate("sandbox", "lifetime_outer_call")]]
    [[clang::annotate("sandbox", "elem_sized_by", "size")]]
    [[clang::annotate("sandbox", "uninitialized")]]
    typedef unsigned char* (*chunk_alloc_cb)(unsigned long size);

    extern "C"
    [[clang::annotate("sandbox", "alias_callback_return", "alloc")]]
    unsigned char* compress_stream(
        int (*write)(
            void* user_data [[clang::annotate("sandbox", "host_opaque_ptr")]],
            const unsigned char* data
                [[clang::annotate("sandbox", "in_ptr")]]
                [[clang::annotate("sandbox", "elem_sized_by", "len")]],
            unsigned long len,
            unsigned long* written [[clang::annotate("sandbox", "out_ptr")]]),
        chunk_alloc_cb alloc,
        void* user_data [[clang::annotate("sandbox", "host_opaque_ptr")]]);

    extern "C" int decompress_stream(
        int (*write)(
            void* user_data [[clang::annotate("sandbox", "host_opaque_ptr")]],
            const unsigned char* data
                [[clang::annotate("sandbox", "in_ptr")]]
                [[clang::annotate("sandbox", "elem_sized_by", "len")]],
            unsigned long len,
            unsigned long* written [[clang::annotate("sandbox", "out_ptr")]]),
        void* user_data [[clang::annotate("sandbox", "host_opaque_ptr")]]);
  )"));

  const size_t inline_sandboxee_end = inline_form.second.rfind("}\n");
  ASSERT_NE(inline_sandboxee_end, std::string::npos);
  EXPECT_THAT(with_typedef.second, StartsWith(inline_form.second.substr(
                                       0, inline_sandboxee_end + 2)));
  EXPECT_THAT(with_typedef.first,
              HasSubstr("sapi_register_callback_stream_write_cb"));
  EXPECT_THAT(with_typedef.first, HasSubstr("sapi_bind_thunk_stream_write_cb"));
  EXPECT_THAT(with_typedef.second, HasSubstr("sapi_invoke_stream_write_cb"));
  EXPECT_THAT(inline_form.first,
              HasSubstr("std::optional<sapi::v::Callback> sapi_cb_write"));
  EXPECT_THAT(inline_form.first,
              Not(HasSubstr("sapi_bind_thunk_stream_write_cb")));
  EXPECT_THAT(inline_form.second,
              Not(HasSubstr("sapi_invoke_stream_write_cb")));
}

// Verifies that redeclaring an unannotated C header callback `typedef` with
// sandbox annotations generates the same host and sandboxee code as declaring
// the annotated `typedef` once, and emits reverse-callback invokers and thunk
// pools only for reversible (scalar-returning) callback typedefs.
TEST_F(SandboxedLibraryEmitterTest,
       RedeclaringUnannotatedCallbackTypedefAttachesAnnotations) {
  SAPI_ASSERT_OK_AND_ASSIGN(const auto redeclared, EmitHostAndSandboxeeSrc(R"(
    typedef int (*stream_writer)(void* other_ctx, const unsigned char*,
                                 unsigned long, unsigned long*);
    typedef unsigned char* (*chunk_allocator)(unsigned long);

    typedef int (*stream_writer)(
        void* user_data [[clang::annotate("sandbox", "host_opaque_ptr")]],
        const unsigned char* data
            [[clang::annotate("sandbox", "in_ptr")]]
            [[clang::annotate("sandbox", "elem_sized_by", "len")]],
        unsigned long len,
        unsigned long* written [[clang::annotate("sandbox", "out_ptr")]]);

    [[clang::annotate("sandbox", "lifetime_outer_call")]]
    [[clang::annotate("sandbox", "elem_sized_by", "size")]]
    [[clang::annotate("sandbox", "uninitialized")]]
    typedef unsigned char* (*chunk_allocator)(unsigned long size);

    extern "C"
    [[clang::annotate("sandbox", "alias_callback_return", "alloc")]]
    unsigned char* compress_stream(
        stream_writer write,
        chunk_allocator alloc,
        void* user_data [[clang::annotate("sandbox", "host_opaque_ptr")]]);

    extern "C" int decompress_stream(
        stream_writer write,
        void* user_data [[clang::annotate("sandbox", "host_opaque_ptr")]]);
  )"));
  SAPI_ASSERT_OK_AND_ASSIGN(const auto single_decl, EmitHostAndSandboxeeSrc(R"(
    typedef int (*stream_writer)(
        void* user_data [[clang::annotate("sandbox", "host_opaque_ptr")]],
        const unsigned char* data
            [[clang::annotate("sandbox", "in_ptr")]]
            [[clang::annotate("sandbox", "elem_sized_by", "len")]],
        unsigned long len,
        unsigned long* written [[clang::annotate("sandbox", "out_ptr")]]);

    [[clang::annotate("sandbox", "lifetime_outer_call")]]
    [[clang::annotate("sandbox", "elem_sized_by", "size")]]
    [[clang::annotate("sandbox", "uninitialized")]]
    typedef unsigned char* (*chunk_allocator)(unsigned long size);

    extern "C"
    [[clang::annotate("sandbox", "alias_callback_return", "alloc")]]
    unsigned char* compress_stream(
        stream_writer write,
        chunk_allocator alloc,
        void* user_data [[clang::annotate("sandbox", "host_opaque_ptr")]]);

    extern "C" int decompress_stream(
        stream_writer write,
        void* user_data [[clang::annotate("sandbox", "host_opaque_ptr")]]);
  )"));

  EXPECT_EQ(redeclared.first, single_decl.first);
  EXPECT_EQ(redeclared.second, single_decl.second);
  EXPECT_THAT(redeclared.first, HasSubstr("sapi_bind_thunk_stream_writer"));
  EXPECT_THAT(redeclared.second, HasSubstr("sapi_invoke_stream_writer"));
  EXPECT_THAT(redeclared.first,
              Not(HasSubstr("sapi_bind_thunk_chunk_allocator")));
  EXPECT_THAT(redeclared.second, HasSubstr("compress_stream"));
  EXPECT_THAT(redeclared.second, HasSubstr("decompress_stream"));
}

// Verifies that when an unannotated library header declares a function taking
// an inline callback parameter (`void (*)(void*, const unsigned char**, ...)`)
// and the annotation file redeclares that function with an inline callback
// whose inner parameters use a pointee typedef and/or sandbox annotations,
// `ConvertFunctionToIR` upgrades the parameter to the redeclared `ParmVarDecl`
// and generates the same code as a single annotated declaration.
TEST_F(SandboxedLibraryEmitterTest,
       RedeclaringFunctionWithInlineCallbackPointeeTypedefAttachesAnnotations) {
  SAPI_ASSERT_OK_AND_ASSIGN(const auto redeclared, EmitHostAndSandboxeeSrc(R"(
    extern "C" void register_sax(
        void (*on_start)(void*, const unsigned char**, const int*,
                         unsigned long));
    extern "C" void register_pointee_only(
        void (*on_attr)(const unsigned char* const*));

    typedef const unsigned char* xml_str_pointee
        [[clang::annotate("sandbox", "in_ptr")]]
        [[clang::annotate("sandbox", "null_terminated")]];

    extern "C" void register_sax(
        [[clang::annotate("sandbox", "callback_idempotent")]]
        void (*on_start)(
            void* ctx [[clang::annotate("sandbox", "host_opaque_ptr")]],
            xml_str_pointee* atts
                [[clang::annotate("sandbox", "in_ptr")]]
                [[clang::annotate("sandbox", "null_terminated")]],
            const int* spans
                [[clang::annotate("sandbox", "in_ptr")]]
                [[clang::annotate("sandbox", "elem_sized_by", "count")]],
            unsigned long count));
    extern "C" void register_pointee_only(
        void (*on_attr)(const xml_str_pointee* single_str));
  )"));
  SAPI_ASSERT_OK_AND_ASSIGN(const auto single_decl, EmitHostAndSandboxeeSrc(R"(
    typedef const unsigned char* xml_str_pointee
        [[clang::annotate("sandbox", "in_ptr")]]
        [[clang::annotate("sandbox", "null_terminated")]];

    extern "C" void register_sax(
        [[clang::annotate("sandbox", "callback_idempotent")]]
        void (*on_start)(
            void* ctx [[clang::annotate("sandbox", "host_opaque_ptr")]],
            xml_str_pointee* atts
                [[clang::annotate("sandbox", "in_ptr")]]
                [[clang::annotate("sandbox", "null_terminated")]],
            const int* spans
                [[clang::annotate("sandbox", "in_ptr")]]
                [[clang::annotate("sandbox", "elem_sized_by", "count")]],
            unsigned long count));
    extern "C" void register_pointee_only(
        void (*on_attr)(const xml_str_pointee* single_str));
  )"));

  EXPECT_EQ(redeclared.first, single_decl.first);
  EXPECT_EQ(redeclared.second, single_decl.second);

  // The annotated redeclaration is also used when its inline callback
  // parameters have only sandbox attributes and no pointee typedef.
  SAPI_ASSERT_OK_AND_ASSIGN(const auto attrs_only_redeclared,
                            EmitHostAndSandboxeeSrc(R"(
    extern "C" void register_spans(void (*cb)(const int*, unsigned long));
    extern "C" void register_spans(
        void (*cb)(
            const int* spans
                [[clang::annotate("sandbox", "in_ptr")]]
                [[clang::annotate("sandbox", "elem_sized_by", "count")]],
            unsigned long count));
  )"));
  SAPI_ASSERT_OK_AND_ASSIGN(const auto attrs_only_single_decl,
                            EmitHostAndSandboxeeSrc(R"(
    extern "C" void register_spans(
        void (*cb)(
            const int* spans
                [[clang::annotate("sandbox", "in_ptr")]]
                [[clang::annotate("sandbox", "elem_sized_by", "count")]],
            unsigned long count));
  )"));
  EXPECT_EQ(attrs_only_redeclared.first, attrs_only_single_decl.first);
  EXPECT_EQ(attrs_only_redeclared.second, attrs_only_single_decl.second);

  // Unannotated C typedefs in the library header (both pointer typedefs and
  // function-pointer typedefs) must not prevent upgrading parameters or return
  // types to annotated redeclarations.
  SAPI_ASSERT_OK_AND_ASSIGN(const auto upgraded_from_raw_typedefs,
                            EmitHostAndSandboxeeSrc(R"(
    typedef const unsigned char* raw_xml_str;
    typedef void (*raw_spans_cb)(const int*, unsigned long);

    extern "C" void parse_with_raw_typedefs(
        raw_xml_str* attrs,
        void (*on_row)(raw_xml_str* row, const int* spans, unsigned long count),
        raw_spans_cb on_spans);
    extern "C" raw_spans_cb get_spans_cb();

    typedef const unsigned char* ann_xml_str
        [[clang::annotate("sandbox", "in_ptr")]]
        [[clang::annotate("sandbox", "null_terminated")]];
    typedef void (*ann_spans_cb)(
        const int* spans
            [[clang::annotate("sandbox", "in_ptr")]]
            [[clang::annotate("sandbox", "elem_sized_by", "count")]],
        unsigned long count);

    extern "C" void parse_with_raw_typedefs(
        ann_xml_str* attrs
            [[clang::annotate("sandbox", "in_ptr")]]
            [[clang::annotate("sandbox", "null_terminated")]],
        void (*on_row)(
            ann_xml_str* row
                [[clang::annotate("sandbox", "in_ptr")]]
                [[clang::annotate("sandbox", "null_terminated")]],
            const int* spans
                [[clang::annotate("sandbox", "in_ptr")]]
                [[clang::annotate("sandbox", "elem_sized_by", "count")]],
            unsigned long count),
        ann_spans_cb on_spans);
    extern "C" ann_spans_cb get_spans_cb();
  )"));
  SAPI_ASSERT_OK_AND_ASSIGN(const auto single_annotated_typedefs,
                            EmitHostAndSandboxeeSrc(R"(
    typedef const unsigned char* ann_xml_str
        [[clang::annotate("sandbox", "in_ptr")]]
        [[clang::annotate("sandbox", "null_terminated")]];
    typedef void (*ann_spans_cb)(
        const int* spans
            [[clang::annotate("sandbox", "in_ptr")]]
            [[clang::annotate("sandbox", "elem_sized_by", "count")]],
        unsigned long count);

    extern "C" void parse_with_raw_typedefs(
        ann_xml_str* attrs
            [[clang::annotate("sandbox", "in_ptr")]]
            [[clang::annotate("sandbox", "null_terminated")]],
        void (*on_row)(
            ann_xml_str* row
                [[clang::annotate("sandbox", "in_ptr")]]
                [[clang::annotate("sandbox", "null_terminated")]],
            const int* spans
                [[clang::annotate("sandbox", "in_ptr")]]
                [[clang::annotate("sandbox", "elem_sized_by", "count")]],
            unsigned long count),
        ann_spans_cb on_spans);
    extern "C" ann_spans_cb get_spans_cb();
  )"));
  EXPECT_EQ(upgraded_from_raw_typedefs.first, single_annotated_typedefs.first);
  EXPECT_EQ(upgraded_from_raw_typedefs.second,
            single_annotated_typedefs.second);
}

// Verifies that when a library header declares functions with raw function
// pointers (both as parameters and as return values) and the annotation file
// redeclares them in the same translation unit using callback typedefs, the
// emitter preserves each callback typedef's reverse invoker and host thunk pool
// and generates the same code as a single typedef-based declaration.
TEST_F(
    SandboxedLibraryEmitterTest,
    RedeclaringFunctionWithCallbackTypedefAfterRawDeclarationEmitsThunkPool) {
  SAPI_ASSERT_OK_AND_ASSIGN(const auto redeclared, EmitHostAndSandboxeeSrc(R"(
    extern "C" int (*get_adder_fn(int which))(int, int);
    extern "C" int apply_fn(int (*cb)(int, int), int a, int b);

    typedef int (*adder_cb)(int a, int b);
    typedef int (*apply_cb)(int a, int b);

    extern "C" adder_cb get_adder_fn(int which);
    extern "C" int apply_fn(apply_cb cb, int a, int b);
  )"));
  SAPI_ASSERT_OK_AND_ASSIGN(const auto single_decl, EmitHostAndSandboxeeSrc(R"(
    typedef int (*adder_cb)(int a, int b);
    typedef int (*apply_cb)(int a, int b);

    extern "C" adder_cb get_adder_fn(int which);
    extern "C" int apply_fn(apply_cb cb, int a, int b);
  )"));

  EXPECT_EQ(redeclared.first, single_decl.first);
  EXPECT_EQ(redeclared.second, single_decl.second);
  EXPECT_THAT(redeclared.first, HasSubstr("sapi_thunk_pool_adder_cb"));
  EXPECT_THAT(redeclared.second, HasSubstr("sapi_invoke_adder_cb"));
  EXPECT_THAT(redeclared.first, HasSubstr("sapi_thunk_pool_apply_cb"));
  EXPECT_THAT(redeclared.second, HasSubstr("sapi_invoke_apply_cb"));
}

// Verifies that `PostParseAllFiles` reports only callback typedefs in the main
// file that are never referenced by any callback parameter.
TEST_F(SandboxedLibraryEmitterTest, UnusedCallbackTypedefIsReported) {
  GeneratorOptions options;
  options.name = "MyLib";
  options.sandboxed_library_gen = true;
  SandboxedLibraryEmitter emitter;
  ASSERT_THAT(
      RunFrontendAction(R"(
    typedef void (*referenced_cb)(
        const unsigned char* buf [[clang::annotate("sandbox", "in_ptr")]]);

    [[clang::annotate("sandbox", "lifetime_outer_call")]]
    [[clang::annotate("sandbox", "elem_sized_by", "size")]]
    typedef unsigned char* (*unused_cb)(unsigned long size);

    extern "C" void compress(referenced_cb sink);
  )",
                        std::make_unique<GeneratorAction>(&emitter, &options)),
      IsOk());

  EXPECT_THAT(emitter.PostParseAllFiles(),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       AllOf(HasSubstr("unused callback typedefs: unused_cb"),
                             Not(HasSubstr("referenced_cb")))));
}

TEST_F(SandboxedLibraryEmitterTest,
       SelfReferentialStructAnnotationMatchesRealStruct) {
  GeneratorOptions options;
  options.name = "MyLib";
  options.sandboxed_library_gen = true;
  SandboxedLibraryEmitter emitter;
  ASSERT_THAT(
      RunFrontendAction(R"(
    struct Node {
      void (*visit)(Node* self, int val);
      Node* next;
    };

    typedef void (*node_visitor)(
        Node* self [[clang::annotate("sandbox", "sandbox_opaque_ptr")]],
        int val);

    inline void sandbox_annotation_1() {
      struct Node {
        node_visitor visit;
        [[clang::annotate("sandbox", "sandbox_opaque_ptr")]] Node* next;
      };
    }

    extern "C" void run_node(
        const Node* n [[clang::annotate("sandbox", "in_ptr")]]);
  )",
                        std::make_unique<GeneratorAction>(&emitter, &options)),
      IsOk());

  EXPECT_THAT(emitter.PostParseAllFiles(), IsOk());
}

// Verifies that a callback typedef referenced only by a function listed in
// `SANDBOX_IGNORE_FUNCS` is still counted as referenced (does not trigger an
// unused-typedef error), while pruning its reverse invoker and thunk pool.
TEST_F(SandboxedLibraryEmitterTest,
       CallbackTypedefUsedOnlyByIgnoredFunctionIsNotReported) {
  SAPI_ASSERT_OK_AND_ASSIGN(const auto emitted, EmitHostAndSandboxeeSrc(R"(
    const char* sandbox_ignore_funcs_1[] = {"compress", "ignored_with_cb",
                                            "ignored_by_val"};

    struct ByVal {
      long a;
      long b;
      long c;
    };
    typedef void (*by_val_cb)(ByVal s);
    typedef int (*ignored_reversible_cb)(const int* x);
    typedef int (*active_const_ptr_cb)(const int* x);

    [[clang::annotate("sandbox", "lifetime_outer_call")]]
    [[clang::annotate("sandbox", "elem_sized_by", "size")]]
    typedef unsigned char* (*chunk_allocator)(unsigned long size);

    extern "C" void compress(chunk_allocator alloc);
    extern "C" void ignored_with_cb(ignored_reversible_cb cb);
    extern "C" void ignored_by_val(by_val_cb cb);
    extern "C" int active_with_cb(active_const_ptr_cb cb);
  )"));

  EXPECT_THAT(emitted.first,
              Not(HasSubstr("sapi_bind_thunk_ignored_reversible_cb")));
  EXPECT_THAT(emitted.second,
              Not(HasSubstr("sapi_invoke_ignored_reversible_cb")));
  EXPECT_THAT(emitted.first, Not(HasSubstr("sapi_bind_thunk_by_val_cb")));
  EXPECT_THAT(emitted.second, Not(HasSubstr("sapi_invoke_by_val_cb")));
  EXPECT_THAT(emitted.first, HasSubstr("sapi_bind_thunk_active_const_ptr_cb"));
  EXPECT_THAT(emitted.second, HasSubstr("sapi_invoke_active_const_ptr_cb"));
}

TEST_F(SandboxedLibraryEmitterTest,
       MultiLevelPointerWithPointeeContractEmission) {
  GeneratorOptions options;
  options.name = "MyLib";
  options.sandboxed_library_gen = true;
  SandboxedLibraryEmitter emitter;
  ASSERT_THAT(
      RunFrontendAction(R"(
    struct DbHandle;

    typedef DbHandle* db_handle_pointee
        [[clang::annotate("sandbox", "sandbox_opaque_ptr")]];

    typedef int* row_pointee
        [[clang::annotate("sandbox", "inout_ptr")]]
        [[clang::annotate("sandbox", "elem_sized_by", "cols")]];

    typedef row_pointee* matrix_pointee
        [[clang::annotate("sandbox", "inout_ptr")]]
        [[clang::annotate("sandbox", "elem_sized_by", "rows")]];

    extern "C" int open_db(
        db_handle_pointee* out_db
            [[clang::annotate("sandbox", "out_ptr")]]);

    extern "C" void transform_matrix_handle(
        matrix_pointee* matrix_ptr
            [[clang::annotate("sandbox", "inout_ptr")]],
        unsigned long rows,
        unsigned long cols);
  )",
                        std::make_unique<GeneratorAction>(&emitter, &options)),
      IsOk());

  ASSERT_THAT(emitter.PostParseAllFiles(), IsOk());
  SAPI_ASSERT_OK_AND_ASSIGN(std::string host_src, emitter.EmitHostSrc(options));
  EXPECT_THAT(host_src, HasSubstr("sapi_remote_out_db"));
  EXPECT_THAT(host_src, HasSubstr("sapi_owned_matrix_ptr"));
  EXPECT_THAT(host_src, HasSubstr("sapi_tables_matrix_ptr"));
  EXPECT_THAT(host_src, HasSubstr("sapi_sync_out_matrix_ptr"));
}

TEST_F(SandboxedLibraryEmitterTest, UnusedPointeeContractIsReported) {
  GeneratorOptions options;
  options.name = "MyLib";
  options.sandboxed_library_gen = true;
  SandboxedLibraryEmitter emitter;
  ASSERT_THAT(
      RunFrontendAction(R"(
    struct DbHandle;

    typedef DbHandle* referenced_pointee
        [[clang::annotate("sandbox", "sandbox_opaque_ptr")]];

    typedef int* transitive_leaf_pointee
        [[clang::annotate("sandbox", "inout_ptr")]]
        [[clang::annotate("sandbox", "elem_sized_by", "cols")]];

    typedef transitive_leaf_pointee* outer_matrix_pointee
        [[clang::annotate("sandbox", "inout_ptr")]]
        [[clang::annotate("sandbox", "elem_sized_by", "rows")]];

    typedef int* unused_inner_pointee [[clang::annotate("sandbox", "out_ptr")]];
    typedef unused_inner_pointee* unused_outer_pointee
        [[clang::annotate("sandbox", "out_ptr")]];

    extern "C" int open_db(
        referenced_pointee* out_db
            [[clang::annotate("sandbox", "out_ptr")]]);

    extern "C" void transform_matrix(
        outer_matrix_pointee* matrix
            [[clang::annotate("sandbox", "inout_ptr")]],
        unsigned long rows,
        unsigned long cols);
  )",
                        std::make_unique<GeneratorAction>(&emitter, &options)),
      IsOk());

  EXPECT_THAT(
      emitter.PostParseAllFiles(),
      StatusIs(absl::StatusCode::kInvalidArgument,
               AllOf(HasSubstr("unused pointee typedefs: "
                               "unused_inner_pointee, unused_outer_pointee"),
                     Not(HasSubstr("referenced_pointee")),
                     Not(HasSubstr("transitive_leaf_pointee")),
                     Not(HasSubstr("outer_matrix_pointee")))));
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

TEST_F(SandboxedLibraryEmitterErrorTest, CallbackPointerParamNoDirection) {
  GeneratorOptions options;
  options.name = "MyLib";
  SandboxedLibraryEmitter emitter;
  EXPECT_THAT(RunFrontendAction(
                  R"cc(
                    extern "C" void func_with_cb(void (*cb)(int* out));
                  )cc",
                  std::make_unique<GeneratorAction>(&emitter, &options)),
              StatusIs(absl::StatusCode::kUnknown,
                       HasSubstr("Tool invocation failed")));
}

TEST_F(SandboxedLibraryEmitterErrorTest, CallbackConstOutParam) {
  GeneratorOptions options;
  options.name = "MyLib";
  SandboxedLibraryEmitter emitter;
  EXPECT_THAT(
      RunFrontendAction(
          R"cc(
            extern "C" void func_with_cb(void (*cb)(
                const int* out [[clang::annotate("sandbox", "out_ptr")]]));
          )cc",
          std::make_unique<GeneratorAction>(&emitter, &options)),
      StatusIs(absl::StatusCode::kUnknown,
               HasSubstr("Tool invocation failed")));
}

TEST_F(SandboxedLibraryEmitterErrorTest, CallbackNullTerminatedOutParam) {
  GeneratorOptions options;
  options.name = "MyLib";
  SandboxedLibraryEmitter emitter;
  EXPECT_THAT(
      RunFrontendAction(
          R"cc(
            extern "C" void func_with_cb(void (*cb)(
                char* str [[clang::annotate("sandbox", "null_terminated")]]
                [[clang::annotate("sandbox", "out_ptr")]]));
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
  ASSERT_THAT(
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
      IsOk());

  EXPECT_THAT(emitter.PostParseAllFiles(),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("alias_ptr references non-existent parameter "
                                 "non_existent")));
}

TEST_F(SandboxedLibraryEmitterErrorTest,
       CallbackParamAliasesNonPointerOuterParam) {
  GeneratorOptions options;
  options.name = "MyLib";
  SandboxedLibraryEmitter emitter;
  ASSERT_THAT(
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
      IsOk());

  EXPECT_THAT(emitter.PostParseAllFiles(),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("alias_ptr references non-pointer parameter "
                                 "not_a_pointer")));
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
  ASSERT_THAT(
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
      IsOk());

  EXPECT_THAT(emitter.PostParseAllFiles(),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("alias_ptr references parameter input_ptr "
                                 "which is not a host opaque pointer")));
}

TEST_F(SandboxedLibraryEmitterErrorTest,
       CallbackTypedefTakingSyncedStructInStructFieldIsRejected) {
  GeneratorOptions options;
  options.name = "MyLib";
  SandboxedLibraryEmitter emitter;
  ASSERT_THAT(
      RunFrontendAction(
          R"cc(
            struct Payload {
              const char* msg;
            };
            typedef void (*payload_cb)(
                Payload* p
                [[clang::annotate("sandbox", "in_ptr")]] [[clang::annotate(
                    "sandbox", "struct_sync", "b1", "{p->msg, in_ptr}", "$")]]);
            struct Holder {
              payload_cb cb;
            };
            inline void sandbox_annotation_1() {
              struct Holder {
                payload_cb cb;
              };
            }
            extern "C" void register_holder(
                Holder* h [[clang::annotate("sandbox", "in_ptr")]]);
          )cc",
          std::make_unique<GeneratorAction>(&emitter, &options)),
      StatusIs(absl::StatusCode::kUnknown,
               HasSubstr("Tool invocation failed")));
}

TEST_F(SandboxedLibraryEmitterErrorTest,
       CallbackSizingExpressionsReferencingPointerParametersAreRejected) {
  int case_idx = 0;
  auto check_rejected = [&](absl::string_view code, absl::string_view substr) {
    set_input_file(absl::StrCat("cb_size_case_", case_idx++, ".cc"));
    GeneratorOptions options;
    options.name = "MyLib";
    SandboxedLibraryEmitter emitter;
    ASSERT_THAT(RunFrontendAction(code, std::make_unique<GeneratorAction>(
                                            &emitter, &options)),
                IsOk());
    EXPECT_THAT(
        emitter.PostParseAllFiles(),
        StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr(substr)));
  };

  check_rejected(
      R"cc(
        extern "C" void run_cb(void (*cb)(
            const int* data [[clang::annotate("sandbox", "in_ptr")]]
            [[clang::annotate("sandbox", "elem_sized_by", "*len")]],
            const unsigned long* len [[clang::annotate("sandbox", "in_ptr")]]));
      )cc",
      "callback cb parameter data: callback sizing expression '*len' cannot "
      "reference non-scalar parameter len");

  check_rejected(
      R"cc(
        typedef void (*fill_cb)(
            int* dst
            [[clang::annotate("sandbox", "out_ptr")]] [[clang::annotate(
                "sandbox", "elem_sized_by_outparam", "*len", "*len")]],
            unsigned long* len [[clang::annotate("sandbox", "inout_ptr")]]);
        extern "C" fill_cb get_fill_cb();
      )cc",
      "callback typedef fill_cb parameter dst: callback sizing expression "
      "'*len' cannot reference non-scalar parameter len");

  check_rejected(
      R"cc(
        extern "C" void run_compound_cb(void (*cb)(
            const char* data [[clang::annotate("sandbox", "in_ptr")]]
            [[clang::annotate("sandbox", "byte_sized_by", "*len * 4")]],
            const unsigned long* len [[clang::annotate("sandbox", "in_ptr")]]));
      )cc",
      "callback cb parameter data: callback sizing expression '*len * 4' "
      "cannot reference non-scalar parameter len");

  check_rejected(
      R"cc(
        [[clang::annotate("sandbox", "lifetime_outer_call")]] [[clang::annotate(
            "sandbox", "elem_sized_by",
            "*out_len")]] typedef unsigned char* (*alloc_cb)(unsigned long*
                                                                 out_len
                                                             [[clang::annotate(
                                                                 "sandbox",
                                                                 "out_ptr")]]);
        extern "C" void use_alloc(alloc_cb cb);
      )cc",
      "callback cb return value: callback sizing expression '*out_len' cannot "
      "reference non-scalar parameter out_len");

  // Scalar capacity (`cap`) and unevaluated `sizeof(*dst)` remain allowed.
  set_input_file("cb_size_valid.cc");
  GeneratorOptions ok_options;
  ok_options.name = "MyLib";
  SandboxedLibraryEmitter ok_emitter;
  ASSERT_THAT(
      RunFrontendAction(
          R"cc(
            extern "C" void run_valid_cb(void (*cb)(
                int* dst [[clang::annotate("sandbox", "out_ptr")]]
                [[clang::annotate("sandbox", "byte_sized_by_outparam", "*len",
                                  "cap * sizeof(*dst)")]],
                unsigned long* len [[clang::annotate("sandbox", "out_ptr")]],
                unsigned long cap));
          )cc",
          std::make_unique<GeneratorAction>(&ok_emitter, &ok_options)),
      IsOk());
  EXPECT_THAT(ok_emitter.PostParseAllFiles(), IsOk());
}

}  // namespace
}  // namespace sapi
