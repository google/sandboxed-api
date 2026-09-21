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

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/container/flat_hash_map.h"
#include "absl/functional/overload.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/LLVM.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "llvm/ADT/StringRef.h"
#include "sandboxed_api/testing.h"
#include "sandboxed_api/tools/clang_generator/annotations.h"
#include "sandboxed_api/tools/clang_generator/arg_converter.h"
#include "sandboxed_api/tools/clang_generator/frontend_action_test_util.h"

namespace sapi {
namespace ir {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::testing::AllOf;
using ::testing::HasSubstr;

TEST(IrTest, LibraryFindFunction) {
  Library lib{
      .name = "MyLib",
      .functions = {Function{.name = "foo"}, Function{.name = "bar"}},
  };
  EXPECT_NE(lib.FindFunction("foo"), nullptr);
  EXPECT_EQ(lib.FindFunction("foo")->name, "foo");
  EXPECT_NE(lib.FindFunction("bar"), nullptr);
  EXPECT_EQ(lib.FindFunction("baz"), nullptr);

  const Library& const_lib = lib;
  EXPECT_NE(const_lib.FindFunction("foo"), nullptr);
  EXPECT_EQ(const_lib.FindFunction("baz"), nullptr);
}

TEST(IrTest, FunctionFindParameter) {
  Function func{
      .name = "my_func",
      .parameters = {Parameter{.name = "p1"}, Parameter{.name = "p2"}},
  };
  EXPECT_NE(func.FindParameter("p1"), nullptr);
  EXPECT_EQ(func.FindParameter("p1")->name, "p1");
  EXPECT_NE(func.FindParameter("p2"), nullptr);
  EXPECT_EQ(func.FindParameter("p3"), nullptr);

  const Function& const_func = func;
  EXPECT_NE(const_func.FindParameter("p1"), nullptr);
  EXPECT_EQ(const_func.FindParameter("p3"), nullptr);
}

TEST(IrTest, ParameterSemanticQueries) {
  Parameter in_ptr{
      .name = "src",
      .type = {.kind = TypeKind::kPointer},
      .payload = BufferParam{.direction = PointerDir::kIn,
                             .bounds = bounds::ElemCount{.size_expr = "n"}},
  };
  EXPECT_TRUE(in_ptr.IsInput());
  EXPECT_FALSE(in_ptr.IsOutput());
  EXPECT_FALSE(in_ptr.IsOpaque());
  EXPECT_EQ(in_ptr.direction(), PointerDir::kIn);
  ASSERT_TRUE(in_ptr.Is<BufferParam>());
  EXPECT_EQ(BoundsSizeExpr(in_ptr.As<BufferParam>()->bounds), "n");

  Parameter out_ptr{
      .name = "dst",
      .type = {.kind = TypeKind::kPointer},
      .payload = BufferParam{.direction = PointerDir::kOut,
                             .bounds = bounds::ElemCount{.size_expr = "n"}},
  };
  EXPECT_FALSE(out_ptr.IsInput());
  EXPECT_TRUE(out_ptr.IsOutput());
  EXPECT_FALSE(out_ptr.IsOpaque());

  Parameter inout_ptr{
      .name = "buf",
      .type = {.kind = TypeKind::kPointer},
      .payload = BufferParam{.direction = PointerDir::kInOut,
                             .bounds = bounds::ElemCount{.size_expr = "n"}},
  };
  EXPECT_TRUE(inout_ptr.IsInput());
  EXPECT_TRUE(inout_ptr.IsOutput());
  EXPECT_TRUE(inout_ptr.IsInOut());
  EXPECT_FALSE(inout_ptr.IsOpaque());

  Parameter opaque_ptr{
      .name = "handle",
      .type = {.kind = TypeKind::kPointer},
      .payload = OpaquePointerParam{.direction = PointerDir::kSandboxOpaque},
  };
  EXPECT_FALSE(opaque_ptr.IsInput());
  EXPECT_FALSE(opaque_ptr.IsOutput());
  EXPECT_TRUE(opaque_ptr.IsOpaque());
  ASSERT_TRUE(opaque_ptr.Is<OpaquePointerParam>());

  Parameter host_opaque_ptr{
      .name = "host_h",
      .type = {.kind = TypeKind::kPointer},
      .payload = OpaquePointerParam{.direction = PointerDir::kHostOpaque},
  };
  EXPECT_FALSE(host_opaque_ptr.IsInput());
  EXPECT_FALSE(host_opaque_ptr.IsOutput());
  EXPECT_TRUE(host_opaque_ptr.IsOpaque());

  // CppStringParam
  Parameter cpp_str_in{
      .name = "s_in",
      .type = {.kind = TypeKind::kString},
      .payload = CppStringParam{.direction = PointerDir::kIn},
  };
  EXPECT_TRUE(cpp_str_in.IsInput());
  EXPECT_FALSE(cpp_str_in.IsOutput());
  EXPECT_EQ(cpp_str_in.direction(), PointerDir::kIn);

  Parameter cpp_str_out{
      .name = "s_out",
      .type = {.kind = TypeKind::kString},
      .payload = CppStringParam{.direction = PointerDir::kOut},
  };
  EXPECT_FALSE(cpp_str_out.IsInput());
  EXPECT_TRUE(cpp_str_out.IsOutput());
  EXPECT_EQ(cpp_str_out.direction(), PointerDir::kOut);

  // Null-terminated C-string buffer.
  Parameter cstr{
      .name = "cstr",
      .type = {.kind = TypeKind::kPointer},
      .payload = BufferParam{.direction = PointerDir::kIn,
                             .bounds = bounds::NullTerminated{}},
  };
  EXPECT_TRUE(cstr.IsInput());
  EXPECT_FALSE(cstr.IsOutput());
  EXPECT_EQ(cstr.direction(), PointerDir::kIn);

  // StructSyncParam
  Parameter sync{
      .name = "sync",
      .type = {.kind = TypeKind::kPointer},
      .payload = StructSyncParam{.direction = PointerDir::kOut},
  };
  EXPECT_FALSE(sync.IsInput());
  EXPECT_TRUE(sync.IsOutput());
  EXPECT_EQ(sync.direction(), PointerDir::kOut);

  // ScalarParam
  Parameter scalar_param{
      .name = "count",
      .type = {.kind = TypeKind::kScalar},
      .payload = ScalarParam{},
  };
  EXPECT_FALSE(scalar_param.IsInput());
  EXPECT_FALSE(scalar_param.IsOutput());
  EXPECT_EQ(scalar_param.direction(), std::nullopt);
  EXPECT_FALSE(scalar_param.uninitialized());

  // VoidParam
  Parameter void_param{
      .name = "sapi_ret_arg",
      .type = {.kind = TypeKind::kVoid},
      .is_return_value = true,
      .payload = VoidParam{},
  };
  EXPECT_FALSE(void_param.IsInput());
  EXPECT_FALSE(void_param.IsOutput());
  EXPECT_EQ(void_param.direction(), std::nullopt);
  EXPECT_FALSE(void_param.uninitialized());
}

TEST(IrTest, VisitorPatternDispatch) {
  Parameter scalar{.name = "val", .payload = ScalarParam{}};
  Parameter buf{.name = "buf",
                .payload = BufferParam{
                    .direction = PointerDir::kIn,
                    .bounds = bounds::ElemCount{.size_expr = "10"},
                }};
  Parameter cpp_str{.name = "str", .payload = CppStringParam{}};

  auto describe = [](const Parameter& p) {
    return std::visit(absl::Overload{
                          [](const ScalarParam&) { return "scalar"; },
                          [](const BufferParam&) { return "buffer"; },
                          [](const CppStringParam&) { return "cpp_string"; },
                          [](const OpaquePointerParam&) { return "opaque"; },
                          [](const StructSyncParam&) { return "struct_sync"; },
                          [](const CallbackParam&) { return "callback"; },
                          [](const VoidParam&) { return "void"; },
                      },
                      p.payload);
  };

  EXPECT_EQ(describe(scalar), "scalar");
  EXPECT_EQ(describe(buf), "buffer");
  EXPECT_EQ(describe(cpp_str), "cpp_string");
}

class IrConvertTest : public ::sapi::FrontendActionTest {
 protected:
  absl::StatusOr<Function> ConvertSnippetToIR(
      absl::string_view code, absl::string_view target_name = "",
      const absl::flat_hash_map<std::string, RecordAnnotations>&
          record_annotations = {}) {
    std::optional<Function> result_func;
    absl::Status conversion_status = absl::NotFoundError(
        absl::StrCat("Target function '", target_name, "' not found"));

    class ExtractAction : public clang::ASTFrontendAction {
     public:
      ExtractAction(absl::string_view target_name,
                    const absl::flat_hash_map<std::string, RecordAnnotations>&
                        record_annotations,
                    std::optional<Function>& out_func, absl::Status& out_status)
          : target_name_(target_name),
            record_annotations_(record_annotations),
            out_func_(out_func),
            out_status_(out_status) {}

      std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
          clang::CompilerInstance&, llvm::StringRef) override {
        class Consumer : public clang::ASTConsumer,
                         public clang::RecursiveASTVisitor<Consumer> {
         public:
          Consumer(absl::string_view target_name,
                   const absl::flat_hash_map<std::string, RecordAnnotations>&
                       record_annotations,
                   std::optional<Function>& out_func, absl::Status& out_status)
              : target_name_(target_name),
                record_annotations_(record_annotations),
                out_func_(out_func),
                out_status_(out_status) {}

          void HandleTranslationUnit(clang::ASTContext& context) override {
            TraverseDecl(context.getTranslationUnitDecl());
          }

          bool VisitFunctionDecl(clang::FunctionDecl* func_decl) {
            if (target_name_.empty() ||
                func_decl->getNameAsString() == target_name_ ||
                func_decl->getNameAsString() == "Target" ||
                func_decl->getNameAsString() == "target") {
              absl::StatusOr<Function> converted =
                  ConvertFunctionToIR(func_decl, record_annotations_);
              if (!converted.ok()) {
                out_status_ = converted.status();
              } else {
                out_func_ = *std::move(converted);
                out_status_ = absl::OkStatus();
              }
              return false;
            }
            return true;
          }

         private:
          std::string target_name_;
          const absl::flat_hash_map<std::string, RecordAnnotations>&
              record_annotations_;
          std::optional<Function>& out_func_;
          absl::Status& out_status_;
        };
        return std::make_unique<Consumer>(target_name_, record_annotations_,
                                          out_func_, out_status_);
      }

     private:
      std::string target_name_;
      const absl::flat_hash_map<std::string, RecordAnnotations>&
          record_annotations_;
      std::optional<Function>& out_func_;
      absl::Status& out_status_;
    };

