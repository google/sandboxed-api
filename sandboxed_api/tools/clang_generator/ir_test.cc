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
#include "clang/AST/Stmt.h"
#include "clang/Basic/LLVM.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"
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
  EXPECT_EQ(
      std::get<bounds::ElemCount>(in_ptr.As<BufferParam>()->bounds).size_expr,
      "n");

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
    return std::visit(
        absl::Overload{
            [](const ScalarParam&) { return "scalar"; },
            [](const BufferParam&) { return "buffer"; },
            [](const CppStringParam&) { return "cpp_string"; },
            [](const OpaquePointerParam&) { return "opaque"; },
            [](const StructSyncParam&) { return "struct_sync"; },
            [](const CallbackParam&) { return "callback"; },
            [](const SandboxeeCallbackParam&) { return "sandboxee_callback"; },
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
          record_annotations = {},
      const absl::flat_hash_map<std::string, CallbackTypedef>&
          callback_typedefs = {}) {
    std::optional<Function> result_func;
    absl::Status conversion_status = absl::NotFoundError(
        absl::StrCat("Target function '", target_name, "' not found"));
    absl::flat_hash_map<std::string, RecordAnnotations> records =
        record_annotations;
    absl::flat_hash_map<std::string, CallbackTypedef> typedefs =
        callback_typedefs;
    absl::flat_hash_map<std::string, PointeeContract> pointee_contracts;
    // Outlives the frontend action below, which only holds a reference.
    const ConversionContext conv_ctx{
        .record_annotations = records,
        .callback_typedefs = typedefs,
        .pointee_contracts = pointee_contracts,
    };

    class ExtractAction : public clang::ASTFrontendAction {
     public:
      ExtractAction(
          absl::string_view target_name, const ConversionContext& conv_ctx,
          absl::flat_hash_map<std::string, RecordAnnotations>&
              record_annotations,
          absl::flat_hash_map<std::string, CallbackTypedef>& callback_typedefs,
          absl::flat_hash_map<std::string, PointeeContract>& pointee_contracts,
          std::optional<Function>& out_func, absl::Status& out_status)
          : target_name_(target_name),
            conv_ctx_(conv_ctx),
            record_annotations_(record_annotations),
            callback_typedefs_(callback_typedefs),
            pointee_contracts_(pointee_contracts),
            out_func_(out_func),
            out_status_(out_status) {}

      std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
          clang::CompilerInstance&, llvm::StringRef) override {
        class Consumer : public clang::ASTConsumer,
                         public clang::RecursiveASTVisitor<Consumer> {
         public:
          Consumer(absl::string_view target_name,
                   const ConversionContext& conv_ctx,
                   absl::flat_hash_map<std::string, RecordAnnotations>&
                       record_annotations,
                   absl::flat_hash_map<std::string, CallbackTypedef>&
                       callback_typedefs,
                   absl::flat_hash_map<std::string, PointeeContract>&
                       pointee_contracts,
                   std::optional<Function>& out_func, absl::Status& out_status)
              : target_name_(target_name),
                conv_ctx_(conv_ctx),
                record_annotations_(record_annotations),
                callback_typedefs_(callback_typedefs),
                pointee_contracts_(pointee_contracts),
                out_func_(out_func),
                out_status_(out_status) {}

          void HandleTranslationUnit(clang::ASTContext& context) override {
            TraverseDecl(context.getTranslationUnitDecl());
            for (const clang::FunctionDecl* func_decl : functions_) {
              if (func_decl->getIdentifier() == nullptr ||
                  !func_decl->getName().starts_with("sandbox_annotation_")) {
                continue;
              }
              const auto* body = llvm::dyn_cast_or_null<clang::CompoundStmt>(
                  func_decl->getBody());
              if (body == nullptr || body->size() != 1) {
                continue;
              }
              const auto* decl_stmt =
                  llvm::dyn_cast<clang::DeclStmt>(body->body_front());
              if (decl_stmt == nullptr || !decl_stmt->isSingleDecl()) {
                continue;
              }
              const auto* record_decl =
                  llvm::dyn_cast_or_null<clang::RecordDecl>(
                      decl_stmt->getSingleDecl());
              if (record_decl == nullptr) {
                continue;
              }
              absl::StatusOr<RecordAnnotations> rec_ann =
                  ParseRecordAnnotations(*record_decl);
              if (!rec_ann.ok()) {
                out_status_ = rec_ann.status();
                return;
              }
              std::string rec_name = rec_ann->name;
              record_annotations_[std::move(rec_name)] = *std::move(rec_ann);
            }
            for (const clang::TypedefNameDecl* td : typedefs_) {
              if (td != td->getMostRecentDecl() &&
                  !HasSandboxAnnotations(*td)) {
                continue;
              }
              absl::StatusOr<CallbackTypedef> cb_typedef =
                  ParseCallbackTypedef(*td);
              if (!cb_typedef.ok()) {
                out_status_ = cb_typedef.status();
                return;
              }
              std::string typedef_name = cb_typedef->name;
              auto [it, inserted] = callback_typedefs_.try_emplace(
                  std::move(typedef_name), *std::move(cb_typedef));
              if (!inserted) {
                out_status_ = absl::InvalidArgumentError(
                    absl::StrCat("callback typedef ", it->first,
                                 " is annotated more than once"));
                return;
              }
              if (absl::StatusOr<InvokerConversion> conversion =
                      ConvertCallbackTypedefToInvokerIR(*td, it->second,
                                                        conv_ctx_);
                  conversion.ok() && !conversion->invoker.has_value()) {
                it->second.no_reverse_reason =
                    std::move(conversion->no_reverse_reason);
              }
            }
            for (const clang::TypedefNameDecl* td : pointee_typedefs_) {
              absl::StatusOr<PointeeContract> contract =
                  ParsePointeeContract(*td);
              if (!contract.ok()) {
                out_status_ = contract.status();
                return;
              }
              if (contract->annotations.pointee_contract.has_value() &&
                  !pointee_contracts_.contains(
                      *contract->annotations.pointee_contract)) {
                contract->annotations.pointee_contract = std::nullopt;
              }
              std::string contract_name = contract->name;
              auto [it, inserted] = pointee_contracts_.try_emplace(
                  std::move(contract_name), *std::move(contract));
              if (!inserted) {
                out_status_ = absl::InvalidArgumentError(
                    absl::StrCat("pointee typedef ", it->first,
                                 " is annotated more than once"));
                return;
              }
            }
            for (const clang::FunctionDecl* func_decl : functions_) {
              if (func_decl->getIdentifier() != nullptr &&
                  func_decl->getName().starts_with("sandbox_annotation_")) {
                continue;
              }
              if (target_name_.empty() ||
                  func_decl->getNameAsString() == target_name_ ||
                  func_decl->getNameAsString() == "Target" ||
                  func_decl->getNameAsString() == "target") {
                absl::StatusOr<Function> converted =
                    ConvertFunctionToIR(func_decl, conv_ctx_);
                if (!converted.ok()) {
                  out_status_ = converted.status();
                } else {
                  out_func_ = *std::move(converted);
                  out_status_ = absl::OkStatus();
                }
                return;
              }
            }
          }

          bool VisitTypedefNameDecl(clang::TypedefNameDecl* td) {
            if (GetCallbackTypedefProto(*td) != nullptr) {
              typedefs_.push_back(td);
            } else if (HasSandboxAnnotations(*td)) {
              pointee_typedefs_.push_back(td);
            }
            return true;
          }

          bool VisitFunctionDecl(clang::FunctionDecl* func_decl) {
            functions_.push_back(func_decl);
            return true;
          }

         private:
          std::string target_name_;
          const ConversionContext& conv_ctx_;
          absl::flat_hash_map<std::string, RecordAnnotations>&
              record_annotations_;
          absl::flat_hash_map<std::string, CallbackTypedef>& callback_typedefs_;
          absl::flat_hash_map<std::string, PointeeContract>& pointee_contracts_;
          std::optional<Function>& out_func_;
          absl::Status& out_status_;
          std::vector<const clang::TypedefNameDecl*> typedefs_;
          std::vector<const clang::TypedefNameDecl*> pointee_typedefs_;
          std::vector<const clang::FunctionDecl*> functions_;
        };
        return std::make_unique<Consumer>(
            target_name_, conv_ctx_, record_annotations_, callback_typedefs_,
            pointee_contracts_, out_func_, out_status_);
      }

     private:
      std::string target_name_;
      const ConversionContext& conv_ctx_;
      absl::flat_hash_map<std::string, RecordAnnotations>& record_annotations_;
      absl::flat_hash_map<std::string, CallbackTypedef>& callback_typedefs_;
      absl::flat_hash_map<std::string, PointeeContract>& pointee_contracts_;
      std::optional<Function>& out_func_;
      absl::Status& out_status_;
    };

    absl::Status run_status = RunFrontendAction(
        code, std::make_unique<ExtractAction>(target_name, conv_ctx, records,
                                              typedefs, pointee_contracts,
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
  const auto* buf0 = func.parameters[0].As<BufferParam>();
  ASSERT_NE(buf0, nullptr);
  ASSERT_TRUE(std::holds_alternative<bounds::ElemCount>(buf0->bounds));
  EXPECT_EQ(std::get<bounds::ElemCount>(buf0->bounds).size_expr, "*len");
  const auto* buf2 = func.parameters[2].As<BufferParam>();
  ASSERT_NE(buf2, nullptr);
  ASSERT_TRUE(std::holds_alternative<bounds::ElemCount>(buf2->bounds));
  EXPECT_EQ(std::get<bounds::ElemCount>(buf2->bounds).size_expr, " *len ");
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

// Verifies that a callback `typedef` supplies the return-buffer bounds,
// lifetime, `uninitialized` flag, and parameter names for a pointer-returning
// callback, and composes with `alias_callback_return` on the outer function.
TEST_F(IrConvertTest,
       CallbackTypedefReturnedBufferWithSizeExprAndUninitialized) {
  SAPI_ASSERT_OK_AND_ASSIGN(const Function func,
                            ConvertSnippetToIR(R"(
    typedef unsigned char* (*chunk_allocator)(unsigned long other_count,
                                              unsigned long);

    [[clang::annotate("sandbox", "lifetime_outer_call")]]
    [[clang::annotate("sandbox", "elem_sized_by", "count * elem_size")]]
    [[clang::annotate("sandbox", "uninitialized")]]
    typedef unsigned char* (*chunk_allocator)(unsigned long count,
                                              unsigned long elem_size);

    extern "C"
    [[clang::annotate("sandbox", "alias_callback_return", "alloc")]]
    unsigned char* compress(chunk_allocator alloc);
  )",
                                               "compress"));
  Library lib{.name = "MyLib", .functions = {func}};
  ASSERT_THAT(ValidateAndLinkLibraryIR(lib), IsOk());

  const Function& linked = lib.functions[0];
  ASSERT_TRUE(linked.return_value.has_value());
  ASSERT_TRUE(linked.return_value->Is<BufferParam>());
  EXPECT_TRUE(std::holds_alternative<lifetime::AliasCallbackReturn>(
      linked.return_value->As<BufferParam>()->lifetime));

  ASSERT_EQ(linked.parameters.size(), 1);
  ASSERT_TRUE(linked.parameters[0].Is<CallbackParam>());
  const CallbackParam* cb = linked.parameters[0].As<CallbackParam>();
  EXPECT_TRUE(cb->uninitialized);
  EXPECT_TRUE(cb->is_callback_return_aliased);

  ASSERT_NE(cb->return_value, nullptr);
  ASSERT_TRUE(cb->return_value->Is<BufferParam>());
  const BufferParam* returned = cb->return_value->As<BufferParam>();
  EXPECT_EQ(returned->direction, PointerDir::kOut);
  EXPECT_TRUE(std::holds_alternative<lifetime::ScopedCall>(returned->lifetime));
  ASSERT_TRUE(std::holds_alternative<bounds::ElemCount>(returned->bounds));
  EXPECT_EQ(std::get<bounds::ElemCount>(returned->bounds).size_expr,
            "count * elem_size");

  // Redeclared typedef parameter names override both conflicting
  // (`other_count`) and omitted parameter names from the original declaration.
  ASSERT_EQ(cb->callback_parameters.size(), 2);
  EXPECT_EQ(cb->callback_parameters[0].name, "count");
  EXPECT_EQ(cb->callback_parameters[1].name, "elem_size");
}

// Verifies that an annotated callback `typedef` redeclaration can annotate an
// unannotated C `typedef` callback with a `host_opaque_ptr` closure parameter
// and `in_ptr`, `out_ptr`, and `inout_ptr` buffers, and that sibling
// references resolve against the redeclared typedef's parameter names.
TEST_F(IrConvertTest, CallbackTypedefWithRedeclarationsAndInputOutputBuffers) {
  SAPI_ASSERT_OK_AND_ASSIGN(const Function func,
                            ConvertSnippetToIR(R"(
    typedef int (*transform_chunk_cb)(void* ctx, const unsigned char* src,
                                      unsigned long n, unsigned char* dst,
                                      unsigned long* total_written);

    typedef int (*transform_chunk_cb)(
        void* user_data [[clang::annotate("sandbox", "host_opaque_ptr")]],
        const unsigned char* in
            [[clang::annotate("sandbox", "in_ptr")]]
            [[clang::annotate("sandbox", "elem_sized_by", "len")]],
        unsigned long len,
        unsigned char* out
            [[clang::annotate("sandbox", "out_ptr")]]
            [[clang::annotate("sandbox", "elem_sized_by", "len")]],
        unsigned long* written [[clang::annotate("sandbox", "inout_ptr")]]);

    extern "C" int run_pipeline(transform_chunk_cb cb);
  )",
                                               "run_pipeline"));
  Library lib{.name = "MyLib", .functions = {func}};
  ASSERT_THAT(ValidateAndLinkLibraryIR(lib), IsOk());

  ASSERT_EQ(func.parameters.size(), 1);
  ASSERT_TRUE(func.parameters[0].Is<CallbackParam>());
  const CallbackParam* cb = func.parameters[0].As<CallbackParam>();
  ASSERT_NE(cb->return_value, nullptr);
  EXPECT_TRUE(cb->return_value->Is<ScalarParam>());
  ASSERT_EQ(cb->callback_parameters.size(), 5);

  EXPECT_EQ(cb->callback_parameters[0].name, "user_data");
  ASSERT_TRUE(cb->callback_parameters[0].Is<OpaquePointerParam>());
  EXPECT_EQ(cb->callback_parameters[0].direction(), PointerDir::kHostOpaque);

  EXPECT_EQ(cb->callback_parameters[1].name, "in");
  ASSERT_TRUE(cb->callback_parameters[1].Is<BufferParam>());
  const BufferParam* in_buf = cb->callback_parameters[1].As<BufferParam>();
  EXPECT_EQ(in_buf->direction, PointerDir::kIn);
  ASSERT_TRUE(std::holds_alternative<bounds::ElemCount>(in_buf->bounds));
  EXPECT_EQ(std::get<bounds::ElemCount>(in_buf->bounds).size_expr, "len");

  EXPECT_EQ(cb->callback_parameters[2].name, "len");
  EXPECT_TRUE(cb->callback_parameters[2].Is<ScalarParam>());

  EXPECT_EQ(cb->callback_parameters[3].name, "out");
  ASSERT_TRUE(cb->callback_parameters[3].Is<BufferParam>());
  const BufferParam* out_buf = cb->callback_parameters[3].As<BufferParam>();
  EXPECT_EQ(out_buf->direction, PointerDir::kOut);
  ASSERT_TRUE(std::holds_alternative<bounds::ElemCount>(out_buf->bounds));
  EXPECT_EQ(std::get<bounds::ElemCount>(out_buf->bounds).size_expr, "len");

  EXPECT_EQ(cb->callback_parameters[4].name, "written");
  ASSERT_TRUE(cb->callback_parameters[4].Is<BufferParam>());
  const BufferParam* written_buf = cb->callback_parameters[4].As<BufferParam>();
  EXPECT_EQ(written_buf->direction, PointerDir::kInOut);
  EXPECT_TRUE(std::holds_alternative<bounds::Singleton>(written_buf->bounds));
}

// Verifies that a `std::function` functor parameter can reference a function
// `typedef` (`std::function<string_consumer_fn>`) and share the same
// `CallbackTypedef` as a function pointer (`string_consumer_fn*`).
TEST_F(IrConvertTest, CallbackTypedefWorksWithStdFunction) {
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

    typedef int string_consumer_fn(
        const char* str [[clang::annotate("sandbox", "in_ptr")]]
                        [[clang::annotate("sandbox", "elem_sized_by", "len")]],
        unsigned long len);

    extern "C" void subscribe(std::function<string_consumer_fn> on_message);
  )",
                                               "subscribe"));
  ASSERT_EQ(func.parameters.size(), 1);
  ASSERT_TRUE(func.parameters[0].Is<CallbackParam>());
  const CallbackParam* cb = func.parameters[0].As<CallbackParam>();
  EXPECT_EQ(cb->functor_template_name, "std::function");
  ASSERT_NE(cb->return_value, nullptr);
  EXPECT_TRUE(cb->return_value->Is<ScalarParam>());
  ASSERT_EQ(cb->callback_parameters.size(), 2);
  EXPECT_EQ(cb->callback_parameters[0].name, "str");
  ASSERT_TRUE(cb->callback_parameters[0].Is<BufferParam>());
  EXPECT_EQ(cb->callback_parameters[0].direction(), PointerDir::kIn);
  EXPECT_EQ(cb->callback_parameters[1].name, "len");
  EXPECT_TRUE(cb->callback_parameters[1].Is<ScalarParam>());
}