    absl::Status run_status = RunFrontendAction(
        code, std::make_unique<ExtractAction>(target_name, record_annotations,
                                              result_func, conversion_status));
    if (!run_status.ok()) return run_status;
    if (!conversion_status.ok()) return conversion_status;
    return *std::move(result_func);
  }
};

TEST_F(IrConvertTest, ASTToFunctionIRConversion) {
  SAPI_ASSERT_OK_AND_ASSIGN(const Function func,
                            ConvertSnippetToIR(R"(
    extern "C" void process_buffer(
        const char* src [[clang::annotate("sandbox", "in_ptr")]]
        [[clang::annotate("sandbox", "elem_sized_by", "n")]],
        char* dst [[clang::annotate("sandbox", "out_ptr")]]
        [[clang::annotate("sandbox", "elem_sized_by", "n")]],
        unsigned long n);
  )",
                                               "process_buffer"));
  EXPECT_EQ(func.name, "process_buffer");
  EXPECT_EQ(func.parameters.size(), 3);
  EXPECT_EQ(func.parameters[0].name, "src");
  EXPECT_EQ(func.parameters[0].direction(), PointerDir::kIn);
  ASSERT_TRUE(func.parameters[0].Is<BufferParam>());
  const auto* src = func.parameters[0].As<BufferParam>();
  ASSERT_TRUE(std::holds_alternative<bounds::ElemCount>(src->bounds));
  EXPECT_EQ(std::get<bounds::ElemCount>(src->bounds).size_expr, "n");

  EXPECT_EQ(func.parameters[1].name, "dst");
  EXPECT_EQ(func.parameters[1].direction(), PointerDir::kOut);
  ASSERT_TRUE(func.parameters[1].Is<BufferParam>());
  const auto* dst = func.parameters[1].As<BufferParam>();
  ASSERT_TRUE(std::holds_alternative<bounds::ElemCount>(dst->bounds));
  EXPECT_EQ(std::get<bounds::ElemCount>(dst->bounds).size_expr, "n");

  EXPECT_EQ(func.parameters[2].name, "n");
  ASSERT_TRUE(func.parameters[2].Is<ScalarParam>());
}

TEST_F(IrConvertTest, ASTToFunctionIRConversionWithDereferencedPointerSize) {
  SAPI_ASSERT_OK_AND_ASSIGN(const Function func,
                            ConvertSnippetToIR(R"(
    extern "C" void read_data(
        char* buf [[clang::annotate("sandbox", "inout_ptr")]]
        [[clang::annotate("sandbox", "elem_sized_by", "*len")]],
        int* len [[clang::annotate("sandbox", "inout_ptr")]],
        char* other [[clang::annotate("sandbox", "in_ptr")]]
        [[clang::annotate("sandbox", "elem_sized_by", " *len ")]]);
  )",
                                               "read_data"));
  ASSERT_EQ(func.parameters.size(), 3);
  ASSERT_TRUE(func.parameters[0].Is<BufferParam>());
  EXPECT_EQ(BoundsSizeExpr(func.parameters[0].As<BufferParam>()->bounds),
            "*len");
  ASSERT_TRUE(func.parameters[2].Is<BufferParam>());
  EXPECT_EQ(BoundsSizeExpr(func.parameters[2].As<BufferParam>()->bounds),
            " *len ");
  // Both spellings have to resolve to the sibling `len`, which validation is
  // what now observes: an unresolvable name would be rejected here.
  Library lib{.name = "MyLib", .functions = {func}};
  EXPECT_THAT(ValidateAndLinkLibraryIR(lib), IsOk());
}

TEST_F(IrConvertTest, ASTToFunctionIRConversionWithCallback) {
  SAPI_ASSERT_OK_AND_ASSIGN(const Function func,
                            ConvertSnippetToIR(R"(
    extern "C" void register_handler(int (*handler)(int code, const char* msg));
  )",
                                               "register_handler"));
  EXPECT_EQ(func.name, "register_handler");
  ASSERT_EQ(func.parameters.size(), 1);
  const Parameter& cb_param = func.parameters[0];
  EXPECT_EQ(cb_param.name, "handler");
  EXPECT_EQ(cb_param.type.kind, TypeKind::kCallback);
  ASSERT_TRUE(cb_param.Is<CallbackParam>());
  const auto* cb = cb_param.As<CallbackParam>();
  ASSERT_NE(cb->return_value, nullptr);
  EXPECT_EQ(cb->return_value->type.canonical_name, "int");
  EXPECT_TRUE(cb->return_value->Is<ScalarParam>());
  ASSERT_EQ(cb->callback_parameters.size(), 2);
  EXPECT_EQ(cb->callback_parameters[0].name, "code");
  EXPECT_TRUE(cb->callback_parameters[0].Is<ScalarParam>());
  EXPECT_EQ(cb->callback_parameters[1].name, "msg");
  EXPECT_TRUE(cb->callback_parameters[1].Is<BufferParam>());
}

// C++ functors (`std::function`, `absl::AnyInvocable`) are recognized as
// callbacks just like plain function pointers, and record the template they
// came from so the emitter can reconstruct the host-side type.
TEST_F(IrConvertTest, ASTToFunctionIRConversionWithFunctorCallback) {
  SAPI_ASSERT_OK_AND_ASSIGN(const Function func,
                            ConvertSnippetToIR(R"(
    namespace std {
      template <typename T>
      class function;
      template <typename R, typename... Args>
      class function<R(Args...)> {
       public:
        ~function();
      };
    }
    extern "C" void register_functor(
        std::function<int(int code, const char* msg)> handler);
  )",
                                               "register_functor"));
  ASSERT_EQ(func.parameters.size(), 1);
  const Parameter& cb_param = func.parameters[0];
  EXPECT_EQ(cb_param.name, "handler");
  EXPECT_EQ(cb_param.type.kind, TypeKind::kCallback);
  ASSERT_TRUE(cb_param.Is<CallbackParam>());
  const auto* cb = cb_param.As<CallbackParam>();
  EXPECT_EQ(cb->functor_template_name, "std::function");
  ASSERT_NE(cb->return_value, nullptr);
  EXPECT_EQ(cb->return_value->type.canonical_name, "int");
  ASSERT_EQ(cb->callback_parameters.size(), 2);
  EXPECT_EQ(cb->callback_parameters[0].name, "code");
  EXPECT_TRUE(cb->callback_parameters[0].Is<ScalarParam>());
  EXPECT_EQ(cb->callback_parameters[1].name, "msg");
  EXPECT_TRUE(cb->callback_parameters[1].Is<BufferParam>());
}

TEST_F(IrConvertTest, ASTToFunctionIRConversionWithUninitialized) {
  SAPI_ASSERT_OK_AND_ASSIGN(const Function func,
                            ConvertSnippetToIR(R"(
    extern "C" [[clang::annotate("sandbox", "uninitialized")]]
    char* process_data(
        int size,
        [[clang::annotate("sandbox", "uninitialized")]]
        char* (*get_chunk)(int len));
  )",
                                               "process_data"));
  EXPECT_EQ(func.name, "process_data");
  ASSERT_TRUE(func.return_value.has_value());
  EXPECT_TRUE(func.return_value->uninitialized());
  ASSERT_TRUE(func.return_value->Is<BufferParam>());
  EXPECT_TRUE(func.return_value->As<BufferParam>()->uninitialized);

  ASSERT_EQ(func.parameters.size(), 2);
  EXPECT_FALSE(func.parameters[0].uninitialized());
  EXPECT_TRUE(func.parameters[1].uninitialized());
  ASSERT_TRUE(func.parameters[1].Is<CallbackParam>());
  EXPECT_TRUE(func.parameters[1].As<CallbackParam>()->uninitialized);
}

TEST_F(IrConvertTest, ASTToFunctionIRConversionWithStructSync) {
  absl::flat_hash_map<std::string, RecordAnnotations> record_annotations;
  RecordAnnotations img_ann{.name = "Image"};
  DataMemberAnnotations pixels_ann{
      .name = "pixels",
      .size_type = ElemSizedBy{"size"},
      .ptr_dir = PointerDir::kIn,
  };
  img_ann.member_annotations.push_back(std::move(pixels_ann));
  record_annotations["Image"] = std::move(img_ann);

  SAPI_ASSERT_OK_AND_ASSIGN(
      const Function func,
      ConvertSnippetToIR(R"(
    struct Image {
      char* pixels;
      int size;
    };
    extern "C" void render_image(
        Image* img [[clang::annotate("sandbox", "in_ptr")]]
        [[clang::annotate("sandbox", "struct_sync", "binding_1",
                           "{img->pixels", "in_ptr}", "$")]]);
  )",
                         "render_image", record_annotations));
  ASSERT_EQ(func.parameters.size(), 1);
  const Parameter& img_param = func.parameters[0];
  ASSERT_TRUE(img_param.Is<StructSyncParam>());
  const auto* sync_param = img_param.As<StructSyncParam>();
  ASSERT_EQ(sync_param->members.size(), 1);
  const StructMemberSync& sync = sync_param->members[0];
  EXPECT_EQ(sync.access_path(), "img->pixels");
  EXPECT_EQ(sync.member_name, "pixels");
  EXPECT_EQ(sync.parent_prefix, "img->");
  EXPECT_EQ(sync.direction, PointerDir::kIn);
  ASSERT_TRUE(std::holds_alternative<bounds::ElemCount>(sync.bounds));
  EXPECT_EQ(std::get<bounds::ElemCount>(sync.bounds).size_expr, "size");
}

// A `typedef struct { ... } Image;` declares an *anonymous* record, so
// `RecordDecl::getName()` is empty. Such a struct can never have record
// annotations, because ParseRecordAnnotations rejects an empty record name, so
// the lookup simply misses and the members keep their default bounds. This is
// idiomatic C, so it must convert rather than error.
TEST_F(IrConvertTest, StructSyncOnAnonymousTypedefStruct) {
  absl::flat_hash_map<std::string, RecordAnnotations> record_annotations;
  RecordAnnotations img_ann{.name = "Image"};
  img_ann.member_annotations.push_back(DataMemberAnnotations{
      .name = "pixels",
      .size_type = ElemSizedBy{"size"},
      .ptr_dir = PointerDir::kIn,
  });
  record_annotations["Image"] = std::move(img_ann);

  SAPI_ASSERT_OK_AND_ASSIGN(
      const Function func,
      ConvertSnippetToIR(R"(
    typedef struct {
      char* pixels;
      int size;
    } Image;
    extern "C" void render_image(
        Image* img [[clang::annotate("sandbox", "in_ptr")]]
        [[clang::annotate("sandbox", "struct_sync", "binding_1",
                           "{img->pixels", "in_ptr}", "$")]]);
  )",
                         "render_image", record_annotations));
  ASSERT_EQ(func.parameters.size(), 1);
  ASSERT_TRUE(func.parameters[0].Is<StructSyncParam>());
  const auto* sync_param = func.parameters[0].As<StructSyncParam>();
  ASSERT_EQ(sync_param->members.size(), 1);
  const StructMemberSync& sync = sync_param->members[0];
  EXPECT_EQ(sync.member_name, "pixels");
  // The "Image" entry above is keyed by the typedef name, which the anonymous
  // record does not carry, so its member annotations do not apply here.
  EXPECT_TRUE(std::holds_alternative<bounds::Singleton>(sync.bounds));
}

TEST_F(IrConvertTest, ASTToFunctionIRConversionWithStructAndEnum) {
  SAPI_ASSERT_OK_AND_ASSIGN(const Function func,
                            ConvertSnippetToIR(R"(
    enum Color { RED, GREEN, BLUE };
    struct Point { int x; int y; };
    extern "C" Point transform_point(Point pt, Color color);
  )",
                                               "transform_point"));
  EXPECT_EQ(func.name, "transform_point");
  ASSERT_TRUE(func.return_value.has_value());
  EXPECT_EQ(func.return_value->type.kind, TypeKind::kStruct);
  ASSERT_EQ(func.parameters.size(), 2);
  EXPECT_EQ(func.parameters[0].name, "pt");
  EXPECT_EQ(func.parameters[0].type.kind, TypeKind::kStruct);
  EXPECT_EQ(func.parameters[1].name, "color");
  EXPECT_EQ(func.parameters[1].type.kind, TypeKind::kScalar);
}

TEST_F(IrConvertTest, RejectsPointerReferences) {
  auto result = ConvertSnippetToIR(R"(
    extern "C" void process_ptrs(int*& ptr_ref);
  )",
                                   "process_ptrs");
  EXPECT_THAT(result, StatusIs(absl::StatusCode::kInvalidArgument,
                               HasSubstr("unsupported reference type")));
}