TEST_F(IrConvertTest,
       CallbackTypedefReturningPointerWithoutLifetimeIsRejected) {
  EXPECT_THAT(
      ConvertSnippetToIR(R"(
    [[clang::annotate("sandbox", "elem_sized_by", "size")]]
    typedef unsigned char* (*chunk_allocator)(unsigned long size);
    extern "C" void compress(chunk_allocator alloc);
  )",
                         "compress"),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("chunk_allocator: callback typedef returns a "
                         "pointer, which requires lifetime_outer_call")));
}

TEST_F(IrConvertTest, CallbackTypedefWithPointerDirectionOnReturnIsRejected) {
  EXPECT_THAT(ConvertSnippetToIR(R"(
    [[clang::annotate("sandbox", "out_ptr")]]
    [[clang::annotate("sandbox", "lifetime_outer_call")]]
    [[clang::annotate("sandbox", "elem_sized_by", "size")]]
    typedef unsigned char* (*chunk_allocator)(unsigned long size);
    extern "C" void compress(chunk_allocator alloc);
  )",
                                 "compress"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("chunk_allocator: a pointer direction "
                                 "annotation is not supported on a callback "
                                 "typedef")));
}

TEST_F(IrConvertTest,
       CallbackTypedefWithSandboxGlobalLifetimeOnReturnIsRejected) {
  EXPECT_THAT(ConvertSnippetToIR(R"(
    [[clang::annotate("sandbox", "lifetime_sandbox_global")]]
    typedef unsigned char* (*chunk_allocator)(unsigned long size);
    extern "C" void compress(chunk_allocator alloc);
  )",
                                 "compress"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("chunk_allocator: lifetime_sandbox_global is "
                                 "not supported on a callback typedef")));
}

TEST_F(IrConvertTest,
       CallbackTypedefWithSizeAnnotationOnNonPointerReturnIsRejected) {
  EXPECT_THAT(ConvertSnippetToIR(R"(
    [[clang::annotate("sandbox", "elem_sized_by", "size")]]
    typedef int (*bad_typedef)(unsigned long size);
    extern "C" void compress(bad_typedef cb);
  )",
                                 "compress"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("bad_typedef: size annotations on a callback "
                                 "typedef are only supported when the "
                                 "callback returns a pointer")));
}

TEST_F(IrConvertTest, CallbackTypedefWithUnnamedParameterDefaultsName) {
  SAPI_ASSERT_OK_AND_ASSIGN(const Function func,
                            ConvertSnippetToIR(R"(
    typedef void (*unnamed_param_cb)(unsigned long);
    extern "C" void compress(unnamed_param_cb cb);
  )",
                                               "compress"));
  ASSERT_EQ(func.parameters.size(), 1);
  ASSERT_TRUE(func.parameters[0].Is<CallbackParam>());
  const CallbackParam* cb = func.parameters[0].As<CallbackParam>();
  ASSERT_EQ(cb->callback_parameters.size(), 1);
  EXPECT_EQ(cb->callback_parameters[0].name, "cb_arg0");
}

TEST_F(IrConvertTest, DuplicateAnnotatedCallbackTypedefIsRejected) {
  EXPECT_THAT(ConvertSnippetToIR(R"(
    typedef void (*byte_sink)(
        const unsigned char* buf [[clang::annotate("sandbox", "in_ptr")]],
        unsigned long size);
    typedef void (*byte_sink)(
        const unsigned char* buf [[clang::annotate("sandbox", "in_ptr")]],
        unsigned long size);

    extern "C" void compress(byte_sink sink);
  )",
                                 "compress"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("callback typedef byte_sink is annotated more "
                                 "than once")));
}

TEST_F(IrConvertTest, ASTToFunctionIRConversionWithUninitialized) {
  SAPI_ASSERT_OK_AND_ASSIGN(const Function func,
                            ConvertSnippetToIR(R"(
    [[clang::annotate("sandbox", "uninitialized")]]
    [[clang::annotate("sandbox", "lifetime_outer_call")]]
    typedef char* (*chunk_allocator)(int len);

    extern "C" [[clang::annotate("sandbox", "uninitialized")]]
    char* process_data(
        int size,
        chunk_allocator get_chunk);
  )",
                                               "process_data"));
  EXPECT_EQ(func.name, "process_data");
  ASSERT_TRUE(func.return_value.has_value());
  EXPECT_TRUE(func.return_value->uninitialized());
  ASSERT_TRUE(func.return_value->Is<BufferParam>());
  EXPECT_TRUE(func.return_value->As<BufferParam>()->uninitialized);
  // No direction is spelled anywhere, but a buffer handed back through a
  // return value travels out of the callee.
  EXPECT_EQ(func.return_value->As<BufferParam>()->direction, PointerDir::kOut);

  ASSERT_EQ(func.parameters.size(), 2);
  EXPECT_FALSE(func.parameters[0].uninitialized());
  EXPECT_TRUE(func.parameters[1].uninitialized());
  ASSERT_TRUE(func.parameters[1].Is<CallbackParam>());
  const CallbackParam* cb = func.parameters[1].As<CallbackParam>();
  EXPECT_TRUE(cb->uninitialized);
  // The callback's return value inherits `uninitialized` from its callback
  // typedef.
  ASSERT_NE(cb->return_value, nullptr);
  ASSERT_TRUE(cb->return_value->Is<BufferParam>());
  EXPECT_EQ(cb->return_value->As<BufferParam>()->direction, PointerDir::kOut);
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
  EXPECT_EQ(std::get<bounds::ElemCount>(sync.bounds).size_expr, "img->size");
}

TEST_F(IrConvertTest,
       StructSyncResolvesMultiFieldAndConstantAndNestedSizeExprs) {
  SAPI_ASSERT_OK_AND_ASSIGN(const Function func,
                            ConvertSnippetToIR(R"(
    using uint32_t = unsigned int;
    struct Limits {
      static constexpr int kExtra = 4;
    };
    struct GifImageDesc {
      int Width;
      int Height;
    };
    struct BaseHeader {
      int base_len;
    };
    struct SavedImage : BaseHeader {
      GifImageDesc ImageDesc;
      struct {
        int anon_len;
      };
      int count;
      unsigned char* RasterBits;
      unsigned char* Palette;
      uint32_t* Words;
    };
    inline void sandbox_annotation_1() {
      struct SavedImage {
        unsigned char* RasterBits [[clang::annotate(
            "sandbox", "elem_sized_by",
            "ImageDesc.Width * ImageDesc.Height")]];
        unsigned char* Palette
            [[clang::annotate("sandbox", "byte_sized_by", "256")]];
        uint32_t* Words [[clang::annotate(
            "sandbox", "byte_sized_by",
            "(count + anon_len + base_len) * sizeof(uint32_t) + Limits::kExtra")]];
      };
    }
    extern "C" void GifApplyTranslation(
        SavedImage* Image [[clang::annotate("sandbox", "inout_ptr")]]
        [[clang::annotate("sandbox", "struct_sync", "binding_1",
                           "{Image->RasterBits", "inout_ptr}",
                           "{Image->Palette", "in_ptr}",
                           "{Image->Words", "in_ptr}", "$")]]);
  )",
                                               "GifApplyTranslation"));
  ASSERT_EQ(func.parameters.size(), 1);
  const auto* sync_param = func.parameters[0].As<StructSyncParam>();
  ASSERT_NE(sync_param, nullptr);
  ASSERT_EQ(sync_param->members.size(), 3);

  ASSERT_TRUE(
      std::holds_alternative<bounds::ElemCount>(sync_param->members[0].bounds));
  EXPECT_EQ(
      std::get<bounds::ElemCount>(sync_param->members[0].bounds).size_expr,
      "Image->ImageDesc.Width * Image->ImageDesc.Height");

  ASSERT_TRUE(
      std::holds_alternative<bounds::ByteCount>(sync_param->members[1].bounds));
  EXPECT_EQ(
      std::get<bounds::ByteCount>(sync_param->members[1].bounds).size_expr,
      "256");

  ASSERT_TRUE(
      std::holds_alternative<bounds::ByteCount>(sync_param->members[2].bounds));
  EXPECT_EQ(
      std::get<bounds::ByteCount>(sync_param->members[2].bounds).size_expr,
      "(Image->count + Image->anon_len + Image->base_len) * sizeof(uint32_t) + "
      "Limits::kExtra");
}