// A struct passed by reference lives in the caller's address space, so it
// needs a copy the generator does not emit today. Only functors are supported
// by reference.
TEST_F(IrConvertTest, RejectsStructReferences) {
  EXPECT_THAT(ConvertSnippetToIR(R"(
    struct Point { int x; int y; };
    extern "C" void take_point(const Point& pt);
  )",
                                 "take_point"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("unsupported reference type")));
}

TEST_F(IrConvertTest, ASTToFunctionIRConversionWithStrings) {
  SAPI_ASSERT_OK_AND_ASSIGN(const Function func,
                            ConvertSnippetToIR(R"(
    namespace std {
      template <typename CharT>
      class basic_string {
       public:
        ~basic_string();
      };
      using string = basic_string<char>;

      template <typename CharT>
      class basic_string_view {
       public:
        ~basic_string_view();
      };
      using string_view = basic_string_view<char>;
    }
    extern "C" std::string process_strings(
        std::string val, std::string_view view, const std::string& ref);
  )",
                                               "process_strings"));
  EXPECT_EQ(func.name, "process_strings");
  ASSERT_TRUE(func.return_value.has_value());
  EXPECT_EQ(func.return_value->type.kind, TypeKind::kString);
  ASSERT_TRUE(func.return_value->Is<CppStringParam>());
  EXPECT_EQ(func.return_value->As<CppStringParam>()->form,
            CppStringParam::Form::kValue);

  ASSERT_EQ(func.parameters.size(), 3);
  EXPECT_EQ(func.parameters[0].name, "val");
  EXPECT_EQ(func.parameters[0].type.kind, TypeKind::kString);
  ASSERT_TRUE(func.parameters[0].Is<CppStringParam>());
  EXPECT_EQ(func.parameters[0].As<CppStringParam>()->form,
            CppStringParam::Form::kValue);

  EXPECT_EQ(func.parameters[1].name, "view");
  EXPECT_EQ(func.parameters[1].type.kind, TypeKind::kString);
  ASSERT_TRUE(func.parameters[1].Is<CppStringParam>());
  EXPECT_EQ(func.parameters[1].As<CppStringParam>()->form,
            CppStringParam::Form::kView);

  EXPECT_EQ(func.parameters[2].name, "ref");
  EXPECT_EQ(func.parameters[2].type.kind, TypeKind::kString);
  ASSERT_TRUE(func.parameters[2].Is<CppStringParam>());
  EXPECT_EQ(func.parameters[2].As<CppStringParam>()->form,
            CppStringParam::Form::kReference);
}

// A mutable `std::string&` shares Form::kReference with the const form; the
// two are told apart only by `is_const`, which is what decides whether the
// value has to be synced back out of the sandbox after the call.
TEST_F(IrConvertTest, ASTToFunctionIRConversionWithMutableStringRef) {
  SAPI_ASSERT_OK_AND_ASSIGN(const Function func,
                            ConvertSnippetToIR(R"(
    namespace std {
      template <typename CharT>
      class basic_string {
       public:
        ~basic_string();
      };
      using string = basic_string<char>;
    }
    extern "C" void fill_string(std::string& out, const std::string& in);
  )",
                                               "fill_string"));
  ASSERT_EQ(func.parameters.size(), 2);

  EXPECT_EQ(func.parameters[0].name, "out");
  EXPECT_EQ(func.parameters[0].type.kind, TypeKind::kString);
  EXPECT_EQ(func.parameters[0].type.canonical_name, "std::string&");
  EXPECT_FALSE(func.parameters[0].type.is_const);
  ASSERT_TRUE(func.parameters[0].Is<CppStringParam>());
  EXPECT_EQ(func.parameters[0].As<CppStringParam>()->form,
            CppStringParam::Form::kReference);
  EXPECT_FALSE(func.parameters[0].As<CppStringParam>()->is_const);
  EXPECT_EQ(func.parameters[0].direction(), PointerDir::kInOut);

  EXPECT_EQ(func.parameters[1].name, "in");
  EXPECT_EQ(func.parameters[1].type.kind, TypeKind::kString);
  EXPECT_EQ(func.parameters[1].type.canonical_name, "const std::string&");
  EXPECT_TRUE(func.parameters[1].type.is_const);
  ASSERT_TRUE(func.parameters[1].Is<CppStringParam>());
  EXPECT_TRUE(func.parameters[1].As<CppStringParam>()->is_const);
  EXPECT_EQ(func.parameters[1].direction(), PointerDir::kIn);
}

// Clang spells cv-qualifiers into the canonical type name, so `const
// std::string` prints as "const class std::basic_string<char>". Matching that
// name against the unqualified spelling alone would file these parameters
// under TypeKind::kStruct and marshal them as opaque records.
TEST_F(IrConvertTest, ASTToFunctionIRConversionWithConstQualifiedStrings) {
  SAPI_ASSERT_OK_AND_ASSIGN(const Function func,
                            ConvertSnippetToIR(R"(
    namespace std {
      template <typename CharT>
      class basic_string {
       public:
        ~basic_string();
      };
      using string = basic_string<char>;

      template <typename CharT>
      class basic_string_view {
       public:
        ~basic_string_view();
      };
      using string_view = basic_string_view<char>;
    }
    extern "C" void take_const_strings(const std::string val,
                                       const std::string_view view);
  )",
                                               "take_const_strings"));
  ASSERT_EQ(func.parameters.size(), 2);

  EXPECT_EQ(func.parameters[0].name, "val");
  EXPECT_EQ(func.parameters[0].type.kind, TypeKind::kString);
  EXPECT_EQ(func.parameters[0].type.canonical_name, "std::string");
  ASSERT_TRUE(func.parameters[0].Is<CppStringParam>());
  EXPECT_EQ(func.parameters[0].As<CppStringParam>()->form,
            CppStringParam::Form::kValue);
  EXPECT_EQ(func.parameters[0].direction(), PointerDir::kIn);

  EXPECT_EQ(func.parameters[1].name, "view");
  EXPECT_EQ(func.parameters[1].type.kind, TypeKind::kString);
  EXPECT_EQ(func.parameters[1].type.canonical_name, "std::string_view");
  ASSERT_TRUE(func.parameters[1].Is<CppStringParam>());
  EXPECT_EQ(func.parameters[1].As<CppStringParam>()->form,
            CppStringParam::Form::kView);
  EXPECT_EQ(func.parameters[1].direction(), PointerDir::kIn);
}

// A `std::string*` is marshalled through Form::kPointer. The pointee's
// const-ness, not the pointer's, decides whether the string has to be copied
// back out of the sandbox after the call.
TEST_F(IrConvertTest, ASTToFunctionIRConversionWithStringPointers) {
  SAPI_ASSERT_OK_AND_ASSIGN(const Function func,
                            ConvertSnippetToIR(R"(
    namespace std {
      template <typename CharT>
      class basic_string {
       public:
        ~basic_string();
      };
      using string = basic_string<char>;
    }
    extern "C" void copy_string(
        const std::string* in [[clang::annotate("sandbox", "in_ptr")]],
        std::string* out [[clang::annotate("sandbox", "out_ptr")]]);
  )",
                                               "copy_string"));
  ASSERT_EQ(func.parameters.size(), 2);

  EXPECT_EQ(func.parameters[0].name, "in");
  EXPECT_EQ(func.parameters[0].type.kind, TypeKind::kPointer);
  EXPECT_TRUE(func.parameters[0].type.is_pointee_string());
  ASSERT_TRUE(func.parameters[0].Is<CppStringParam>());
  EXPECT_EQ(func.parameters[0].As<CppStringParam>()->form,
            CppStringParam::Form::kPointer);
  EXPECT_TRUE(func.parameters[0].As<CppStringParam>()->is_const);
  EXPECT_EQ(func.parameters[0].direction(), PointerDir::kIn);

  EXPECT_EQ(func.parameters[1].name, "out");
  EXPECT_EQ(func.parameters[1].type.kind, TypeKind::kPointer);
  EXPECT_TRUE(func.parameters[1].type.is_pointee_string());
  ASSERT_TRUE(func.parameters[1].Is<CppStringParam>());
  EXPECT_EQ(func.parameters[1].As<CppStringParam>()->form,
            CppStringParam::Form::kPointer);
  EXPECT_FALSE(func.parameters[1].As<CppStringParam>()->is_const);
  EXPECT_EQ(func.parameters[1].direction(), PointerDir::kOut);
}