TEST_F(IrConvertTest, ParseRecordAnnotationsRejectsUnknownMemberInSizeExpr) {
  EXPECT_THAT(
      ConvertSnippetToIR(R"(
    struct GifImageDesc {
      int Width;
      int Height;
    };
    struct SavedImage {
      GifImageDesc ImageDesc;
      unsigned char* RasterBits;
    };
    inline void sandbox_annotation_1() {
      struct SavedImage {
        unsigned char* RasterBits [[clang::annotate(
            "sandbox", "elem_sized_by",
            "TypoDesc.Width * ImageDesc.Height")]];
      };
    }
    extern "C" void GifApplyTranslation(
        SavedImage* Image [[clang::annotate("sandbox", "inout_ptr")]]);
  )",
                         "GifApplyTranslation"),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("field RasterBits in record SavedImage: size "
                         "expression 'TypoDesc.Width * ImageDesc.Height' "
                         "references unknown member 'TypoDesc'")));
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

// A callback handed back out of the sandbox has no `ParmVarDecl` anywhere to
// read its parameter names and annotations from, which is the gap a callback
// typedef fills.
TEST_F(IrConvertTest, RejectsReturnedCallbackWithoutCallbackTypedef) {
  EXPECT_THAT(ConvertSnippetToIR(R"(
    extern "C" void (*get_handler())(int);
  )",
                                 "get_handler"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("hands back a callback, so its return type "
                                 "must be a callback typedef")));
}

TEST_F(IrConvertTest, ConvertsReturnedCallbackNamingACallbackTypedef) {
  SAPI_ASSERT_OK_AND_ASSIGN(const Function func,
                            ConvertSnippetToIR(R"(
    typedef void (*handler_cb)(int value);
    extern "C" handler_cb get_handler();
  )",
                                               "get_handler"));
  ASSERT_TRUE(func.return_value.has_value());
  const auto* returned =
      std::get_if<SandboxeeCallbackParam>(&func.return_value->payload);
  ASSERT_NE(returned, nullptr);
  EXPECT_EQ(returned->typedef_name, "handler_cb");
}

TEST_F(IrConvertTest,
       ConvertsRedeclaredReturnedCallbackAfterRawFunctionPointerDeclaration) {
  SAPI_ASSERT_OK_AND_ASSIGN(const Function func,
                            ConvertSnippetToIR(R"(
    extern "C" void (*get_handler(int which))(int);
    typedef void (*handler_cb)(int value);
    extern "C" handler_cb get_handler(int which);
  )",
                                               "get_handler"));
  ASSERT_TRUE(func.return_value.has_value());
  const auto* returned =
      std::get_if<SandboxeeCallbackParam>(&func.return_value->payload);
  ASSERT_NE(returned, nullptr);
  EXPECT_EQ(returned->typedef_name, "handler_cb");
}

// Only a callback typedef that can be called in the reverse direction has an
// invoker for a host thunk to route through. Saying so at the use site is what
// keeps the alternative from being a link error against a thunk pool that was
// never emitted.
TEST_F(IrConvertTest, RejectsReturnedCallbackWhoseTypedefHasNoReverse) {
  EXPECT_THAT(ConvertSnippetToIR(
                  R"(
    [[clang::annotate("sandbox", "lifetime_outer_call")]]
    [[clang::annotate("sandbox", "elem_sized_by", "n")]]
    typedef char* (*alloc_cb)(int n);
    extern "C" alloc_cb get_allocator();
  )",
                  "get_allocator"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("hands back a callback, but callback typedef "
                                 "alloc_cb hands back 'char *'")));
}

// A callback typedef describes one callback, not a callback that itself hands
// back another callback.
TEST_F(IrConvertTest, RejectsCallbackThatReturnsACallback) {
  EXPECT_THAT(
      ConvertSnippetToIR(
          R"(
    typedef void (*inner_cb)(int);
    typedef inner_cb (*outer_cb)(int value);
    extern "C" void take_handler(outer_cb cb);
  )",
          "take_handler"),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("a callback that hands back another callback is not "
                         "supported")));
}

// A functor is an object with captured state that only means anything in the
// address space that built it, so no thunk can stand in for it.
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
                       HasSubstr("hands back C++ functor 'std::function'")));
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
    [[clang::annotate("sandbox", "lifetime_outer_call")]]
    [[clang::annotate("sandbox", "elem_sized_by", "size")]]
    typedef unsigned char* (*chunk_allocator)(unsigned long size);

    extern "C" {
    [[clang::annotate("sandbox", "alias_callback_return", "cb")]]
    unsigned char* process_data(
        const unsigned char* data [[clang::annotate("sandbox", "in_ptr")]]
            [[clang::annotate("sandbox", "elem_sized_by", "size")]],
        unsigned long size,
        chunk_allocator cb,
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

TEST_F(IrConvertTest, ValidateAndLinkLibraryIREmptyOutparamCapacityError) {
  SAPI_ASSERT_OK_AND_ASSIGN(const Function func,
                            ConvertSnippetToIR(R"(
    extern "C" void empty_cap(
        char* dst [[clang::annotate("sandbox", "out_ptr")]]
            [[clang::annotate("sandbox", "elem_sized_by_outparam", "*len", "")]],
        unsigned long* len [[clang::annotate("sandbox", "out_ptr")]]);
  )",
                                               "empty_cap"));

  Library lib{.name = "MyLib", .functions = {func}};
  EXPECT_THAT(ValidateAndLinkLibraryIR(lib),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("requires a non-empty capacity expression")));
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

TEST_F(IrConvertTest,
       CopyFromAndBindOutPtrSupportsTriviallyCopyableStructReturn) {
  SAPI_ASSERT_OK_AND_ASSIGN(const Function func,
                            ConvertSnippetToIR(R"(
    struct Ctx;
    struct Palette {
      unsigned char r;
      unsigned char g;
      unsigned char b;
    };
    extern "C" [[clang::annotate("sandbox", "copy_from_and_bind_out_ptr",
                                 "ctx", "b1")]]
    [[clang::annotate("sandbox", "elem_sized_by", "256")]]
    const Palette* get_palette(
        Ctx* ctx [[clang::annotate("sandbox", "sandbox_opaque_ptr")]]);
  )",
                                               "get_palette"));
  ASSERT_TRUE(func.return_value.has_value());
  ASSERT_TRUE(func.return_value->Is<BufferParam>());
  const auto* ret_buf = func.return_value->As<BufferParam>();
  EXPECT_TRUE(ret_buf->context_bound.copy_from_and_bind.has_value());
}

TEST_F(
    IrConvertTest,
    CopyFromAndBindOutPtrRejectsNonTriviallyCopyableStructReturnAndOutparam) {
  EXPECT_THAT(
      ConvertSnippetToIR(R"(
    struct Ctx;
    struct WithPtr {
      int* ptr;
    };
    extern "C" [[clang::annotate("sandbox", "copy_from_and_bind_out_ptr",
                                 "ctx", "b1")]]
    [[clang::annotate("sandbox", "elem_sized_by", "1")]]
    const WithPtr* get_with_ptr(
        Ctx* ctx [[clang::annotate("sandbox", "sandbox_opaque_ptr")]]);
  )",
                         "get_with_ptr"),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("return pointer sapi_ret_arg has unsupported pointee "
                         "type")));

  EXPECT_THAT(
      ConvertSnippetToIR(R"(
    extern "C" void get_with_ptr_outparam(
        Ctx* ctx [[clang::annotate("sandbox", "sandbox_opaque_ptr")]],
        WithPtr** out [[clang::annotate("sandbox",
                                        "copy_from_and_bind_out_ptr",
                                        "ctx", "b1")]]
            [[clang::annotate("sandbox", "elem_sized_by", "1")]]);
  )",
                         "get_with_ptr_outparam"),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("pointer argument out has unsupported pointee type")));

  EXPECT_THAT(
      ConvertSnippetToIR(R"(
    struct Pod {
      int x;
    };
    [[clang::annotate("sandbox", "lifetime_outer_call")]]
    [[clang::annotate("sandbox", "elem_sized_by", "1")]]
    typedef Pod* (*pod_allocator)(int n);
    extern "C" void use_pod_cb(pod_allocator cb);
  )",
                         "use_pod_cb"),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("return pointer return_val has unsupported pointee "
                         "type")));
}