// `is_const` on a string pointer is the *pointee's* const-ness: `const
// std::string*` is const, `std::string* const` is not. Top-level const on a
// parameter says nothing about whether the string may be written back, and is
// not even part of the function's type. Forms that do not carry an explicit
// direction fall back to kInOut only when the callee could write through them.
TEST_F(IrConvertTest, ASTToFunctionIRConversionWithStringConstPlacement) {
  SAPI_ASSERT_OK_AND_ASSIGN(const Function func,
                            ConvertSnippetToIR(R"(
    namespace std {
      template <typename CharT>
      class basic_string {
       public:
        ~basic_string();
      };
      using string = basic_string<char>;
    }
    extern "C" void take_strings(
        const std::string* ptr_to_const
            [[clang::annotate("sandbox", "in_ptr")]],
        std::string* const const_ptr [[clang::annotate("sandbox", "in_ptr")]],
        std::string& ref,
        const std::string& const_ref);
  )",
                                               "take_strings"));
  ASSERT_EQ(func.parameters.size(), 4);

  // Pointer to const: the string cannot be written back.
  ASSERT_TRUE(func.parameters[0].Is<CppStringParam>());
  EXPECT_TRUE(func.parameters[0].As<CppStringParam>()->is_const);

  // Const pointer to mutable string: writable through, so not const.
  ASSERT_TRUE(func.parameters[1].Is<CppStringParam>());
  EXPECT_FALSE(func.parameters[1].As<CppStringParam>()->is_const);

  // A mutable reference defaults to kInOut.
  ASSERT_TRUE(func.parameters[2].Is<CppStringParam>());
  EXPECT_EQ(func.parameters[2].As<CppStringParam>()->form,
            CppStringParam::Form::kReference);
  EXPECT_FALSE(func.parameters[2].As<CppStringParam>()->is_const);
  EXPECT_EQ(func.parameters[2].direction(), PointerDir::kInOut);

  // A const reference defaults to kIn.
  ASSERT_TRUE(func.parameters[3].Is<CppStringParam>());
  EXPECT_TRUE(func.parameters[3].As<CppStringParam>()->is_const);
  EXPECT_EQ(func.parameters[3].direction(), PointerDir::kIn);
}

// Writing back through a pointer-to-const is a contradiction. Accepting it
// would emit glue that copies out into a const std::string.
TEST_F(IrConvertTest, RejectsConstStringWithOutDirection) {
  EXPECT_THAT(
      ConvertSnippetToIR(R"(
    namespace std {
      template <typename CharT>
      class basic_string {
       public:
        ~basic_string();
      };
      using string = basic_string<char>;
    }
    extern "C" void write_const_string(
        const std::string* out [[clang::annotate("sandbox", "out_ptr")]]);
  )",
                         "write_const_string"),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("a const C++ string cannot be an output parameter")));
}

TEST_F(IrConvertTest,
       ASTToFunctionIRConversionWithBufferBoundsVariantsAndUnnamedParams) {
  SAPI_ASSERT_OK_AND_ASSIGN(const Function func,
                            ConvertSnippetToIR(R"(
    extern "C" void various_bounds(
        int,
        const char* cstr [[clang::annotate("sandbox", "in_ptr")]]
        [[clang::annotate("sandbox", "null_terminated")]],
        void* raw [[clang::annotate("sandbox", "in_ptr")]]
        [[clang::annotate("sandbox", "byte_sized_by", "128")]],
        char* out_data [[clang::annotate("sandbox", "out_ptr")]]
        [[clang::annotate("sandbox", "elem_sized_by_outparam", "*written_len",
                           "1024")]],
        int* out_bytes_data [[clang::annotate("sandbox", "out_ptr")]]
        [[clang::annotate("sandbox", "byte_sized_by_outparam", "*written_bytes",
                           "2048")]],
        void* bound_data [[clang::annotate("sandbox", "in_ptr")]]
        [[clang::annotate("sandbox", "sized_by_binding", "ctx",
                          "len_binding")]]);
  )",
                                               "various_bounds"));
  EXPECT_EQ(func.name, "various_bounds");
  EXPECT_FALSE(func.return_value.has_value());
  ASSERT_EQ(func.parameters.size(), 6);
  // Unnamed param 0 -> sapi_arg0
  EXPECT_EQ(func.parameters[0].name, "sapi_arg0");
  EXPECT_EQ(func.parameters[0].type.kind, TypeKind::kScalar);
  ASSERT_TRUE(func.parameters[0].Is<ScalarParam>());

  // Null terminated C-string
  EXPECT_EQ(func.parameters[1].name, "cstr");
  ASSERT_TRUE(func.parameters[1].Is<BufferParam>());
  EXPECT_TRUE(std::holds_alternative<bounds::NullTerminated>(
      func.parameters[1].As<BufferParam>()->bounds));
  EXPECT_EQ(func.parameters[1].direction(), PointerDir::kIn);

  // Byte sized buffer
  EXPECT_EQ(func.parameters[2].name, "raw");
  ASSERT_TRUE(func.parameters[2].Is<BufferParam>());
  const auto* raw_buf = func.parameters[2].As<BufferParam>();
  ASSERT_TRUE(std::holds_alternative<bounds::ByteCount>(raw_buf->bounds));
  EXPECT_EQ(std::get<bounds::ByteCount>(raw_buf->bounds).size_expr, "128");

  // Element sized by outparam
  EXPECT_EQ(func.parameters[3].name, "out_data");
  ASSERT_TRUE(func.parameters[3].Is<BufferParam>());
  const auto* out_buf = func.parameters[3].As<BufferParam>();
  ASSERT_TRUE(
      std::holds_alternative<bounds::ElemSizedByOutparam>(out_buf->bounds));
  const auto& out_bounds =
      std::get<bounds::ElemSizedByOutparam>(out_buf->bounds);
  EXPECT_EQ(out_bounds.outparam_name, "written_len");
  EXPECT_EQ(out_bounds.capacity_expr, "1024");
  EXPECT_EQ(BoundsSizeExpr(out_buf->bounds), "*written_len");

  // Byte sized by outparam on a typed pointer (int*)
  EXPECT_EQ(func.parameters[4].name, "out_bytes_data");
  ASSERT_TRUE(func.parameters[4].Is<BufferParam>());
  const auto* out_bytes_buf = func.parameters[4].As<BufferParam>();
  ASSERT_TRUE(std::holds_alternative<bounds::ByteSizedByOutparam>(
      out_bytes_buf->bounds));
  const auto& out_bytes_bounds =
      std::get<bounds::ByteSizedByOutparam>(out_bytes_buf->bounds);
  EXPECT_EQ(out_bytes_bounds.outparam_name, "written_bytes");
  EXPECT_EQ(out_bytes_bounds.capacity_expr, "2048");
  EXPECT_EQ(BoundsSizeExpr(out_bytes_buf->bounds), "*written_bytes");

  // Sized by binding
  EXPECT_EQ(func.parameters[5].name, "bound_data");
  ASSERT_TRUE(func.parameters[5].Is<BufferParam>());
  const auto* bound_buf = func.parameters[5].As<BufferParam>();
  ASSERT_TRUE(
      std::holds_alternative<bounds::SizedByBinding>(bound_buf->bounds));
  const auto& bound_bounds =
      std::get<bounds::SizedByBinding>(bound_buf->bounds);
  EXPECT_EQ(bound_bounds.context_expr, "ctx");
  EXPECT_EQ(bound_bounds.binding_name, "len_binding");
}