TEST_F(IrConvertTest, ConvertsStructCallbackFieldNamingACallbackTypedef) {
  absl::flat_hash_map<std::string, RecordAnnotations> record_annotations;
  RecordAnnotations hooks_ann{.name = "Hooks"};
  hooks_ann.member_annotations.push_back(DataMemberAnnotations{
      .name = "cb",
      .callback_typedef = "adder",
  });
  record_annotations["Hooks"] = std::move(hooks_ann);

  const absl::flat_hash_map<std::string, CallbackTypedef> typedefs = {
      {"adder",
       CallbackTypedef{
           .name = "adder",
           .canonical_type = "int (int, int)",
           .params = {{.name = "a"}, {.name = "b"}},
       }},
  };

  SAPI_ASSERT_OK_AND_ASSIGN(
      const Function func,
      ConvertSnippetToIR(R"(
    struct Hooks {
      int (*cb)(int, int);
      int tag;
    };
    extern "C" void set_hooks(
        const Hooks* h [[clang::annotate("sandbox", "in_ptr")]]);
  )",
                         "set_hooks", record_annotations, typedefs));
  ASSERT_EQ(func.parameters.size(), 1);
  ASSERT_TRUE(func.parameters[0].Is<StructSyncParam>());
  const auto* sync = func.parameters[0].As<StructSyncParam>();
  EXPECT_EQ(sync->direction, PointerDir::kIn);
  ASSERT_EQ(sync->callbacks.size(), 1);
  EXPECT_EQ(sync->callbacks[0].member_name, "cb");
  EXPECT_EQ(sync->callbacks[0].typedef_name, "adder");
}

TEST_F(IrConvertTest, RejectsStructCallbackFieldNamingUnknownCallbackTypedef) {
  absl::flat_hash_map<std::string, RecordAnnotations> record_annotations;
  RecordAnnotations hooks_ann{.name = "Hooks"};
  hooks_ann.member_annotations.push_back(DataMemberAnnotations{
      .name = "cb",
      .callback_typedef = "no_such_typedef",
  });
  record_annotations["Hooks"] = std::move(hooks_ann);

  EXPECT_THAT(ConvertSnippetToIR(R"(
    struct Hooks {
      int (*cb)(int, int);
    };
    extern "C" void set_hooks(
        const Hooks* h [[clang::annotate("sandbox", "in_ptr")]]);
  )",
                                 "set_hooks", record_annotations),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("callback field cb of 'Hooks' names unknown "
                                 "callback typedef no_such_typedef")));
}

TEST_F(IrConvertTest,
       RejectsStructCallbackFieldWithCallbackTypedefShapeMismatch) {
  absl::flat_hash_map<std::string, RecordAnnotations> record_annotations;
  RecordAnnotations hooks_ann{.name = "Hooks"};
  hooks_ann.member_annotations.push_back(DataMemberAnnotations{
      .name = "cb",
      .callback_typedef = "adder",
  });
  record_annotations["Hooks"] = std::move(hooks_ann);

  const absl::flat_hash_map<std::string, CallbackTypedef> typedefs = {
      {"adder",
       CallbackTypedef{
           .name = "adder",
           .canonical_type = "int (int, int)",
           .params = {{.name = "a"}, {.name = "b"}},
       }},
  };

  EXPECT_THAT(
      ConvertSnippetToIR(R"(
    struct Hooks {
      int (*cb)(int);
    };
    extern "C" void set_hooks(
        const Hooks* h [[clang::annotate("sandbox", "in_ptr")]]);
  )",
                         "set_hooks", record_annotations, typedefs),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("callback field cb of 'Hooks' has type "
                         "'int (int)', which does not match callback typedef "
                         "adder of type 'int (int, int)'")));
}

TEST_F(IrConvertTest, RejectsOutStructCallbackFieldWhoseTypedefHasNoReverse) {
  absl::flat_hash_map<std::string, RecordAnnotations> record_annotations;
  RecordAnnotations hooks_ann{.name = "Hooks"};
  hooks_ann.member_annotations.push_back(DataMemberAnnotations{
      .name = "alloc",
      .callback_typedef = "chunk_allocator",
  });
  record_annotations["Hooks"] = std::move(hooks_ann);

  const absl::flat_hash_map<std::string, CallbackTypedef> typedefs = {
      {"chunk_allocator",
       CallbackTypedef{
           .name = "chunk_allocator",
           .canonical_type = "char *(unsigned long)",
           .params = {{.name = "size"}},
           .no_reverse_reason = "it hands back a pointer",
       }},
  };

  EXPECT_THAT(ConvertSnippetToIR(R"(
    struct Hooks {
      char* (*alloc)(unsigned long);
    };
    extern "C" void get_hooks(
        Hooks* h [[clang::annotate("sandbox", "out_ptr")]]);
  )",
                                 "get_hooks", record_annotations, typedefs),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("field alloc of 'Hooks' hands back a "
                                 "callback, but it hands back a pointer")));
}

TEST_F(IrConvertTest, RejectsArrayOfStructsWithCallbackFields) {
  absl::flat_hash_map<std::string, RecordAnnotations> record_annotations;
  RecordAnnotations hooks_ann{.name = "Hooks"};
  hooks_ann.member_annotations.push_back(DataMemberAnnotations{
      .name = "cb",
      .callback_typedef = "adder",
  });
  record_annotations["Hooks"] = std::move(hooks_ann);

  const absl::flat_hash_map<std::string, CallbackTypedef> typedefs = {
      {"adder",
       CallbackTypedef{
           .name = "adder",
           .canonical_type = "int (int, int)",
           .params = {{.name = "a"}, {.name = "b"}},
       }},
  };

  EXPECT_THAT(
      ConvertSnippetToIR(R"(
    struct Hooks {
      int (*cb)(int, int);
    };
    extern "C" void set_hooks_array(
        const Hooks* h [[clang::annotate("sandbox", "in_ptr")]]
                       [[clang::annotate("sandbox", "elem_sized_by", "n")]],
        unsigned long n);
  )",
                         "set_hooks_array", record_annotations, typedefs),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("h: array of structs with callback fields is "
                         "not supported")));
}

TEST_F(IrConvertTest, RejectsUncallableReturnedCallbackSignature) {
  EXPECT_THAT(ConvertSnippetToIR(
                  R"(
    typedef double (*float_cb)(double x);
    extern "C" float_cb get_handler();
  )",
                  "get_handler"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("callback sapi_ret_arg parameter 1 has type "
                                 "'double'")));
}

TEST_F(IrConvertTest,
       ValidateAndLinkCallbackInvokerReportsTypedefNameAndExcludesTarget) {
  Function invoker{
      .name = "sapi_invoke_byte_sink",
      .parameters =
          {
              Parameter{
                  .name = "sapi_target",
                  .type = {.kind = TypeKind::kScalar,
                           .canonical_name = "uintptr_t"},
                  .payload = ScalarParam{},
              },
              Parameter{
                  .name = "buf",
                  .type = {.kind = TypeKind::kPointer,
                           .canonical_name = "const char *"},
                  .payload =
                      BufferParam{
                          .direction = PointerDir::kIn,
                          .bounds =
                              bounds::ElemCount{.size_expr = "sapi_target"},
                      },
              },
          },
      .invoker_for_typedef = "byte_sink",
  };

  Library lib{.name = "MyLib", .functions = {invoker}};
  EXPECT_THAT(
      ValidateAndLinkLibraryIR(lib),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("callback typedef byte_sink parameter buf references "
                         "non-existent sibling parameter sapi_target")));
}

TEST_F(IrConvertTest, ConvertsCallbackIdempotentOnInlineCallbackAndTypedef) {
  SAPI_ASSERT_OK_AND_ASSIGN(const Function func,
                            ConvertSnippetToIR(R"(
    [[clang::annotate("sandbox", "callback_idempotent")]]
    typedef int (*multi_cb)(int x);
    extern "C" void run_callbacks(
        int (*single_cb)(int),
        [[clang::annotate("sandbox", "callback_idempotent")]]
        int (*inline_multi_cb)(int),
        multi_cb td_multi_cb);
  )",
                                               "run_callbacks"));
  ASSERT_EQ(func.parameters.size(), 3);
  ASSERT_TRUE(func.parameters[0].Is<CallbackParam>());
  EXPECT_FALSE(func.parameters[0].As<CallbackParam>()->idempotent);
  ASSERT_TRUE(func.parameters[1].Is<CallbackParam>());
  EXPECT_TRUE(func.parameters[1].As<CallbackParam>()->idempotent);
  ASSERT_TRUE(func.parameters[2].Is<CallbackParam>());
  EXPECT_TRUE(func.parameters[2].As<CallbackParam>()->idempotent);
}

TEST_F(IrConvertTest,
       RejectsCallbackIdempotentOnNonCallbackAndTypedefParameter) {
  EXPECT_THAT(ConvertSnippetToIR(R"(
    extern "C" void bad_param(
        int x [[clang::annotate("sandbox", "callback_idempotent")]]);
  )",
                                 "bad_param"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("callback_idempotent can only be written on a "
                                 "callback or a callback typedef")));

  EXPECT_THAT(
      ConvertSnippetToIR(R"(
    typedef int (*adder)(int a, int b);
    extern "C" void bad_td_param(
        [[clang::annotate("sandbox", "callback_idempotent")]] adder cb);
  )",
                         "bad_td_param"),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("callback_idempotent cannot be placed on a callback "
                         "parameter that uses callback typedef adder; move "
                         "the annotation to the typedef")));

  EXPECT_THAT(ConvertSnippetToIR(R"(
    typedef int (*adder)(int a, int b);
    extern "C" [[clang::annotate("sandbox", "callback_idempotent")]]
    adder bad_return_td();
  )",
                                 "bad_return_td"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("callback_idempotent can only be written on a "
                                 "callback or a callback typedef")));

  EXPECT_THAT(ConvertSnippetToIR(R"(
    extern "C" [[clang::annotate("sandbox", "callback_idempotent")]]
    int (*bad_return_raw())(int);
  )",
                                 "bad_return_raw"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("callback_idempotent can only be written on a "
                                 "callback or a callback typedef")));
}