// `clear_bindings` is stored on ContextBoundAnnotations, which only
// BufferParam carries. Null-terminated strings must therefore also be
// represented as buffers so the annotation is not silently dropped.
TEST_F(IrConvertTest, ASTToFunctionIRConversionWithClearBindingsCString) {
  SAPI_ASSERT_OK_AND_ASSIGN(const Function func,
                            ConvertSnippetToIR(R"(
    extern "C" void reset_with_str(
        const char* str [[clang::annotate("sandbox", "in_ptr")]]
                        [[clang::annotate("sandbox", "null_terminated")]]
                        [[clang::annotate("sandbox", "clear_bindings")]]);
  )",
                                               "reset_with_str"));
  ASSERT_EQ(func.parameters.size(), 1);
  EXPECT_EQ(func.parameters[0].name, "str");
  ASSERT_TRUE(func.parameters[0].Is<BufferParam>());
  const auto* buf = func.parameters[0].As<BufferParam>();
  EXPECT_TRUE(std::holds_alternative<bounds::NullTerminated>(buf->bounds));
  EXPECT_TRUE(buf->context_bound.clear_bindings);
}

TEST_F(IrConvertTest, ASTToFunctionIRConversionWithMultiLevelPointers) {
  SAPI_ASSERT_OK_AND_ASSIGN(const Function func,
                            ConvertSnippetToIR(R"(
    typedef unsigned char JSAMPLE;
    typedef JSAMPLE* JSAMPROW;
    typedef JSAMPROW* JSAMPARRAY;

    extern "C" void process_multi_ptrs(
        int** double_ptr,
        const char* const* argv,
        JSAMPARRAY samples,
        void** opaque_handle_ptr,
        char*** triple_ptr);
  )",
                                               "process_multi_ptrs"));
  EXPECT_EQ(func.name, "process_multi_ptrs");
  ASSERT_EQ(func.parameters.size(), 5);

  // Param 0: int** (depth 2)
  const auto& p0 = func.parameters[0].type;
  EXPECT_EQ(p0.kind, TypeKind::kPointer);
  EXPECT_TRUE(p0.is_pointer());
  EXPECT_EQ(p0.indirection_depth(), 2);
  EXPECT_TRUE(p0.is_pointee_pointer());
  ASSERT_NE(p0.pointee, nullptr);
  EXPECT_EQ(p0.pointee_type_name(), "int *");
  EXPECT_EQ(p0.pointee->indirection_depth(), 1);
  ASSERT_NE(p0.base_type(), nullptr);
  EXPECT_EQ(p0.base_type()->canonical_name, "int");
  EXPECT_EQ(p0.base_type()->kind, TypeKind::kScalar);

  // Param 1: const char* const* (depth 2, const qualifiers)
  const auto& p1 = func.parameters[1].type;
  EXPECT_EQ(p1.indirection_depth(), 2);
  EXPECT_TRUE(p1.is_pointee_pointer());
  ASSERT_NE(p1.pointee, nullptr);
  EXPECT_TRUE(p1.pointee->is_const);
  ASSERT_NE(p1.base_type(), nullptr);
  EXPECT_EQ(p1.base_type()->canonical_name, "const char");

  // Param 2: JSAMPARRAY / unsigned char** (depth 2)
  const auto& p2 = func.parameters[2].type;
  EXPECT_EQ(p2.indirection_depth(), 2);
  EXPECT_TRUE(p2.is_pointee_pointer());
  ASSERT_NE(p2.base_type(), nullptr);
  EXPECT_EQ(p2.base_type()->canonical_name, "unsigned char");
  EXPECT_EQ(p2.base_type()->kind, TypeKind::kScalar);

  // Param 3: void** (depth 2, base is void)
  const auto& p3 = func.parameters[3].type;
  EXPECT_EQ(p3.indirection_depth(), 2);
  EXPECT_TRUE(p3.is_pointee_pointer());
  ASSERT_NE(p3.pointee, nullptr);
  EXPECT_TRUE(p3.pointee->is_pointee_void());
  ASSERT_NE(p3.base_type(), nullptr);
  EXPECT_EQ(p3.base_type()->kind, TypeKind::kVoid);

  // Param 4: char*** (depth 3)
  const auto& p4 = func.parameters[4].type;
  EXPECT_EQ(p4.indirection_depth(), 3);
  EXPECT_TRUE(p4.is_pointee_pointer());
  ASSERT_NE(p4.pointee, nullptr);
  EXPECT_EQ(p4.pointee->indirection_depth(), 2);
  ASSERT_NE(p4.base_type(), nullptr);
  EXPECT_EQ(p4.base_type()->canonical_name, "char");
}

TEST_F(IrConvertTest, RejectsUnsupportedReferenceType) {
  auto result = ConvertSnippetToIR(R"(
    extern "C" void take_ref(int& r);
  )",
                                   "take_ref");
  EXPECT_THAT(result, StatusIs(absl::StatusCode::kInvalidArgument,
                               HasSubstr("unsupported reference type")));
}

TEST_F(IrConvertTest, RejectsMissingPointerAnnotation) {
  auto result = ConvertSnippetToIR(R"(
    extern "C" void raw_ptr(int* p);
  )",
                                   "raw_ptr");
  EXPECT_THAT(result, StatusIs(absl::StatusCode::kInvalidArgument,
                               HasSubstr("missing sandbox annotation")));
}

TEST(IrTypeInfoTest, IndirectionDepthAndBaseTypeQueries) {
  // Scalar (depth 0)
  TypeInfo scalar_type{.kind = TypeKind::kScalar, .canonical_name = "int"};
  EXPECT_EQ(scalar_type.indirection_depth(), 0);
  EXPECT_EQ(scalar_type.base_type()->canonical_name, "int");
  EXPECT_FALSE(scalar_type.is_pointee_pointer());
  EXPECT_FALSE(scalar_type.is_pointee_void());

  // Single pointer (depth 1)
  TypeInfo ptr1{.kind = TypeKind::kPointer,
                .canonical_name = "int*",
                .pointee = std::make_shared<TypeInfo>(scalar_type)};
  EXPECT_EQ(ptr1.indirection_depth(), 1);
  EXPECT_EQ(ptr1.pointee_type_name(), "int");
  EXPECT_EQ(ptr1.base_type()->canonical_name, "int");
  EXPECT_FALSE(ptr1.is_pointee_pointer());

  // Double pointer (depth 2)
  TypeInfo ptr2{.kind = TypeKind::kPointer,
                .canonical_name = "int**",
                .pointee = std::make_shared<TypeInfo>(ptr1)};
  EXPECT_EQ(ptr2.indirection_depth(), 2);
  EXPECT_EQ(ptr2.pointee_type_name(), "int*");
  EXPECT_EQ(ptr2.base_type()->canonical_name, "int");
  EXPECT_TRUE(ptr2.is_pointee_pointer());

  // Void pointer (depth 1 to void)
  TypeInfo void_type{.kind = TypeKind::kVoid, .canonical_name = "void"};
  TypeInfo void_ptr{.kind = TypeKind::kPointer,
                    .canonical_name = "void*",
                    .pointee = std::make_shared<TypeInfo>(void_type)};
  EXPECT_EQ(void_ptr.indirection_depth(), 1);
  EXPECT_EQ(void_ptr.pointee_type_name(), "void");
  EXPECT_TRUE(void_ptr.is_pointee_void());
  EXPECT_EQ(void_ptr.base_type()->kind, TypeKind::kVoid);

  // String pointer (depth 1 to string)
  TypeInfo string_type{.kind = TypeKind::kString,
                       .canonical_name = "std::string"};
  TypeInfo str_ptr{.kind = TypeKind::kPointer,
                   .canonical_name = "std::string*",
                   .pointee = std::make_shared<TypeInfo>(string_type)};
  EXPECT_EQ(str_ptr.indirection_depth(), 1);
  EXPECT_EQ(str_ptr.pointee_type_name(), "std::string");
  EXPECT_TRUE(str_ptr.is_pointee_string());
  EXPECT_FALSE(str_ptr.is_pointee_arithmetic());
}

// Callback parameters annotated with alias_ptr are linked to the enclosing
// function's host opaque parameters as part of ValidateAndLinkLibraryIR. Each
// distinct outer parameter gets its own handle, and two callback parameters
// aliasing the same outer parameter share one. Mirrors `with_host_opaque` in
// the replaced_library_callbacks test library.
TEST_F(IrConvertTest, ValidateAndLinkLibraryIRLinksAliasPtrToHostOpaque) {
  SAPI_ASSERT_OK_AND_ASSIGN(const Function func,
                            ConvertSnippetToIR(R"(
    extern "C" int with_host_opaque(
        int (*combiner)(
            void* closure1 [[clang::annotate("sandbox", "host_opaque_ptr")]]
                [[clang::annotate("sandbox", "alias_ptr", "outer1")]],
            int val,
            void* closure2 [[clang::annotate("sandbox", "host_opaque_ptr")]]
                [[clang::annotate("sandbox", "alias_ptr", "outer2")]],
            void* closure3 [[clang::annotate("sandbox", "host_opaque_ptr")]]
                [[clang::annotate("sandbox", "alias_ptr", "outer1")]]),
        void* outer1 [[clang::annotate("sandbox", "host_opaque_ptr")]],
        void* outer2 [[clang::annotate("sandbox", "host_opaque_ptr")]]);
  )",
                                               "with_host_opaque"));

  Library lib{.name = "MyLib", .functions = {func}};
  ASSERT_THAT(ValidateAndLinkLibraryIR(lib), IsOk());

  Function& linked = lib.functions[0];
  const auto* outer1 = linked.FindParameter("outer1")->As<OpaquePointerParam>();
  const auto* outer2 = linked.FindParameter("outer2")->As<OpaquePointerParam>();
  const auto* cb = linked.FindParameter("combiner")->As<CallbackParam>();
  ASSERT_NE(outer1, nullptr);
  ASSERT_NE(outer2, nullptr);
  ASSERT_NE(cb, nullptr);

  EXPECT_EQ(outer1->host_opaque_handle, 1);
  EXPECT_EQ(outer2->host_opaque_handle, 2);
  ASSERT_EQ(cb->callback_parameters.size(), 4);
  EXPECT_EQ(
      cb->callback_parameters[0].As<OpaquePointerParam>()->host_opaque_handle,
      1);
  EXPECT_EQ(
      cb->callback_parameters[2].As<OpaquePointerParam>()->host_opaque_handle,
      2);
  // closure3 aliases outer1, so it reuses the handle already assigned to it.
  EXPECT_EQ(
      cb->callback_parameters[3].As<OpaquePointerParam>()->host_opaque_handle,
      1);
}

TEST_F(IrConvertTest, ValidateAndLinkLibraryIRAliasPtrMissingOuterParamError) {
  SAPI_ASSERT_OK_AND_ASSIGN(const Function func,
                            ConvertSnippetToIR(R"(
    extern "C" void with_err(
        void (*cb)(
            void* closure [[clang::annotate("sandbox", "host_opaque_ptr")]]
                [[clang::annotate("sandbox", "alias_ptr", "missing")]]));
  )",
                                               "with_err"));

  Library lib{.name = "MyLib", .functions = {func}};
  EXPECT_THAT(ValidateAndLinkLibraryIR(lib),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("references non-existent parameter missing")));
}

// Only a by-value std::string return can own the bytes copied out of the
// sandbox; the other forms would have to refer to a temporary.
TEST_F(IrConvertTest, RejectsIndirectStringReturn) {
  EXPECT_THAT(ConvertSnippetToIR(R"(
    namespace std {
      template <typename CharT>
      class basic_string {
       public:
        ~basic_string();
      };
      using string = basic_string<char>;
    }
    extern "C" const std::string& borrow_string();
  )",
                                 "borrow_string"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("returns a C++ string indirectly")));
}

// Retaining a buffer records its size so later calls can size it from the
// binding. A null-terminated string has no size to record.
TEST_F(IrConvertTest, RejectsRetainedNullTerminated) {
  EXPECT_THAT(ConvertSnippetToIR(R"(
    extern "C" void retain_str(
        const char* str [[clang::annotate("sandbox", "in_ptr")]]
                        [[clang::annotate("sandbox", "null_terminated")]]
                        [[clang::annotate("sandbox", "retain_and_bind",
                                          "ctx", "str_id")]]);
  )",
                                 "retain_str"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("cannot combine retain_and_bind with "
                                 "null_terminated")));
}

// alias_ptr names the outer parameter whose handle an opaque callback
// parameter reuses. On a regular parameter there is no outer scope to name,
// so the combination is rejected rather than silently ignored.
TEST_F(IrConvertTest, RejectsAliasPtrOnNonCallbackOpaqueParam) {
  EXPECT_THAT(ConvertSnippetToIR(R"(
    extern "C" void with_bad_alias(
        void* owner [[clang::annotate("sandbox", "host_opaque_ptr")]],
        void* handle [[clang::annotate("sandbox", "sandbox_opaque_ptr")]]
                     [[clang::annotate("sandbox", "alias_ptr", "owner")]]);
  )",
                                 "with_bad_alias"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("is opaque and should not have a lifetime")));
}

// A callback is only supported as a function parameter, where its own
// parameter names and annotations can be read off the `ParmVarDecl`.
TEST_F(IrConvertTest, RejectsReturnedFunctionPointer) {
  EXPECT_THAT(ConvertSnippetToIR(R"(
    extern "C" void (*get_handler())(int);
  )",
                                 "get_handler"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("return function pointer sapi_ret_arg is not "
                                 "supported")));
}

TEST_F(IrConvertTest, RejectsReturnedFunctor) {
  EXPECT_THAT(ConvertSnippetToIR(R"(
    namespace std {
      template <typename T>
      class function;
      template <typename R, typename... Args>
      class function<R(Args...)> {
       public:
        ~function();
      };
    }
    std::function<void(int)> get_handler();
  )",
                                 "get_handler"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("return C++ functor sapi_ret_arg is not "
                                 "supported")));
}

// In C++ an empty parameter list is a prototype, so a prototype-less function
// pointer can only be spelled in C.
class IrConvertCTest : public IrConvertTest {
 protected:
  void SetUp() override { set_input_file("input.c"); }

  std::vector<std::string> GetCommandLineFlagsForTesting(
      absl::string_view input_file) override {
    return {"tool", "-fsyntax-only", "--std=c99",
            "-I.",  "-Wno-error",    std::string(input_file)};
  }
};

TEST_F(IrConvertCTest, RejectsPrototypelessCallback) {
  EXPECT_THAT(ConvertSnippetToIR(R"(
    void with_cb(void (*cb)());
  )",
                                 "with_cb"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("callback cb does not have a function proto "
                                 "type loc")));
}