TEST_F(IrConvertTest, ConvertsNestedStructCallbackField) {
  absl::flat_hash_map<std::string, RecordAnnotations> record_annotations;
  RecordAnnotations inner_ann{.name = "Inner"};
  inner_ann.member_annotations.push_back(DataMemberAnnotations{
      .name = "cb",
      .callback_typedef = "adder",
  });
  record_annotations["Inner"] = std::move(inner_ann);

  const absl::flat_hash_map<std::string, CallbackTypedef> typedefs = {
      {"adder",
       CallbackTypedef{
           .name = "adder",
           .canonical_type = "int (int, int)",
           .params = {{.name = "a"}, {.name = "b"}},
       }},
  };

  SAPI_ASSERT_OK_AND_ASSIGN(
      const Function func,
      ConvertSnippetToIR(R"(
    struct Inner {
      int (*cb)(int, int);
    };
    struct Outer {
      Inner inner;
      int extra;
    };
    extern "C" void set_outer(
        const Outer* o [[clang::annotate("sandbox", "in_ptr")]]);
  )",
                         "set_outer", record_annotations, typedefs));
  ASSERT_EQ(func.parameters.size(), 1);
  ASSERT_TRUE(func.parameters[0].Is<StructSyncParam>());
  const auto* sync = func.parameters[0].As<StructSyncParam>();
  ASSERT_EQ(sync->callbacks.size(), 1);
  EXPECT_EQ(sync->callbacks[0].member_name, "inner.cb");
  EXPECT_EQ(sync->callbacks[0].typedef_name, "adder");
}

TEST_F(IrConvertTest, RejectsConstantArrayOfStructWithCallbackField) {
  absl::flat_hash_map<std::string, RecordAnnotations> record_annotations;
  RecordAnnotations inner_ann{.name = "Inner"};
  inner_ann.member_annotations.push_back(DataMemberAnnotations{
      .name = "cb",
      .callback_typedef = "adder",
  });
  record_annotations["Inner"] = std::move(inner_ann);

  const absl::flat_hash_map<std::string, CallbackTypedef> typedefs = {
      {"adder",
       CallbackTypedef{
           .name = "adder",
           .canonical_type = "int (int, int)",
           .params = {{.name = "a"}, {.name = "b"}},
       }},
  };

  EXPECT_THAT(ConvertSnippetToIR(R"(
    struct Inner {
      int (*cb)(int, int);
    };
    struct Outer {
      Inner arr[2];
    };
    extern "C" void set_outer(
        const Outer* o [[clang::annotate("sandbox", "in_ptr")]]);
  )",
                                 "set_outer", record_annotations, typedefs),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("callback fields inside an array are not "
                                 "supported")));
}

TEST_F(IrConvertTest,
       RejectsConstantArrayOfStructWithCallbackFieldUnderStructSync) {
  absl::flat_hash_map<std::string, RecordAnnotations> record_annotations;
  RecordAnnotations inner_ann{.name = "Inner"};
  inner_ann.member_annotations.push_back(DataMemberAnnotations{
      .name = "cb",
      .callback_typedef = "adder",
  });
  record_annotations["Inner"] = std::move(inner_ann);

  const absl::flat_hash_map<std::string, CallbackTypedef> typedefs = {
      {"adder",
       CallbackTypedef{
           .name = "adder",
           .canonical_type = "int (int, int)",
           .params = {{.name = "a"}, {.name = "b"}},
       }},
  };

  EXPECT_THAT(
      ConvertSnippetToIR(R"(
    struct Inner {
      int (*cb)(int, int);
    };
    struct SyncedOuter {
      const char* name;
      Inner arr[2];
    };
    extern "C" void set_synced_outer(
        const SyncedOuter* o
        [[clang::annotate("sandbox", "in_ptr")]]
        [[clang::annotate("sandbox", "struct_sync", "b1",
                          "{o->name, in_ptr}", "$")]]);
  )",
                         "set_synced_outer", record_annotations, typedefs),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("callback fields inside an array are not "
                         "supported")));
}

TEST_F(IrConvertTest, RejectsReturnPointerToStructWithCallbackField) {
  absl::flat_hash_map<std::string, RecordAnnotations> record_annotations;
  RecordAnnotations hooks_ann{.name = "Hooks"};
  hooks_ann.member_annotations.push_back(DataMemberAnnotations{
      .name = "cb",
      .callback_typedef = "adder",
  });
  record_annotations["Hooks"] = std::move(hooks_ann);

  const absl::flat_hash_map<std::string, CallbackTypedef> typedefs = {
      {"adder",
       CallbackTypedef{
           .name = "adder",
           .canonical_type = "int (int, int)",
           .params = {{.name = "a"}, {.name = "b"}},
       }},
  };

  EXPECT_THAT(
      ConvertSnippetToIR(R"(
    struct Hooks {
      int (*cb)(int, int);
    };
    extern "C" {
    [[clang::annotate("sandbox", "alias_ptr", "h")]]
    Hooks* passthrough_hooks(
        Hooks* h [[clang::annotate("sandbox", "inout_ptr")]]);
    }
  )",
                         "passthrough_hooks", record_annotations, typedefs),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("structs with callback fields cannot be "
                         "returned by pointer")));
}

TEST_F(IrConvertTest, ConvertsTwoLevelPointerWithPointeeContract) {
  SAPI_ASSERT_OK_AND_ASSIGN(const Function func,
                            ConvertSnippetToIR(R"(
    struct DbHandle;
    typedef DbHandle* db_handle_pointee
        [[clang::annotate("sandbox", "sandbox_opaque_ptr")]];
    typedef const char* c_str_in_pointee
        [[clang::annotate("sandbox", "in_ptr")]]
        [[clang::annotate("sandbox", "null_terminated")]];
    extern "C" int open_db_with_args(
        int argc,
        const c_str_in_pointee* argv
            [[clang::annotate("sandbox", "in_ptr")]]
            [[clang::annotate("sandbox", "elem_sized_by", "argc")]],
        db_handle_pointee* out_db
            [[clang::annotate("sandbox", "out_ptr")]]);
  )",
                                               "open_db_with_args"));
  ASSERT_EQ(func.parameters.size(), 3);

  const auto* argv_buf = func.parameters[1].As<BufferParam>();
  ASSERT_NE(argv_buf, nullptr);
  EXPECT_EQ(argv_buf->direction, PointerDir::kIn);
  ASSERT_TRUE(std::holds_alternative<bounds::ElemCount>(argv_buf->bounds));
  EXPECT_EQ(std::get<bounds::ElemCount>(argv_buf->bounds).size_expr, "argc");
  ASSERT_NE(argv_buf->pointee, nullptr);
  const auto* inner_str = argv_buf->pointee->As<BufferParam>();
  ASSERT_NE(inner_str, nullptr);
  EXPECT_EQ(inner_str->direction, PointerDir::kIn);
  EXPECT_TRUE(
      std::holds_alternative<bounds::NullTerminated>(inner_str->bounds));

  const auto* out_db_buf = func.parameters[2].As<BufferParam>();
  ASSERT_NE(out_db_buf, nullptr);
  EXPECT_EQ(out_db_buf->direction, PointerDir::kOut);
  EXPECT_TRUE(std::holds_alternative<bounds::Singleton>(out_db_buf->bounds));
  ASSERT_NE(out_db_buf->pointee, nullptr);
  const auto* inner_db = out_db_buf->pointee->As<OpaquePointerParam>();
  ASSERT_NE(inner_db, nullptr);
  EXPECT_EQ(inner_db->direction, PointerDir::kSandboxOpaque);

  Library lib{.name = "MyLib", .functions = {func}};
  EXPECT_THAT(ValidateAndLinkLibraryIR(lib), IsOk());
}