// A library exercising sibling sizing, a context binding and callback return
// aliasing together links cleanly.
TEST_F(IrConvertTest, ValidateAndLinkLibraryIRValid) {
  SAPI_ASSERT_OK_AND_ASSIGN(const Function func,
                            ConvertSnippetToIR(R"(
    extern "C" {
    [[clang::annotate("sandbox", "alias_callback_return", "cb")]]
    unsigned char* process_data(
        const unsigned char* data [[clang::annotate("sandbox", "in_ptr")]]
            [[clang::annotate("sandbox", "elem_sized_by", "size")]],
        unsigned long size,
        [[clang::annotate("sandbox", "out_ptr")]]
        [[clang::annotate("sandbox", "elem_sized_by", "size")]]
        unsigned char* (*cb)(unsigned long size),
        void* bound_void [[clang::annotate("sandbox", "in_ptr")]]
            [[clang::annotate("sandbox", "sized_by_binding", "ctx",
                              "len_binding")]]);
    }
  )",
                                               "process_data"));

  Library lib{.name = "MyLib", .functions = {func}};
  ASSERT_THAT(ValidateAndLinkLibraryIR(lib), IsOk());

  const auto* cb = lib.functions[0].FindParameter("cb")->As<CallbackParam>();
  ASSERT_NE(cb, nullptr);
  EXPECT_TRUE(cb->is_callback_return_aliased);
}

TEST_F(IrConvertTest, ValidateAndLinkLibraryIRMissingSiblingError) {
  SAPI_ASSERT_OK_AND_ASSIGN(const Function func,
                            ConvertSnippetToIR(R"(
    extern "C" void bad_sibling(
        char* data [[clang::annotate("sandbox", "in_ptr")]]
            [[clang::annotate("sandbox", "elem_sized_by", "missing_len")]]);
  )",
                                               "bad_sibling"));

  Library lib{.name = "MyLib", .functions = {func}};
  EXPECT_THAT(ValidateAndLinkLibraryIR(lib),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("references non-existent sibling parameter")));
}

// A buffer may be sized by an outparam, but only by one the callee actually
// writes to.
TEST_F(IrConvertTest, ValidateAndLinkLibraryIRBadOutparamSizingError) {
  SAPI_ASSERT_OK_AND_ASSIGN(const Function func,
                            ConvertSnippetToIR(R"(
    extern "C" void bad_outparam(
        char* dst [[clang::annotate("sandbox", "out_ptr")]]
            [[clang::annotate("sandbox", "elem_sized_by_outparam", "*in_only",
                              "1024")]],
        int* in_only [[clang::annotate("sandbox", "in_ptr")]]);
  )",
                                               "bad_outparam"));

  Library lib{.name = "MyLib", .functions = {func}};
  EXPECT_THAT(ValidateAndLinkLibraryIR(lib),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("which is not an output pointer")));
}

TEST_F(IrConvertTest, ValidateAndLinkLibraryIRReturnValueMissingSiblingError) {
  SAPI_ASSERT_OK_AND_ASSIGN(const Function func,
                            ConvertSnippetToIR(R"(
    extern "C" [[clang::annotate("sandbox", "out_ptr")]]
    [[clang::annotate("sandbox", "elem_sized_by", "missing_len")]]
    char* get_data();
  )",
                                               "get_data"));

  Library lib{.name = "MyLib", .functions = {func}};
  EXPECT_THAT(
      ValidateAndLinkLibraryIR(lib),
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          HasSubstr("return value references non-existent sibling parameter")));
}

TEST_F(IrConvertTest, ValidateAndLinkLibraryIRCallbackAliasNonCallbackError) {
  SAPI_ASSERT_OK_AND_ASSIGN(const Function func,
                            ConvertSnippetToIR(R"(
    extern "C" {
    [[clang::annotate("sandbox", "alias_callback_return", "num")]]
    char* bad_alias_target(int num);
    }
  )",
                                               "bad_alias_target"));

  Library lib{.name = "MyLib", .functions = {func}};
  EXPECT_THAT(
      ValidateAndLinkLibraryIR(lib),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("references non-existent or non-callback parameter")));
}

TEST_F(IrConvertTest, ValidateAndLinkLibraryIRReturnAliasHostPtrError) {
  SAPI_ASSERT_OK_AND_ASSIGN(const Function func,
                            ConvertSnippetToIR(R"(
    extern "C" {
    [[clang::annotate("sandbox", "alias_ptr", "num")]]
    char* bad_alias_host_target(int num);
    }
  )",
                                               "bad_alias_host_target"));

  Library lib{.name = "MyLib", .functions = {func}};
  EXPECT_THAT(
      ValidateAndLinkLibraryIR(lib),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("references non-existent or non-pointer parameter")));
}

TEST_F(IrConvertTest, ValidateAndLinkLibraryIRMissingOutparamSizeError) {
  SAPI_ASSERT_OK_AND_ASSIGN(const Function func,
                            ConvertSnippetToIR(R"(
    extern "C" void missing_outparam(
        char* dst [[clang::annotate("sandbox", "out_ptr")]]
            [[clang::annotate("sandbox", "elem_sized_by_outparam",
                              "*nonexistent_len", "1024")]]);
  )",
                                               "missing_outparam"));

  Library lib{.name = "MyLib", .functions = {func}};
  EXPECT_THAT(
      ValidateAndLinkLibraryIR(lib),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("references non-existent outparam nonexistent_len")));
}

// A callback parameter sized by one of the callback's own parameters is
// valid: sibling references resolve within the callback's scope.
TEST_F(IrConvertTest,
       ValidateAndLinkLibraryIRCallbackSiblingResolvesInCallbackScope) {
  SAPI_ASSERT_OK_AND_ASSIGN(const Function func,
                            ConvertSnippetToIR(R"(
    extern "C" void with_callback(
        void (*cb)(const char* data [[clang::annotate("sandbox", "in_ptr")]]
                       [[clang::annotate("sandbox", "elem_sized_by", "len")]],
                   unsigned long len));
  )",
                                               "with_callback"));

  Library lib{.name = "MyLib", .functions = {func}};
  EXPECT_THAT(ValidateAndLinkLibraryIR(lib), IsOk());
}

// Bounds inside a callback are validated (previously skipped entirely).
TEST_F(IrConvertTest, ValidateAndLinkLibraryIRCallbackMissingSiblingError) {
  SAPI_ASSERT_OK_AND_ASSIGN(const Function func,
                            ConvertSnippetToIR(R"(
    extern "C" void with_callback(
        void (*cb)(const char* data [[clang::annotate("sandbox", "in_ptr")]]
                       [[clang::annotate("sandbox", "elem_sized_by",
                                         "nope")]]));
  )",
                                               "with_callback"));

  Library lib{.name = "MyLib", .functions = {func}};
  EXPECT_THAT(
      ValidateAndLinkLibraryIR(lib),
      StatusIs(absl::StatusCode::kInvalidArgument,
               AllOf(HasSubstr("callback cb"),
                     HasSubstr("references non-existent sibling parameter"))));
}

// A callback only sees the arguments it is passed, so one of its buffers
// cannot be sized by a parameter of the function that takes the callback, even
// though the generated trampoline is a lambda that captures that parameter and
// would therefore have the name in scope.
TEST_F(IrConvertTest,
       ValidateAndLinkLibraryIRCallbackSiblingDoesNotResolveInOuterScope) {
  SAPI_ASSERT_OK_AND_ASSIGN(const Function func,
                            ConvertSnippetToIR(R"(
    extern "C" void with_callback(
        unsigned long outer_len,
        void (*cb)(const char* data [[clang::annotate("sandbox", "in_ptr")]]
                       [[clang::annotate("sandbox", "elem_sized_by",
                                         "outer_len")]]));
  )",
                                               "with_callback"));

  Library lib{.name = "MyLib", .functions = {func}};
  EXPECT_THAT(ValidateAndLinkLibraryIR(lib),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("references non-existent sibling parameter "
                                 "outer_len")));
}

}  // namespace
}  // namespace ir
}  // namespace sapi