TEST_F(IrConvertTest, ConvertsThreeLevelPointerWithChainedPointeeContracts) {
  SAPI_ASSERT_OK_AND_ASSIGN(const Function func,
                            ConvertSnippetToIR(R"(
    typedef int* row_pointee
        [[clang::annotate("sandbox", "inout_ptr")]]
        [[clang::annotate("sandbox", "elem_sized_by", "cols")]];
    typedef row_pointee* matrix_pointee
        [[clang::annotate("sandbox", "inout_ptr")]]
        [[clang::annotate("sandbox", "elem_sized_by", "rows")]];
    extern "C" void transform_matrix_handle(
        matrix_pointee* matrix_ptr
            [[clang::annotate("sandbox", "inout_ptr")]],
        unsigned long rows,
        unsigned long cols);
  )",
                                               "transform_matrix_handle"));
  ASSERT_EQ(func.parameters.size(), 3);
  const auto* level1 = func.parameters[0].As<BufferParam>();
  ASSERT_NE(level1, nullptr);
  EXPECT_EQ(level1->direction, PointerDir::kInOut);
  EXPECT_TRUE(std::holds_alternative<bounds::Singleton>(level1->bounds));

  ASSERT_NE(level1->pointee, nullptr);
  const auto* level2 = level1->pointee->As<BufferParam>();
  ASSERT_NE(level2, nullptr);
  EXPECT_EQ(level2->direction, PointerDir::kInOut);
  ASSERT_TRUE(std::holds_alternative<bounds::ElemCount>(level2->bounds));
  EXPECT_EQ(std::get<bounds::ElemCount>(level2->bounds).size_expr, "rows");

  ASSERT_NE(level2->pointee, nullptr);
  const auto* level3 = level2->pointee->As<BufferParam>();
  ASSERT_NE(level3, nullptr);
  EXPECT_EQ(level3->direction, PointerDir::kInOut);
  ASSERT_TRUE(std::holds_alternative<bounds::ElemCount>(level3->bounds));
  EXPECT_EQ(std::get<bounds::ElemCount>(level3->bounds).size_expr, "cols");
  EXPECT_EQ(level3->pointee, nullptr);

  Library lib{.name = "MyLib", .functions = {func}};
  EXPECT_THAT(ValidateAndLinkLibraryIR(lib), IsOk());
}

TEST_F(IrConvertTest,
       ValidateAndLinkLibraryIRNestedPointeeContractMissingSiblingError) {
  SAPI_ASSERT_OK_AND_ASSIGN(const Function func,
                            ConvertSnippetToIR(R"(
    typedef int* row_pointee
        [[clang::annotate("sandbox", "inout_ptr")]]
        [[clang::annotate("sandbox", "elem_sized_by", "missing_cols")]];
    typedef row_pointee* matrix_pointee
        [[clang::annotate("sandbox", "inout_ptr")]]
        [[clang::annotate("sandbox", "elem_sized_by", "rows")]];
    extern "C" void transform_matrix_handle(
        matrix_pointee* matrix_ptr
            [[clang::annotate("sandbox", "inout_ptr")]],
        unsigned long rows);
  )",
                                               "transform_matrix_handle"));

  Library lib{.name = "MyLib", .functions = {func}};
  EXPECT_THAT(ValidateAndLinkLibraryIR(lib),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("references non-existent sibling parameter "
                                 "missing_cols")));
}

TEST_F(IrConvertTest, RejectsMultiLevelPointerWithoutPointeeContract) {
  EXPECT_THAT(ConvertSnippetToIR(R"(
    extern "C" void take_double_ptr(
        int** out_ptr [[clang::annotate("sandbox", "out_ptr")]]);
  )",
                                 "take_double_ptr"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("pointer argument out_ptr has unsupported "
                                 "pointee type: 'int *' is a pointer")));
}

TEST_F(IrConvertTest, RejectsPointeeTypedefWithoutDirection) {
  EXPECT_THAT(ConvertSnippetToIR(R"(
    typedef int* bad_pointee [[clang::annotate("sandbox", "elem_sized_by", "n")]];
    extern "C" void take_double_ptr(
        bad_pointee* out_ptr [[clang::annotate("sandbox", "out_ptr")]],
        unsigned long n);
  )",
                                 "take_double_ptr"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("pointee typedef bad_pointee with type "
                                 "'int *': missing pointer direction "
                                 "annotation")));
}

TEST_F(IrConvertTest, RejectsMultiLevelPointerWithUnannotatedTypedef) {
  EXPECT_THAT(ConvertSnippetToIR(R"(
    typedef int* unannotated_ptr;
    extern "C" void take_double_ptr(
        unannotated_ptr* out_ptr [[clang::annotate("sandbox", "out_ptr")]]);
  )",
                                 "take_double_ptr"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("pointer argument out_ptr has unsupported "
                                 "pointee type: 'unannotated_ptr' is a "
                                 "pointer")));
}

TEST_F(IrConvertTest, RejectsFourLevelPointerAndReturnPointeeContract) {
  EXPECT_THAT(ConvertSnippetToIR(R"(
    typedef int* l1_td [[clang::annotate("sandbox", "inout_ptr")]];
    typedef l1_td* l2_td [[clang::annotate("sandbox", "inout_ptr")]];
    typedef l2_td* l3_td [[clang::annotate("sandbox", "inout_ptr")]];
    extern "C" void take_4level(
        l3_td* p [[clang::annotate("sandbox", "inout_ptr")]]);
  )",
                                 "take_4level"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("multi-level pointers deeper than 3 levels "
                                 "are not supported")));

  EXPECT_THAT(ConvertSnippetToIR(R"(
    typedef int* ret_td [[clang::annotate("sandbox", "inout_ptr")]];
    extern "C" [[clang::annotate("sandbox", "out_ptr")]]
    ret_td* ret_pointee();
  )",
                                 "ret_pointee"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("return pointer sapi_ret_arg has unsupported "
                                 "pointee type")));
}

TEST_F(IrConvertTest,
       RejectsHostOpaqueOutAndOutWrapperAroundCallerAllocatedPointee) {
  EXPECT_THAT(ConvertSnippetToIR(R"(
    typedef void* host_ctx_td
        [[clang::annotate("sandbox", "host_opaque_ptr")]];
    typedef int* caller_row_out_td
        [[clang::annotate("sandbox", "out_ptr")]]
        [[clang::annotate("sandbox", "elem_sized_by", "cols")]];
    typedef caller_row_out_td* bad_middle_out_td
        [[clang::annotate("sandbox", "out_ptr")]]
        [[clang::annotate("sandbox", "elem_sized_by", "rows")]];
    extern "C" void bad_host_opaque_out(
        host_ctx_td* out_ctx [[clang::annotate("sandbox", "out_ptr")]]);
    extern "C" void bad_host_opaque_inout(
        host_ctx_td* inout_ctx [[clang::annotate("sandbox", "inout_ptr")]]);
    extern "C" void ok_host_opaque_in(
        const host_ctx_td* in_ctx [[clang::annotate("sandbox", "in_ptr")]]);
    extern "C" void bad_caller_row_out(
        caller_row_out_td* out_row [[clang::annotate("sandbox", "out_ptr")]],
        unsigned long cols);
    extern "C" void ok_caller_row_in(
        caller_row_out_td* out_row [[clang::annotate("sandbox", "in_ptr")]],
        unsigned long cols);
    extern "C" void bad_3level_middle_out(
        bad_middle_out_td* mat [[clang::annotate("sandbox", "inout_ptr")]],
        unsigned long rows, unsigned long cols);
  )",
                                 "bad_host_opaque_out"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("pointer argument out_ctx: host_opaque_ptr "
                                 "pointee typedef host_ctx_td is not supported "
                                 "on output or in-out multi-level pointers")));
  EXPECT_THAT(ConvertSnippetToIR("", "bad_host_opaque_inout"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("pointer argument inout_ctx: host_opaque_ptr "
                                 "pointee typedef host_ctx_td is not supported "
                                 "on output or in-out multi-level pointers")));
  EXPECT_THAT(ConvertSnippetToIR("", "ok_host_opaque_in"), IsOk());
  EXPECT_THAT(ConvertSnippetToIR("", "bad_caller_row_out"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("pointer argument out_row: SANDBOX_OUT_PTR "
                                 "cannot wrap caller-allocated pointee typedef "
                                 "caller_row_out_td")));
  EXPECT_THAT(ConvertSnippetToIR("", "ok_caller_row_in"), IsOk());
  EXPECT_THAT(ConvertSnippetToIR("", "bad_3level_middle_out"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("pointee typedef bad_middle_out_td: "
                                 "SANDBOX_OUT_PTR cannot wrap caller-allocated "
                                 "pointee typedef caller_row_out_td")));
}

TEST_F(IrConvertTest,
       QualifiesSharedCopyFromAndBindPointeeTypedefPerFunctionAndParam) {
  const char* snippet = R"(
    struct DbHandle;
    typedef const int* bound_pair_td
        [[clang::annotate("sandbox", "out_ptr")]]
        [[clang::annotate("sandbox", "elem_sized_by", "2")]]
        [[clang::annotate("sandbox", "copy_from_and_bind_out_ptr",
                          "db", "binding_10")]];
    extern "C" void get_two_slices(
        DbHandle* db [[clang::annotate("sandbox", "sandbox_opaque_ptr")]],
        bound_pair_td* out_a [[clang::annotate("sandbox", "out_ptr")]],
        bound_pair_td* out_b [[clang::annotate("sandbox", "out_ptr")]]);
    extern "C" void get_other_slice(
        DbHandle* db [[clang::annotate("sandbox", "sandbox_opaque_ptr")]],
        bound_pair_td* out_a [[clang::annotate("sandbox", "out_ptr")]]);
  )";
  SAPI_ASSERT_OK_AND_ASSIGN(const Function fn1,
                            ConvertSnippetToIR(snippet, "get_two_slices"));
  SAPI_ASSERT_OK_AND_ASSIGN(const Function fn2,
                            ConvertSnippetToIR("", "get_other_slice"));

  const auto* a1 =
      fn1.parameters[1].As<BufferParam>()->pointee->As<BufferParam>();
  const auto* b1 =
      fn1.parameters[2].As<BufferParam>()->pointee->As<BufferParam>();
  const auto* a2 =
      fn2.parameters[1].As<BufferParam>()->pointee->As<BufferParam>();
  ASSERT_TRUE(a1->context_bound.copy_from_and_bind.has_value());
  ASSERT_TRUE(b1->context_bound.copy_from_and_bind.has_value());
  ASSERT_TRUE(a2->context_bound.copy_from_and_bind.has_value());
  EXPECT_EQ(a1->context_bound.copy_from_and_bind->binding_name,
            "get_two_slices_out_a_binding_10");
  EXPECT_EQ(b1->context_bound.copy_from_and_bind->binding_name,
            "get_two_slices_out_b_binding_10");
  EXPECT_EQ(a2->context_bound.copy_from_and_bind->binding_name,
            "get_other_slice_out_a_binding_10");
}

TEST_F(IrConvertTest, RejectsUnsupportedOuterAnnotationsWithPointeeContract) {
  EXPECT_THAT(ConvertSnippetToIR(R"(
    struct Ctx;
    typedef int* item_td [[clang::annotate("sandbox", "inout_ptr")]];
    extern "C" void bad_outparam_sized(
        item_td* items [[clang::annotate("sandbox", "out_ptr")]]
                       [[clang::annotate("sandbox", "elem_sized_by_outparam",
                                         "*out_n", "cap")]],
        unsigned long* out_n [[clang::annotate("sandbox", "out_ptr")]],
        unsigned long cap);
    extern "C" void bad_binding_sized(
        Ctx* ctx [[clang::annotate("sandbox", "sandbox_opaque_ptr")]],
        item_td* items [[clang::annotate("sandbox", "out_ptr")]]
                       [[clang::annotate("sandbox", "sized_by_binding",
                                         "ctx", "$sz")]]);
    extern "C" void bad_outer_lifetime(
        item_td* items [[clang::annotate("sandbox", "out_ptr")]]
                       [[clang::annotate("sandbox",
                                         "lifetime_sandbox_global")]]);
    extern "C" void bad_outer_clear_bindings(
        item_td* items [[clang::annotate("sandbox", "inout_ptr")]]
                       [[clang::annotate("sandbox", "clear_bindings")]]);
  )",
                                 "bad_outparam_sized"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("pointer argument items: sized_by_outparam "
                                 "and sized_by_binding cannot be combined "
                                 "with a pointee contract on the outer "
                                 "pointer")));
  EXPECT_THAT(ConvertSnippetToIR("", "bad_binding_sized"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("pointer argument items: sized_by_outparam "
                                 "and sized_by_binding cannot be combined "
                                 "with a pointee contract on the outer "
                                 "pointer")));
  EXPECT_THAT(ConvertSnippetToIR("", "bad_outer_lifetime"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("pointer argument items: lifetime annotations "
                                 "cannot be combined with a pointee contract "
                                 "on the outer pointer")));
  EXPECT_THAT(ConvertSnippetToIR("", "bad_outer_clear_bindings"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("pointer argument items: bind_data and "
                                 "clear_bindings cannot be combined with a "
                                 "pointee contract on the outer pointer")));
}

TEST_F(IrConvertTest,
       RejectsNonOutOuterPointerWithOutputOnlyCalleeWrittenPointeeTypedef) {
  constexpr absl::string_view kSnippet = R"(
    struct DbHandle;
    typedef const int* bound_pair_td
        [[clang::annotate("sandbox", "out_ptr")]]
        [[clang::annotate("sandbox", "elem_sized_by", "2")]]
        [[clang::annotate("sandbox", "copy_from_and_bind_out_ptr",
                          "db", "binding_10")]];
    typedef const char* global_cstr_out_td
        [[clang::annotate("sandbox", "out_ptr")]]
        [[clang::annotate("sandbox", "null_terminated")]]
        [[clang::annotate("sandbox", "lifetime_sandbox_global")]];
    typedef const char* global_cstr_inout_td
        [[clang::annotate("sandbox", "inout_ptr")]]
        [[clang::annotate("sandbox", "null_terminated")]]
        [[clang::annotate("sandbox", "lifetime_sandbox_global")]];
    typedef const global_cstr_out_td* bad_middle_in_td
        [[clang::annotate("sandbox", "in_ptr")]];
    typedef global_cstr_out_td* bad_middle_inout_td
        [[clang::annotate("sandbox", "inout_ptr")]];

    extern "C" void bad_bound_in(
        DbHandle* db [[clang::annotate("sandbox", "sandbox_opaque_ptr")]],
        const bound_pair_td* p [[clang::annotate("sandbox", "in_ptr")]]);
    extern "C" void bad_bound_inout(
        DbHandle* db [[clang::annotate("sandbox", "sandbox_opaque_ptr")]],
        bound_pair_td* p [[clang::annotate("sandbox", "inout_ptr")]]);
    extern "C" void bad_global_out_in(
        const global_cstr_out_td* p [[clang::annotate("sandbox", "in_ptr")]]);
    extern "C" void bad_global_out_inout(
        global_cstr_out_td* p [[clang::annotate("sandbox", "inout_ptr")]]);
    extern "C" void bad_3level_middle_in(
        bad_middle_in_td* p [[clang::annotate("sandbox", "in_ptr")]]);
    extern "C" void bad_3level_middle_inout(
        bad_middle_inout_td* p [[clang::annotate("sandbox", "out_ptr")]]);
    extern "C" void ok_global_out_out(
        global_cstr_out_td* p [[clang::annotate("sandbox", "out_ptr")]]);
    extern "C" void ok_global_inout_in(
        const global_cstr_inout_td* p [[clang::annotate("sandbox", "in_ptr")]]);
    extern "C" void ok_global_inout_inout(
        global_cstr_inout_td* p [[clang::annotate("sandbox", "inout_ptr")]]);
  )";
  EXPECT_THAT(
      ConvertSnippetToIR(kSnippet, "bad_bound_in"),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("pointer argument p: output-only callee-written "
                         "pointee typedef bound_pair_td requires "
                         "SANDBOX_OUT_PTR on the wrapping pointer")));
  EXPECT_THAT(
      ConvertSnippetToIR("", "bad_bound_inout"),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("pointer argument p: output-only callee-written "
                         "pointee typedef bound_pair_td requires "
                         "SANDBOX_OUT_PTR on the wrapping pointer")));
  EXPECT_THAT(
      ConvertSnippetToIR("", "bad_global_out_in"),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("pointer argument p: output-only callee-written "
                         "pointee typedef global_cstr_out_td requires "
                         "SANDBOX_OUT_PTR on the wrapping pointer")));
  EXPECT_THAT(
      ConvertSnippetToIR("", "bad_global_out_inout"),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("pointer argument p: output-only callee-written "
                         "pointee typedef global_cstr_out_td requires "
                         "SANDBOX_OUT_PTR on the wrapping pointer")));
  EXPECT_THAT(
      ConvertSnippetToIR("", "bad_3level_middle_in"),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("pointee typedef bad_middle_in_td: output-only "
                         "callee-written pointee typedef global_cstr_out_td "
                         "requires SANDBOX_OUT_PTR on the wrapping pointer")));
  EXPECT_THAT(
      ConvertSnippetToIR("", "bad_3level_middle_inout"),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("pointee typedef bad_middle_inout_td: output-only "
                         "callee-written pointee typedef global_cstr_out_td "
                         "requires SANDBOX_OUT_PTR on the wrapping pointer")));
  EXPECT_THAT(ConvertSnippetToIR("", "ok_global_out_out"), IsOk());
  EXPECT_THAT(ConvertSnippetToIR("", "ok_global_inout_in"), IsOk());
  EXPECT_THAT(ConvertSnippetToIR("", "ok_global_inout_inout"), IsOk());
}

}  // namespace
}  // namespace ir
}  // namespace sapi
