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

// Unit tests for the IR code generator.
//
// These tests build ir::Library values directly instead of running the Clang
// frontend, so each one states the exact IR shape it is about. They assert on
// the tokens that carry meaning -- CheckedMultiply, PtrBefore, RetainPointer --
// rather than on whole formatted lines, which is what the golden files in
// tests/testcases already cover.

#include "sandboxed_api/tools/clang_generator/codegen.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "sandboxed_api/tools/clang_generator/annotations.h"
#include "sandboxed_api/tools/clang_generator/generator.h"
#include "sandboxed_api/tools/clang_generator/ir.h"

namespace sapi {
namespace {

using ::absl_testing::IsOk;
using ::testing::AllOf;
using ::testing::ContainsRegex;
using ::testing::HasSubstr;
using ::testing::Not;

GeneratorOptions TestOptions() {
  GeneratorOptions options;
  options.name = "testlib";
  options.out_file = "testlib_sapi.cc";
  return options;
}

ir::TypeInfo VoidType() {
  return ir::TypeInfo{.kind = ir::TypeKind::kVoid, .canonical_name = "void"};
}

ir::TypeInfo ScalarType(absl::string_view name) {
  return ir::TypeInfo{.kind = ir::TypeKind::kScalar,
                      .canonical_name = std::string(name)};
}

ir::TypeInfo PointerTo(ir::TypeInfo pointee) {
  std::string name = absl::StrCat(pointee.canonical_name, "*");
  return ir::TypeInfo{
      .kind = ir::TypeKind::kPointer,
      .canonical_name = std::move(name),
      .pointee = std::make_shared<ir::TypeInfo>(std::move(pointee)),
  };
}

ir::Parameter ScalarArg(absl::string_view name, absl::string_view type) {
  return ir::Parameter{
      .name = std::string(name),
      .type = ScalarType(type),
      .payload = ir::ScalarParam{},
  };
}

// A buffer parameter of `pointee` elements, sized by `bounds`.
ir::Parameter BufferArg(absl::string_view name, ir::TypeInfo pointee,
                        PointerDir direction, ir::BufferBounds bounds) {
  return ir::Parameter{
      .name = std::string(name),
      .type = PointerTo(std::move(pointee)),
      .payload =
          ir::BufferParam{
              .direction = direction,
              .bounds = std::move(bounds),
          },
  };
}

ir::BufferBounds ElemCount(absl::string_view size_expr) {
  return ir::bounds::ElemCount{.size_expr = std::string(size_expr)};
}

ir::BufferBounds ByteCount(absl::string_view size_expr) {
  return ir::bounds::ByteCount{.size_expr = std::string(size_expr)};
}

ir::Library LibraryWith(ir::Function func) {
  return ir::Library{.name = "testlib", .functions = {std::move(func)}};
}

absl::StatusOr<std::string> HostSrc(ir::Function func) {
  return EmitHostSrc(TestOptions(), LibraryWith(std::move(func)),
                     /*includes=*/{});
}

// void process(char* buf, unsigned long len), with `buf` sized by `len`
// elements.
ir::Function BufferFunction(PointerDir direction) {
  return ir::Function{
      .name = "process",
      .parameters =
          {
              BufferArg("buf", ScalarType("char"), direction, ElemCount("len")),
              ScalarArg("len", "unsigned long"),
          },
  };
}

// ---------------------------------------------------------------------------
// EmitFuncDecl / EmitWrapperDecl
// ---------------------------------------------------------------------------

TEST(EmitFuncDeclTest, EmitsCPrototypeForVoidFunction) {
  std::string out;
  EmitFuncDecl(out, BufferFunction(PointerDir::kOut));
  EXPECT_THAT(out, HasSubstr("void process(char* buf, unsigned long len)"));
}

TEST(EmitFuncDeclTest, SpellsNonVoidReturnType) {
  ir::Function func{
      .name = "count",
      .return_value =
          ir::Parameter{
              .name = "sapi_ret_arg",
              .type = ScalarType("int"),
              .is_return_value = true,
              .payload = ir::ScalarParam{},
          },
  };

  std::string out;
  EmitFuncDecl(out, func);
  EXPECT_THAT(out, HasSubstr("int count("));
}

TEST(EmitWrapperDeclTest, PrefixesTheSandboxeeEntryPoint) {
  std::string out;
  EmitWrapperDecl(out, BufferFunction(PointerDir::kOut));
  EXPECT_THAT(out, HasSubstr("sapi_wrapper_process"));
}

// ---------------------------------------------------------------------------
// EmitHostSrc: buffer marshaling
// ---------------------------------------------------------------------------

TEST(EmitHostSrcTest, ScalarOnlyFunctionMarshalsNothing) {
  ir::Function func{
      .name = "set_level",
      .parameters = {ScalarArg("level", "int")},
  };

  absl::StatusOr<std::string> src = HostSrc(std::move(func));
  ASSERT_THAT(src, IsOk());
  EXPECT_THAT(*src, AllOf(HasSubstr("api.sapi_wrapper_set_level(level)"),
                          Not(HasSubstr("sapi::v::Array")),
                          Not(HasSubstr("nullptr :"))));
}

TEST(EmitHostSrcTest, ElementSizedBufferMultipliesWithOverflowCheck) {
  ir::TypeInfo record_type{
      .kind = ir::TypeKind::kStruct,
      .canonical_name = "MyPacket",
  };
  ir::Function func{
      .name = "send_packets",
      .parameters =
          {
              BufferArg("pkts", std::move(record_type), PointerDir::kIn,
                        ElemCount("num_pkts")),
              ScalarArg("num_pkts", "size_t"),
          },
  };

  absl::StatusOr<std::string> src = HostSrc(std::move(func));
  ASSERT_THAT(src, IsOk());
  // All buffer capacities are normalized to byte counts
  // (`sapi::v::Array<char>`), scaling element counts by the element size via
  // `CheckedMultiply`.
  EXPECT_THAT(
      *src,
      AllOf(HasSubstr("sapi::v::Array<char> sapi_tmp_pkts"),
            HasSubstr("sandbox->CheckedMultiply(sizeof(*pkts), (num_pkts))"),
            Not(HasSubstr("sapi::v::Array<MyPacket>"))));
}

TEST(EmitHostSrcTest, ByteSizedBufferUsesTheSizeExpressionVerbatim) {
  ir::TypeInfo record_type{
      .kind = ir::TypeKind::kStruct,
      .canonical_name = "MyPacket",
  };
  ir::Function func{
      .name = "send_packet",
      .parameters =
          {
              BufferArg("pkt", std::move(record_type), PointerDir::kOut,
                        ByteCount("pkt_bytes")),
              ScalarArg("pkt_bytes", "size_t"),
          },
  };

  absl::StatusOr<std::string> src = HostSrc(std::move(func));
  ASSERT_THAT(src, IsOk());
  // A byte count is already in bytes; scaling it again by sizeof(MyPacket)
  // would over-allocate.
  EXPECT_THAT(*src, AllOf(HasSubstr("sapi::v::Array<char> sapi_tmp_pkt"),
                          HasSubstr("(pkt)), pkt_bytes)"),
                          Not(HasSubstr("sapi::v::Array<MyPacket>")),
                          Not(HasSubstr("CheckedMultiply(sizeof(*pkt)"))));
}

TEST(EmitHostSrcTest, BufferTransferDirectionFollowsPointerDirection) {
  absl::StatusOr<std::string> in_src = HostSrc(BufferFunction(PointerDir::kIn));
  ASSERT_THAT(in_src, IsOk());
  EXPECT_THAT(*in_src, AllOf(HasSubstr("sapi_tmp_buf.PtrBefore()"),
                             Not(HasSubstr("sapi_tmp_buf.PtrAfter()"))));

  absl::StatusOr<std::string> out_src =
      HostSrc(BufferFunction(PointerDir::kOut));
  ASSERT_THAT(out_src, IsOk());
  EXPECT_THAT(*out_src, AllOf(HasSubstr("sapi_tmp_buf.PtrAfter()"),
                              Not(HasSubstr("sapi_tmp_buf.PtrBefore()"))));

  absl::StatusOr<std::string> inout_src =
      HostSrc(BufferFunction(PointerDir::kInOut));
  ASSERT_THAT(inout_src, IsOk());
  EXPECT_THAT(*inout_src, HasSubstr("sapi_tmp_buf.PtrBoth()"));
}

TEST(EmitHostSrcTest, BufferArgumentIsGuardedAgainstANullHostPointer) {
  absl::StatusOr<std::string> src = HostSrc(BufferFunction(PointerDir::kOut));
  ASSERT_THAT(src, IsOk());
  // A null host pointer has to reach the sandboxee as a null pointer rather
  // than as the address of an empty transfer buffer.
  EXPECT_THAT(*src, HasSubstr("buf == nullptr ? nullptr :"));
}

// ---------------------------------------------------------------------------
// EmitHostSrc: context bindings
// ---------------------------------------------------------------------------

TEST(EmitHostSrcTest, RetainAndBindHandsTheBufferToTheRegistry) {
  ir::Parameter data =
      BufferArg("data", ScalarType("char"), PointerDir::kIn, ByteCount("len"));
  data.As<ir::BufferParam>()->context_bound.retain_and_bind =
      RetainAndBind{.context = "ctx", .binding_name = "payload"};

  ir::Function func{
      .name = "ctx_set_payload",
      .parameters =
          {
              ir::Parameter{
                  .name = "ctx",
                  .type = PointerTo(VoidType()),
                  .payload =
                      ir::OpaquePointerParam{
                          .direction = PointerDir::kSandboxOpaque,
                      },
              },
              std::move(data),
              ScalarArg("len", "unsigned long"),
          },
  };

  absl::StatusOr<std::string> src = HostSrc(std::move(func));
  ASSERT_THAT(src, IsOk());
  EXPECT_THAT(*src,
              AllOf(HasSubstr("RetainPointer"), HasSubstr("\"payload\"")));
}

TEST(EmitHostSrcTest, RetainedNullTerminatedStringUsesStrlenPlusOne) {
  ir::Parameter str = BufferArg("label", ScalarType("const char"),
                                PointerDir::kIn, ir::bounds::NullTerminated{});
  str.As<ir::BufferParam>()->context_bound.retain_and_bind =
      RetainAndBind{.context = "ctx", .binding_name = "label"};

  ir::Function func{
      .name = "ctx_set_label",
      .parameters =
          {
              ir::Parameter{
                  .name = "ctx",
                  .type = PointerTo(VoidType()),
                  .payload =
                      ir::OpaquePointerParam{
                          .direction = PointerDir::kSandboxOpaque,
                      },
              },
              std::move(str),
          },
  };

  absl::StatusOr<std::string> src = HostSrc(std::move(func));
  ASSERT_THAT(src, IsOk());
  EXPECT_THAT(
      *src, AllOf(HasSubstr("strlen(reinterpret_cast<const char*>(label)) + 1"),
                  HasSubstr("RetainPointer")));
}

TEST(EmitHostSrcTest, SizedByBindingSubstitutesDollarReferencesWithGetSize) {
  ir::Function func{
      .name = "ctx_read_frames",
      .parameters =
          {
              ir::Parameter{
                  .name = "ctx",
                  .type = PointerTo(VoidType()),
                  .payload =
                      ir::OpaquePointerParam{
                          .direction = PointerDir::kSandboxOpaque,
                      },
              },
              BufferArg("out_buf", ScalarType("char"), PointerDir::kOut,
                        ir::bounds::SizedByBinding{
                            .context_expr = "ctx",
                            .binding_name = "$frame_size * $channels * 2",
                        }),
              BufferArg("bare_ident_buf", ScalarType("char"), PointerDir::kOut,
                        ir::bounds::SizedByBinding{
                            .context_expr = "ctx",
                            .binding_name = "header_size",
                        }),
              BufferArg("numeric_buf", ScalarType("char"), PointerDir::kOut,
                        ir::bounds::SizedByBinding{
                            .context_expr = "ctx",
                            .binding_name = "64",
                        }),
          },
  };

  absl::StatusOr<std::string> src = HostSrc(std::move(func));
  ASSERT_THAT(src, IsOk());
  EXPECT_THAT(
      *src,
      AllOf(
          ContainsRegex(
              R"(ContextBindingRegistry::Instance\(\)->GetSize\(ctx,\s*"frame_size"\)\s*\*\s*sapi::lwbox::ContextBindingRegistry::Instance\(\)->GetSize\(ctx,\s*"channels"\)\s*\*\s*2)"),
          ContainsRegex(
              R"(ContextBindingRegistry::Instance\(\)->GetSize\(ctx,\s*"header_size"\))"),
          Not(HasSubstr("\"64\""))));
}

TEST(EmitHostSrcTest, ClearBindingsReleasesTheContext) {
  ir::Function func{
      .name = "ctx_destroy",
      .parameters =
          {
              ir::Parameter{
                  .name = "ctx",
                  .type = PointerTo(VoidType()),
                  .payload =
                      ir::OpaquePointerParam{
                          .direction = PointerDir::kSandboxOpaque,
                      },
              },
          },
  };
  func.parameters[0]
      .As<ir::OpaquePointerParam>()
      ->context_bound.clear_bindings = true;

  absl::StatusOr<std::string> src = HostSrc(std::move(func));
  ASSERT_THAT(src, IsOk());
  EXPECT_THAT(*src, HasSubstr("ClearBindings"));
}

TEST(EmitHostSrcTest,
     StructMemberSizedByBindingPinsContextAndValidatesPostCallCapacity) {
  ir::Parameter slice_param{
      .name = "slice",
      .type = PointerTo(ir::TypeInfo{
          .kind = ir::TypeKind::kStruct,
          .canonical_name = "MatrixSlice",
      }),
      .payload =
          ir::StructSyncParam{
              .direction = PointerDir::kInOut,
              .members =
                  {
                      ir::StructMemberSync{
                          .member_name = "cells",
                          .parent_prefix = "slice->",
                          .direction = PointerDir::kInOut,
                          .bounds =
                              ir::bounds::SizedByBinding{
                                  .context_expr =
                                      "sapi::lwbox::CompositeKey(slice->ncid, "
                                      "slice->varid)",
                                  .binding_name = "slice->rows * $cols",
                                  .is_elem_sized = true,
                              },
                      },
                  },
          },
  };
  ir::Parameter table_param{
      .name = "table",
      .type = PointerTo(ir::TypeInfo{
          .kind = ir::TypeKind::kStruct,
          .canonical_name = "const MatrixSlice",
          .is_const = true,
      }),
      .payload =
          ir::StructSyncParam{
              .direction = PointerDir::kIn,
              .members =
                  {
                      ir::StructMemberSync{
                          .member_name = "cells",
                          .parent_prefix = "table->",
                          .direction = PointerDir::kIn,
                          .bounds =
                              ir::bounds::SizedByBinding{
                                  .context_expr = "sapi::lwbox::CompositeKey("
                                                  "table[sapi_i].ncid, "
                                                  "table[sapi_i].varid)",
                                  .binding_name = "$cols",
                                  .is_elem_sized = true,
                              },
                      },
                  },
              .array_bounds = ir::bounds::NullTerminated{},
          },
  };

  absl::StatusOr<std::string> src = HostSrc(ir::Function{
      .name = "sync_matrix_slices",
      .parameters = {std::move(slice_param), std::move(table_param)},
  });
  ASSERT_THAT(src, IsOk());
  EXPECT_THAT(
      *src,
      AllOf(
          ContainsRegex(
              R"(sapi_member_ctx_slice_cells\s*=\s*sapi::lwbox::CompositeKey\(\s*slice->ncid,\s*slice->varid\))"),
          ContainsRegex(
              R"(GetSize\(\s*(\*sandbox,\s*)?sapi_member_ctx_slice_cells,\s*"cols"\))"),
          // Neither the pre-call nor the post-call lookup reads the handle
          // fields of the written-back struct.
          Not(ContainsRegex(
              R"(GetSize\(\s*(\*sandbox,\s*)?sapi::lwbox::CompositeKey\(\s*slice->)")),
          HasSubstr("sapi_post_member_size_slice_cells > "
                    "sapi_member_size_slice_cells"),
          ContainsRegex(
              R"(CheckedMultiply\(\s*sizeof\(\*\(table\[sapi_i\]\.cells\)\))"),
          ContainsRegex(
              R"(GetSize\(\s*(\*sandbox,\s*)?sapi::lwbox::CompositeKey\(\s*table\[sapi_i\]\.ncid,\s*table\[sapi_i\]\.varid\),\s*"cols"\))")));
}

TEST(EmitHostSrcTest, ElemSizedByBindingReturnScalesBoundCountByPointeeSize) {
  ir::BufferParam copy_bound_ret{
      .direction = PointerDir::kOut,
      .bounds =
          ir::bounds::SizedByBinding{
              .context_expr = "ctx",
              .binding_name = "$count",
              .is_elem_sized = true,
          },
  };
  ir::BufferParam malloc_ret = copy_bound_ret;
  copy_bound_ret.context_bound.copy_from_and_bind =
      CopyFromAndBindOutPtr{.context = "ctx", .binding_name = "samples"};
  malloc_ret.lifetime = ir::lifetime::Malloc{};

  // Both host copies of the returned array hold `$count` elements, not bytes.
  for (const ir::BufferParam& ret : {copy_bound_ret, malloc_ret}) {
    absl::StatusOr<std::string> src = HostSrc(ir::Function{
        .name = "get_samples",
        .return_value =
            ir::Parameter{
                .name = "sapi_ret_arg",
                .type = PointerTo(ScalarType("int")),
                .is_return_value = true,
                .payload = ret,
            },
        .parameters =
            {
                ir::Parameter{
                    .name = "ctx",
                    .type = PointerTo(VoidType()),
                    .payload =
                        ir::OpaquePointerParam{
                            .direction = PointerDir::kSandboxOpaque,
                        },
                },
            },
    });
    ASSERT_THAT(src, IsOk());
    EXPECT_THAT(
        *src,
        ContainsRegex(
            R"(CheckedMultiply\(\s*sizeof\(\*sapi_returned_sb_ptr\),\s*\([^;]*GetSize\(\s*(\*sandbox,\s*)?ctx,\s*"count"\))"));
  }
}

TEST(EmitHostSrcTest, RuntimeHeaderIsIncludedInHostOnly) {
  constexpr absl::string_view kRuntimeInclude =
      R"(#include "sandboxed_api/lwbox/runtime/lwbox_runtime.h")";

  absl::StatusOr<std::string> host = HostSrc(BufferFunction(PointerDir::kOut));
  ASSERT_THAT(host, IsOk());
  EXPECT_THAT(*host, HasSubstr(kRuntimeInclude));

  absl::StatusOr<std::string> sandboxee_hdr = EmitSandboxeeHdr(
      TestOptions(), LibraryWith(BufferFunction(PointerDir::kOut)),
      /*includes=*/{});
  ASSERT_THAT(sandboxee_hdr, IsOk());
  EXPECT_THAT(*sandboxee_hdr, Not(HasSubstr("lwbox_runtime.h")));

  absl::StatusOr<std::string> sandboxee_src = EmitSandboxeeSrc(
      TestOptions(), LibraryWith(BufferFunction(PointerDir::kOut)),
      /*includes=*/{});
  ASSERT_THAT(sandboxee_src, IsOk());
  EXPECT_THAT(*sandboxee_src, Not(HasSubstr("lwbox_runtime.h")));
}

// ---------------------------------------------------------------------------
// Thunks and sandboxee-side emission
// ---------------------------------------------------------------------------

TEST(EmitHostSrcTest, HostThunkBodyReplacesGeneratedMarshaling) {
  ir::Function func = BufferFunction(PointerDir::kOut);
  func.host_thunk = ir::ThunkOverride{
      .function_name = "process",
      .body = "{ custom_host_thunk_body(); }",
      .declaration = "void process(char* buf, unsigned long len)",
  };

  absl::StatusOr<std::string> src = HostSrc(std::move(func));
  ASSERT_THAT(src, IsOk());
  EXPECT_THAT(*src, AllOf(HasSubstr("custom_host_thunk_body()"),
                          Not(HasSubstr("sapi_tmp_buf"))));
}

TEST(EmitSandboxeeTest, SandboxeeThunkReplacesWrapperInHeaderAndSource) {
  ir::Function regular = BufferFunction(PointerDir::kOut);
  ir::Function thunked{
      .name = "custom",
      .sandboxee_thunk =
          ir::ThunkOverride{
              .function_name = "custom",
              .body = "void sapi_wrapper_custom() { custom_sandboxee_body(); }",
              .declaration = "void sapi_wrapper_custom()",
          },
  };
  ir::Library library{
      .name = "testlib",
      .functions = {std::move(regular), std::move(thunked)},
  };

  absl::StatusOr<std::string> hdr =
      EmitSandboxeeHdr(TestOptions(), library, /*includes=*/{});
  ASSERT_THAT(hdr, IsOk());
  EXPECT_THAT(*hdr, AllOf(HasSubstr("sapi_wrapper_process"),
                          Not(HasSubstr("sapi_wrapper_custom"))));

  absl::StatusOr<std::string> src =
      EmitSandboxeeSrc(TestOptions(), library, /*includes=*/{});
  ASSERT_THAT(src, IsOk());
  EXPECT_THAT(*src, HasSubstr("custom_sandboxee_body()"));
}

TEST(EmitSandboxeeTest, SourceForwardsCallAndWritesReturnValue) {
  ir::Function func = BufferFunction(PointerDir::kOut);
  func.return_value = ir::Parameter{
      .name = "sapi_ret_arg",
      .type = ScalarType("int"),
      .is_return_value = true,
      .payload = ir::ScalarParam{},
  };

  absl::StatusOr<std::string> src =
      EmitSandboxeeSrc(TestOptions(), LibraryWith(std::move(func)),
                       /*includes=*/{});
  ASSERT_THAT(src, IsOk());
  // The wrapper is what the host calls over RPC; it has to declare and then
  // call the real library function and store the result in *sapi_ret_arg.
  EXPECT_THAT(*src,
              AllOf(HasSubstr("int process(char* buf, unsigned long len)"),
                    HasSubstr("sapi_wrapper_process("),
                    HasSubstr("auto sapi_ret_val = process(buf, len);"),
                    HasSubstr("*sapi_ret_arg = sapi_ret_val;")));
}

TEST(EmitSandboxeeTest, FunctorCallbackWrapsFunctionPointerWithoutCfiCheck) {
  ir::Parameter cb_arg{
      .name = "x",
      .type = ScalarType("int"),
      .payload = ir::ScalarParam{},
  };
  ir::Parameter cb_ret{
      .name = "return_val",
      .type = ScalarType("int"),
      .is_return_value = true,
      .payload = ir::ScalarParam{},
  };
  ir::Parameter cb_param{
      .name = "cb",
      .type = ir::TypeInfo{.kind = ir::TypeKind::kCallback,
                           .canonical_name = "std::function<int(int)>"},
      .payload =
          ir::CallbackParam{
              .callback_parameters = {std::move(cb_arg)},
              .return_value =
                  std::make_shared<ir::Parameter>(std::move(cb_ret)),
              .functor_template_name = "std::function",
          },
  };
  ir::Function func{
      .name = "run_with_functor",
      .parameters = {std::move(cb_param)},
  };

  absl::StatusOr<std::string> src =
      EmitSandboxeeSrc(TestOptions(), LibraryWith(std::move(func)),
                       /*includes=*/{});
  ASSERT_THAT(src, IsOk());
  EXPECT_THAT(
      *src,
      AllOf(HasSubstr("? std::function<int(int)>(nullptr)"),
            HasSubstr("[cb](int a0) __attribute__((no_sanitize(\n"
                      "                                 \"cfi\", \"kcfi\"))) "
                      "-> int { return cb(a0); }")));
}

TEST(EmitSandboxeeTest, MainReferencesEveryLibraryFunction) {
  ir::Library library{
      .name = "testlib",
      .functions = {BufferFunction(PointerDir::kOut),
                    ir::Function{.name = "reset"}},
  };

  absl::StatusOr<std::string> main = EmitSandboxeeMain(TestOptions(), library);
  ASSERT_THAT(main, IsOk());
  // main() exists to keep the wrapped symbols alive through linking, so every
  // function in the library has to be declared and called.
  EXPECT_THAT(*main, AllOf(HasSubstr("extern \"C\" void process();"),
                           HasSubstr("extern \"C\" void reset();"),
                           ContainsRegex(R"(int main\(\)[^}]*process\(\);)"),
                           ContainsRegex(R"(int main\(\)[^}]*reset\(\);)")));
}

// ---------------------------------------------------------------------------
// C++ strings and return values
// ---------------------------------------------------------------------------

TEST(EmitCppStringTest, ValueStringIsMarshaledAsInputDataAndSize) {
  ir::Function func{
      .name = "consume_str",
      .parameters =
          {
              ir::Parameter{
                  .name = "s",
                  .type = ir::TypeInfo{.kind = ir::TypeKind::kString,
                                       .canonical_name = "std::string"},
                  .payload =
                      ir::CppStringParam{
                          .form = ir::CppStringParam::Form::kValue,
                          .direction = PointerDir::kIn,
                      },
              },
          },
  };

  absl::StatusOr<std::string> host = HostSrc(func);
  ASSERT_THAT(host, IsOk());
  EXPECT_THAT(*host, AllOf(HasSubstr("sapi::v::Array<char> sapi_tmp_s"),
                           HasSubstr("sapi_tmp_s.PtrBefore(), s.size()"),
                           Not(HasSubstr("sapi::v::LenVal"))));

  absl::StatusOr<std::string> sandboxee =
      EmitSandboxeeSrc(TestOptions(), LibraryWith(std::move(func)),
                       /*includes=*/{});
  ASSERT_THAT(sandboxee, IsOk());
  EXPECT_THAT(*sandboxee,
              AllOf(HasSubstr("const char* s_data, size_t s_size"),
                    HasSubstr("consume_str(std::string(s_data, s_size))"),
                    Not(HasSubstr("LenValStruct"))));
}

TEST(EmitCppStringTest, ReturnedStringCopiesFullLengthThroughLenVal) {
  ir::Function func{
      .name = "produce_str",
      .return_value =
          ir::Parameter{
              .name = "sapi_ret_arg",
              .type = ir::TypeInfo{.kind = ir::TypeKind::kString,
                                   .canonical_name = "std::string"},
              .is_return_value = true,
              .payload =
                  ir::CppStringParam{
                      .form = ir::CppStringParam::Form::kValue,
                      .direction = PointerDir::kOut,
                  },
          },
  };

  absl::StatusOr<std::string> host = HostSrc(func);
  ASSERT_THAT(host, IsOk());
  // Must construct std::string from (GetData(), GetDataSize()) so embedded NUL
  // bytes are preserved rather than truncating at the first NUL via GetCString.
  EXPECT_THAT(*host,
              AllOf(HasSubstr("sapi::v::LenVal sapi_ret_tmp(nullptr, 0);"),
                    HasSubstr("sapi_ret_tmp.PtrAfter()"),
                    HasSubstr("sapi_ret_tmp.GetData()"),
                    HasSubstr("sapi_ret_tmp.GetDataSize()"),
                    Not(HasSubstr("GetCString"))));

  absl::StatusOr<std::string> sandboxee =
      EmitSandboxeeSrc(TestOptions(), LibraryWith(std::move(func)),
                       /*includes=*/{});
  ASSERT_THAT(sandboxee, IsOk());
  EXPECT_THAT(*sandboxee,
              AllOf(HasSubstr("std::string sapi_ret_val = produce_str();"),
                    HasSubstr("memcpy(sapi_ret_arg->data, sapi_ret_val.data(), "
                              "sapi_ret_arg->size);")));
}

// C APIs frequently take a caller-provided buffer or struct pointer and return
// that exact same pointer on success (for convenience/chaining) or nullptr on
// failure — e.g. libwebp's WebPDecodeRGBAInto(..., uint8_t* output_buffer,
// ...), libudfread's udfread_readdir(UDFDIR*, struct udfread_dirent* entry), or
// libjpeg's jpeg_std_error(struct jpeg_error_mgr* err).
//
// With SANDBOX_ALIAS_PTR(out_buf), the host does not allocate or copy a new
// return buffer; it only checks whether the sandboxee returned non-null and
// yields the caller's original host pointer (or nullptr).
TEST(EmitHostSrcTest, AliasedReturnPointerReturnsOriginalHostPointer) {
  ir::Function func{
      .name = "decode_into_buffer",
      .return_value =
          ir::Parameter{
              .name = "sapi_ret_arg",
              .type = PointerTo(ScalarType("uint8_t")),
              .is_return_value = true,
              .payload =
                  ir::BufferParam{
                      .direction = PointerDir::kOut,
                      .bounds = ir::bounds::Singleton{},
                      .lifetime =
                          ir::lifetime::AliasHostPtr{
                              .host_param_name = "out_buf",
                          },
                  },
          },
      .parameters =
          {
              BufferArg("out_buf", ScalarType("uint8_t"), PointerDir::kOut,
                        ByteCount("len")),
              ScalarArg("len", "size_t"),
          },
  };

  absl::StatusOr<std::string> src = HostSrc(std::move(func));
  ASSERT_THAT(src, IsOk());
  EXPECT_THAT(*src,
              HasSubstr("return sapi_ret_arg.GetValue() ? out_buf : nullptr;"));
}

// C libraries frequently return a `const char*` pointing to a static string
// literal in the library's read-only data segment — e.g. Cairo's
// cairo_status_to_string(cairo_status_t), zlib's zError(int), or libopus's
// opus_strerror(int).
//
// With SANDBOX_NULL_TERMINATED and SANDBOX_LIFETIME_GLOBAL, the caller never
// frees the returned pointer and expects process-lifetime validity, so the host
// fetches and caches a host-owned copy in GlobalStringRegistry keyed by the
// sandboxee address.
TEST(EmitHostSrcTest,
     SandboxGlobalReturnStringFetchesFromGlobalStringRegistry) {
  ir::Function func{
      .name = "error_message",
      .return_value =
          ir::Parameter{
              .name = "sapi_ret_arg",
              .type = PointerTo(ScalarType("const char")),
              .is_return_value = true,
              .payload =
                  ir::BufferParam{
                      .direction = PointerDir::kOut,
                      .bounds = ir::bounds::NullTerminated{},
                      .lifetime = ir::lifetime::SandboxGlobal{},
                  },
          },
  };

  absl::StatusOr<std::string> src = HostSrc(std::move(func));
  ASSERT_THAT(src, IsOk());
  EXPECT_THAT(
      *src,
      AllOf(HasSubstr(
                "sapi::lwbox::GlobalStringRegistry::Instance()->GetOrFetch("),
            HasSubstr("*sandbox, sapi_ret_arg.GetValue())")));
}

// ---------------------------------------------------------------------------
// Outparam-sized buffers and callbacks
// ---------------------------------------------------------------------------

// Compression and decoding C APIs often write into a caller-allocated output
// buffer of known capacity (`cap_bytes`) and report the actual number of
// elements produced via an output pointer parameter (`*out_count`) — e.g.
// LZO's lzo1c_99_compress(..., lzo_bytep dst, lzo_uintp dst_len, ...).
//
// With SANDBOX_OUT_PTR and SANDBOX_ELEM_SIZED_BY_OUTPARAM(*out_count,
// cap_bytes), the host allocates `cap_bytes` in the sandbox without copying
// uninitialized host memory in; after the call, because the untrusted sandboxee
// controls `*out_count`, the host checks for multiplication overflow, verifies
// that the byte size does not exceed `cap_bytes`, and copies only that many
// bytes back from the sandbox.
TEST(EmitHostSrcTest,
     ElemSizedByOutparamAllocatesCapacityAndChecksScaledFinalSize) {
  ir::Function func{
      .name = "read_items",
      .parameters =
          {
              BufferArg("items", ScalarType("int"), PointerDir::kOut,
                        ir::bounds::ElemSizedByOutparam{
                            .outparam_name = "out_count",
                            .capacity_expr = "cap_bytes"}),
              BufferArg("out_count", ScalarType("size_t"), PointerDir::kOut,
                        ir::bounds::Singleton{}),
              ScalarArg("cap_bytes", "size_t"),
          },
  };

  absl::StatusOr<std::string> src = HostSrc(std::move(func));
  ASSERT_THAT(src, IsOk());
  EXPECT_THAT(
      *src,
      AllOf(
          HasSubstr("(items)), cap_bytes)"),
          HasSubstr("sapi_tmp_items.PtrNone()"),
          Not(HasSubstr("CopyToSandbox")),
          ContainsRegex(
              R"(\(out_count == nullptr\s*\?\s*0\s*:\s*sandbox->CheckedMultiply\(\s*sizeof\(\*items\),\s*sapi::lwbox::CheckedToSizeT\(\*out_count\)\)\))"),
          HasSubstr("absl::OutOfRangeError"), HasSubstr("CopyFromSandbox")));
}

// Packet-transformation C APIs often mutate a buffer in place where the buffer
// has a larger capacity (`cap_bytes`), enters with `*inout_len` bytes of input
// payload, and exits with an updated `*inout_len` (for example after appending
// an authentication tag or trailer) — e.g. libsrtp's srtp_protect(srtp_t ctx,
// void* rtp_hdr, int* len_ptr).
//
// With SANDBOX_INOUT_PTR and SANDBOX_BYTE_SIZED_BY_OUTPARAM(*inout_len,
// cap_bytes), the host allocates the full `cap_bytes` in the sandbox, copies
// the initial `*inout_len` bytes in before the call (`CopyToSandbox`), and
// after the call validates the updated `*inout_len <= cap_bytes` before copying
// the transformed payload back (`CopyFromSandbox`).
TEST(EmitHostSrcTest, ByteSizedByOutparamInOutCopiesInitialAndFinalSize) {
  ir::Function func{
      .name = "protect_packet",
      .parameters =
          {
              BufferArg("pkt", ScalarType("uint8_t"), PointerDir::kInOut,
                        ir::bounds::ByteSizedByOutparam{
                            .outparam_name = "inout_len",
                            .capacity_expr = "cap_bytes"}),
              BufferArg("inout_len", ScalarType("size_t"), PointerDir::kInOut,
                        ir::bounds::Singleton{}),
              ScalarArg("cap_bytes", "size_t"),
          },
  };

  absl::StatusOr<std::string> src = HostSrc(std::move(func));
  ASSERT_THAT(src, IsOk());
  EXPECT_THAT(
      *src,
      AllOf(
          HasSubstr("(pkt)), cap_bytes)"), HasSubstr("CopyToSandbox"),
          ContainsRegex(
              R"(absl::MakeSpan\(reinterpret_cast<const char\*>\(pkt\),\s*\(inout_len == nullptr\s*\?\s*0\s*:\s*sapi::lwbox::CheckedToSizeT\(\*inout_len\)\)\))"),
          HasSubstr("sapi_tmp_inout_len.PtrBoth()"),
          ContainsRegex(
              R"(size_t sapi_final_size =\s*\(inout_len == nullptr\s*\?\s*0\s*:\s*sapi::lwbox::CheckedToSizeT\(\*inout_len\)\);)"),
          HasSubstr("absl::OutOfRangeError"), HasSubstr("CopyFromSandbox")));
}

// When a sandboxed library invokes a host callback with a pointer to a
// non-trivially-copyable struct (one containing pointer fields) annotated with
// SANDBOX_OUT_PTR and SANDBOX_SHALLOW_SYNC — e.g. a host I/O or metadata
// callback that populates only top-level fields of a state/options struct
// without touching nested pointers — data flows out of the host callback and
// into the sandboxee.
//
// Consequently, the generated callback handler skips copying the uninitialized
// struct from the sandboxee before calling the host callback
// (no TransferFromSandboxee) and only transfers the populated struct bytes back
// to the sandboxee afterward (TransferToSandboxee).
TEST(EmitHostSrcTest, CallbackOutShallowStructTransfersBackToSandboxee) {
  ir::TypeInfo record_type{
      .kind = ir::TypeKind::kStruct,
      .canonical_name = "MyState",
  };
  ir::Parameter cb_struct_param{
      .name = "out_state",
      .type = PointerTo(std::move(record_type)),
      .payload =
          ir::StructSyncParam{
              .direction = PointerDir::kOut,
              .is_shallow = true,
          },
  };
  ir::Parameter cb_param{
      .name = "cb",
      .type = ir::TypeInfo{.kind = ir::TypeKind::kCallback,
                           .canonical_name = "void (*)(MyState*)"},
      .payload =
          ir::CallbackParam{
              .callback_parameters = {std::move(cb_struct_param)},
          },
  };
  ir::Function func{
      .name = "run_with_cb",
      .parameters = {std::move(cb_param)},
  };

  absl::StatusOr<std::string> src = HostSrc(std::move(func));
  ASSERT_THAT(src, IsOk());
  EXPECT_THAT(
      *src, AllOf(HasSubstr("TransferToSandboxee(&sapi_arr_out_state.value())"),
                  Not(HasSubstr(
                      "TransferFromSandboxee(&sapi_arr_out_state.value())"))));
}

// Libraries that accept a custom malloc-style allocator callback — e.g. passing
// an output-buffer allocation callback into a decoder like libwebp — return a
// freshly allocated host buffer (`SANDBOX_OUT_PTR SANDBOX_UNINITIALIZED`) for
// the sandboxee to fill during the call.
//
// By default, the callback handler copies the initial contents of a host-owned
// return buffer into the sandbox shadow allocation (as if allocated by calloc).
// Marking the callback return value `SANDBOX_UNINITIALIZED` skips that initial
// host-to-sandbox transfer (`TransferToSandboxee`) while still copying the
// sandboxee's writes back to the host (`TransferFromSandboxee`) when the outer
// call completes.
TEST(EmitHostSrcTest, CallbackReturnValueUninitializedSkipsInitialTransfer) {
  ir::Parameter cb_ret{
      .name = "return_val",
      .type = PointerTo(ScalarType("int")),
      .is_return_value = true,
      .payload =
          ir::BufferParam{
              .direction = PointerDir::kOut,
              .bounds = ir::bounds::Singleton{},
              .uninitialized = true,
          },
  };
  ir::Parameter cb_param{
      .name = "alloc_cb",
      .type = ir::TypeInfo{.kind = ir::TypeKind::kCallback,
                           .canonical_name = "int* (*)(void)"},
      .payload =
          ir::CallbackParam{
              .return_value =
                  std::make_shared<ir::Parameter>(std::move(cb_ret)),
          },
  };
  ir::Function func{
      .name = "with_allocator",
      .parameters = {std::move(cb_param)},
  };

  absl::StatusOr<std::string> src = HostSrc(std::move(func));
  ASSERT_THAT(src, IsOk());
  EXPECT_THAT(*src,
              AllOf(HasSubstr("sapi_ret_var"),
                    Not(HasSubstr("TransferToSandboxee(sapi_ret_var.get())")),
                    HasSubstr("TransferFromSandboxee(var.get())")));
}

TEST(EmitHostSrcTest, StructSyncEmitsResolvedMemberSizeExprsAndConstants) {
  ir::TypeInfo record_type{
      .kind = ir::TypeKind::kStruct,
      .canonical_name = "SavedImage",
  };
  ir::Parameter struct_param{
      .name = "Image",
      .type = PointerTo(std::move(record_type)),
      .payload =
          ir::StructSyncParam{
              .direction = PointerDir::kInOut,
              .members =
                  {
                      ir::StructMemberSync{
                          .member_name = "RasterBits",
                          .parent_prefix = "Image->",
                          .direction = PointerDir::kInOut,
                          .bounds =
                              ir::bounds::ElemCount{
                                  .size_expr = "Image->ImageDesc.Width * "
                                               "Image->ImageDesc.Height",
                              },
                      },
                      ir::StructMemberSync{
                          .member_name = "Palette",
                          .parent_prefix = "Image->",
                          .direction = PointerDir::kIn,
                          .bounds =
                              ir::bounds::ByteCount{
                                  .size_expr = "256",
                              },
                      },
                  },
          },
  };
  ir::Function func{
      .name = "GifApplyTranslation",
      .parameters = {std::move(struct_param)},
  };

  absl::StatusOr<std::string> src = HostSrc(std::move(func));
  ASSERT_THAT(src, IsOk());
  EXPECT_THAT(
      *src,
      AllOf(HasSubstr(
                "sandbox->CheckedMultiply(\n"
                "        sizeof(*(Image->RasterBits)),\n"
                "        (Image->ImageDesc.Width * Image->ImageDesc.Height))"),
            HasSubstr("sapi_member_size_Image_Palette = 256;")));
}

TEST(EmitHostSrcTest,
     CallbackInvokerEmitsBidirectionalBindThunkAndRegisterCallback) {
  ir::Function invoker{
      .name = "sapi_invoke_adder",
      .return_value =
          ir::Parameter{
              .name = "sapi_ret_arg",
              .type = ScalarType("int"),
              .is_return_value = true,
              .payload = ir::ScalarParam{},
          },
      .parameters =
          {
              ScalarArg("sapi_target", "uintptr_t"),
              ScalarArg("a", "int"),
              ScalarArg("b", "int"),
          },
      .invoker_for_typedef = "adder",
  };

  absl::StatusOr<std::string> src = HostSrc(std::move(invoker));
  ASSERT_THAT(src, IsOk());
  EXPECT_THAT(
      *src,
      AllOf(HasSubstr("sapi_bind_thunk_adder(uintptr_t sapi_sandboxee_fn)"),
            HasSubstr("HostCallbackRegistry::Instance()->LookUpTrampoline(\n"
                      "              *sandbox, \"adder\", sapi_sandboxee_fn)"),
            HasSubstr("sapi_register_callback_adder"),
            HasSubstr("SandboxeeCallbackRegistry::Instance()->LookUpThunk(\n"
                      "              *sandbox, *sapi_thunk_pool_adder(), "
                      "sapi_host_fn)"),
            HasSubstr("HostCallbackRegistry::Instance()->TrampolineFor")));
}

TEST(EmitHostSrcTest,
     StructSyncParamWithCallbackFieldTranslatesInBothDirections) {
  ir::Parameter hooks_param{
      .name = "hooks",
      .type = PointerTo(ir::TypeInfo{.kind = ir::TypeKind::kStruct,
                                     .canonical_name = "CalculatorHooks"}),
      .payload =
          ir::StructSyncParam{
              .direction = PointerDir::kInOut,
              .callbacks =
                  {
                      ir::StructCallbackField{
                          .member_name = "combine",
                          .typedef_name = "adder",
                      },
                  },
          },
  };
  ir::Function func{
      .name = "install_struct_hooks",
      .parameters = {std::move(hooks_param)},
  };

  absl::StatusOr<std::string> src = HostSrc(std::move(func));
  ASSERT_THAT(src, IsOk());
  EXPECT_THAT(
      *src,
      AllOf(HasSubstr("sapi_register_callback_adder("),
            HasSubstr("reinterpret_cast<uintptr_t>(hooks->combine)"),
            HasSubstr("sapi_bind_thunk_adder("),
            HasSubstr(
                "reinterpret_cast<uintptr_t>(sapi_tmp_hooks.data().combine)")));
}

TEST(EmitHostSrcTest,
     CallbackTypedefInvokerWithNullTerminatedParamIncludesStringHeaders) {
  ir::Function invoker{
      .name = "sapi_invoke_log_msg",
      .parameters =
          {
              ScalarArg("sapi_target", "uintptr_t"),
              BufferArg("msg", ScalarType("const char"), PointerDir::kIn,
                        ir::bounds::NullTerminated{}),
          },
      .invoker_for_typedef = "log_msg",
  };

  absl::StatusOr<std::string> src = HostSrc(std::move(invoker));
  ASSERT_THAT(src, IsOk());
  EXPECT_THAT(*src, AllOf(HasSubstr("#include <cstring>"),
                          HasSubstr("#include <string>"),
                          HasSubstr("sandbox->GetCString(")));
}

TEST(EmitHostSrcTest,
     CallbackParamSizedByOutparamUsesCapacityAndActiveOutputBytes) {
  ir::Function invoker{
      .name = "sapi_invoke_fill_ints",
      .parameters =
          {
              ScalarArg("sapi_target", "uintptr_t"),
              BufferArg("buf", ScalarType("int"), PointerDir::kOut,
                        ir::bounds::ElemSizedByOutparam{
                            .outparam_name = "out_len",
                            .capacity_expr = "cap_bytes",
                        }),
              BufferArg("out_len", ScalarType("size_t"), PointerDir::kOut,
                        ir::bounds::Singleton{}),
              ScalarArg("cap_bytes", "size_t"),
          },
      .invoker_for_typedef = "fill_ints",
  };

  absl::StatusOr<std::string> src = HostSrc(std::move(invoker));
  ASSERT_THAT(src, IsOk());
  EXPECT_THAT(
      *src,
      AllOf(HasSubstr("sapi_arr_buf.emplace(cap_bytes);"),
            HasSubstr("*sapi_host_out_len"),
            HasSubstr("CHECK_LE(sapi_out_bytes_buf, sapi_arr_buf->GetSize());"),
            HasSubstr("CopyToSandbox")));
}

TEST(EmitHostSrcTest,
     CallbackIdempotentEmitsSingleShotByDefaultAndIdempotentWhenRequested) {
  ir::Parameter single_shot_cb{
      .name = "single_cb",
      .type = ir::TypeInfo{.kind = ir::TypeKind::kCallback,
                           .canonical_name = "int (*)(int)"},
      .payload = ir::CallbackParam{.idempotent = false},
  };
  ir::Parameter idempotent_cb{
      .name = "multi_cb",
      .type = ir::TypeInfo{.kind = ir::TypeKind::kCallback,
                           .canonical_name = "int (*)(int)"},
      .payload = ir::CallbackParam{.idempotent = true},
  };
  absl::StatusOr<std::string> src = HostSrc(ir::Function{
      .name = "run_both",
      .parameters = {std::move(single_shot_cb), std::move(idempotent_cb)},
  });
  ASSERT_THAT(src, IsOk());
  EXPECT_THAT(*src, AllOf(HasSubstr("sapi_cb_single_cb.emplace(single_cb, "
                                    "/*idempotent=*/false);"),
                          HasSubstr("sapi_cb_multi_cb.emplace(multi_cb);")));

  ir::Function invoker{
      .name = "sapi_invoke_multi_td",
      .parameters =
          {
              ScalarArg("sapi_target", "uintptr_t"),
              ScalarArg("x", "int"),
          },
      .invoker_for_typedef = "multi_td",
      .is_idempotent_typedef = true,
  };
  absl::StatusOr<std::string> invoker_src = HostSrc(std::move(invoker));
  ASSERT_THAT(invoker_src, IsOk());
  EXPECT_THAT(*invoker_src, HasSubstr("/*idempotent=*/true);"));
}

TEST(EmitHostSrcTest,
     MultiLevelPointer2DGridRecordsLeafOutputsInHostSyncVector) {
  auto row_pointee = std::make_shared<ir::Parameter>(
      BufferArg("int_row_inout_pointee", ScalarType("int"), PointerDir::kInOut,
                ElemCount("cols")));
  ir::Parameter grid_param{
      .name = "grid",
      .type = PointerTo(row_pointee->type),
      .payload =
          ir::BufferParam{
              .direction = PointerDir::kInOut,
              .bounds = ElemCount("rows"),
              .pointee = row_pointee,
          },
  };
  absl::StatusOr<std::string> src = HostSrc(ir::Function{
      .name = "scale_2d_grid",
      .parameters =
          {
              std::move(grid_param),
              ScalarArg("rows", "size_t"),
              ScalarArg("cols", "size_t"),
          },
  });
  ASSERT_THAT(src, IsOk());
  EXPECT_THAT(
      *src,
      AllOf(
          HasSubstr("std::vector<sapi::v::Var*> sapi_sync_out_grid;"),
          HasSubstr("sapi_sync_out_grid.push_back(sapi_arr.get());"),
          HasSubstr("grid == nullptr ? nullptr : sapi_tmp_grid.PtrBefore()"),
          HasSubstr("for (sapi::v::Var* sapi_out_var : sapi_sync_out_grid)")));
}

TEST(EmitHostSrcTest,
     ThreeLevelPointerChecksOverflowWhenAllocatingMiddlePointerTable) {
  auto row_pointee = std::make_shared<ir::Parameter>(
      BufferArg("int_row_inout_pointee", ScalarType("int"), PointerDir::kInOut,
                ElemCount("cols")));
  auto matrix_pointee = std::make_shared<ir::Parameter>(ir::Parameter{
      .name = "int_matrix_inout_pointee",
      .type = PointerTo(row_pointee->type),
      .payload =
          ir::BufferParam{
              .direction = PointerDir::kInOut,
              .bounds = ElemCount("rows"),
              .pointee = row_pointee,
          },
  });
  ir::Parameter matrix_ptr{
      .name = "matrix_ptr",
      .type = PointerTo(matrix_pointee->type),
      .payload =
          ir::BufferParam{
              .direction = PointerDir::kInOut,
              .bounds = ir::bounds::Singleton{},
              .pointee = matrix_pointee,
          },
  };
  absl::StatusOr<std::string> src = HostSrc(ir::Function{
      .name = "add_to_matrix",
      .parameters =
          {
              std::move(matrix_ptr),
              ScalarArg("rows", "size_t"),
              ScalarArg("cols", "size_t"),
          },
  });
  ASSERT_THAT(src, IsOk());
  EXPECT_THAT(*src, HasSubstr("sandbox->CheckedMultiply(sizeof(int*),\n"
                              "                                   "
                              "std::max<size_t>(sapi_cnt_d1, 1))"));
}

TEST(EmitHostSrcTest,
     MultiLevelPointerWithCopyFromAndBindPointeeEmitsPtrAfter) {
  ir::Parameter db{
      .name = "db",
      .type = PointerTo(ScalarType("struct DbHandle")),
      .payload =
          ir::OpaquePointerParam{.direction = PointerDir::kSandboxOpaque},
  };
  auto bound_pointee = std::make_shared<ir::Parameter>(ir::Parameter{
      .name = "bound_pair_td",
      .type = PointerTo(ScalarType("const int")),
      .payload =
          ir::BufferParam{
              .direction = PointerDir::kOut,
              .bounds = ElemCount("2"),
              .context_bound =
                  ContextBoundAnnotations{
                      .copy_from_and_bind =
                          CopyFromAndBindOutPtr{
                              .context = "db",
                              .binding_name = "get_slice_out_slice_b0",
                          },
                  },
          },
  });
  ir::Parameter out_slice{
      .name = "out_slice",
      .type = PointerTo(bound_pointee->type),
      .payload =
          ir::BufferParam{
              .direction = PointerDir::kOut,
              .bounds = ir::bounds::Singleton{},
              .pointee = bound_pointee,
          },
  };
  absl::StatusOr<std::string> src = HostSrc(ir::Function{
      .name = "get_slice",
      .parameters = {std::move(db), std::move(out_slice)},
  });
  ASSERT_THAT(src, IsOk());
  EXPECT_THAT(*src,
              AllOf(HasSubstr("sapi_tmp_out_slice.PtrAfter()"),
                    HasSubstr("*out_slice = sapi_tmp_out_slice.GetValue();")));
}

TEST(EmitHostSrcTest,
     MultiLevelPointerInOuterWithGlobalCStringPointeeDeclaresOwnedVars) {
  auto global_cstr_pointee = std::make_shared<ir::Parameter>(ir::Parameter{
      .name = "static_c_str_pointee",
      .type = PointerTo(ScalarType("const char")),
      .payload =
          ir::BufferParam{
              .direction = PointerDir::kInOut,
              .bounds = ir::bounds::NullTerminated{},
              .lifetime = ir::lifetime::SandboxGlobal{},
          },
  });
  ir::Parameter singleton_in{
      .name = "single_str_ptr",
      .type = PointerTo(global_cstr_pointee->type),
      .payload =
          ir::BufferParam{
              .direction = PointerDir::kIn,
              .bounds = ir::bounds::Singleton{},
              .pointee = global_cstr_pointee,
          },
  };
  ir::Parameter array_in{
      .name = "str_array",
      .type = PointerTo(global_cstr_pointee->type),
      .payload =
          ir::BufferParam{
              .direction = PointerDir::kIn,
              .bounds = ElemCount("count"),
              .pointee = global_cstr_pointee,
          },
  };
  absl::StatusOr<std::string> src = HostSrc(ir::Function{
      .name = "inspect_global_strings",
      .parameters =
          {
              std::move(singleton_in),
              std::move(array_in),
              ScalarArg("count", "size_t"),
          },
  });
  ASSERT_THAT(src, IsOk());
  EXPECT_THAT(
      *src,
      AllOf(
          HasSubstr("std::vector<std::unique_ptr<sapi::v::Var>> "
                    "sapi_owned_single_str_ptr;"),
          HasSubstr("sapi_owned_single_str_ptr.push_back(std::move(sapi_cstr)"
                    ");"),
          HasSubstr("std::vector<std::unique_ptr<sapi::v::Var>> "
                    "sapi_owned_str_array;"),
          HasSubstr("sapi_owned_str_array.push_back(std::move(sapi_cstr));")));
}

TEST(EmitHostSrcTest, MultiLevelCallbackBoundsUseCheckedToSizeT) {
  auto byte_row = std::make_shared<ir::Parameter>(
      BufferArg("byte_row", ScalarType("const char"), PointerDir::kIn,
                ByteCount("rlen")));
  auto elem_row = std::make_shared<ir::Parameter>(BufferArg(
      "elem_row", ScalarType("const int"), PointerDir::kIn, ElemCount("cols")));
  ir::Parameter cb_param{
      .name = "grid_cb",
      .type = ir::TypeInfo{.kind = ir::TypeKind::kCallback,
                           .canonical_name =
                               "void (*)(const char**, int, int, const int**, "
                               "int, int)"},
      .payload =
          ir::CallbackParam{
              .callback_parameters =
                  {
                      ir::Parameter{
                          .name = "byte_rows",
                          .type = PointerTo(byte_row->type),
                          .payload =
                              ir::BufferParam{
                                  .direction = PointerDir::kIn,
                                  .bounds = ByteCount("outer_bytes"),
                                  .pointee = byte_row,
                              },
                      },
                      ScalarArg("outer_bytes", "int"),
                      ScalarArg("rlen", "int"),
                      ir::Parameter{
                          .name = "elem_rows",
                          .type = PointerTo(elem_row->type),
                          .payload =
                              ir::BufferParam{
                                  .direction = PointerDir::kIn,
                                  .bounds = ElemCount("rows"),
                                  .pointee = elem_row,
                              },
                      },
                      ScalarArg("rows", "int"),
                      ScalarArg("cols", "int"),
                  },
          },
  };
  absl::StatusOr<std::string> src = HostSrc(ir::Function{
      .name = "invoke_grid_cb",
      .parameters = {std::move(cb_param)},
  });
  ASSERT_THAT(src, IsOk());
  EXPECT_THAT(*src,
              AllOf(HasSubstr("sapi::lwbox::CheckedToSizeT(outer_bytes) / "
                              "sizeof(const char*)"),
                    HasSubstr("size_t sapi_elem_bytes = "
                              "sapi::lwbox::CheckedToSizeT(rlen);"),
                    HasSubstr("sapi::lwbox::CheckedToSizeT(rows)"),
                    HasSubstr("sandbox->CheckedMultiply(\n"
                              "                sizeof(**elem_rows), "
                              "sapi::lwbox::CheckedToSizeT(cols))")));
}

TEST(EmitHostSrcTest, MallocLifetimeBoundsUseCheckedToSizeT) {
  ir::Parameter ret_bytes{
      .name = "sapi_ret_arg",
      .type = PointerTo(ScalarType("char")),
      .is_return_value = true,
      .payload =
          ir::BufferParam{
              .direction = PointerDir::kOut,
              .bounds = ByteCount("*out_bytes"),
              .lifetime = ir::lifetime::Malloc{},
          },
  };
  ir::Parameter out_bytes =
      BufferArg("out_bytes", ScalarType("int"), PointerDir::kOut,
                ir::bounds::Singleton{});
  absl::StatusOr<std::string> byte_src = HostSrc(ir::Function{
      .name = "alloc_bytes",
      .return_value = std::move(ret_bytes),
      .parameters = {std::move(out_bytes)},
  });
  ASSERT_THAT(byte_src, IsOk());
  EXPECT_THAT(
      *byte_src,
      ContainsRegex(
          R"(\(out_bytes == nullptr\s*\?\s*0\s*:\s*sapi::lwbox::CheckedToSizeT\(\*out_bytes\)\))"));

  ir::Parameter ret_elems{
      .name = "sapi_ret_arg",
      .type = PointerTo(ScalarType("char")),
      .is_return_value = true,
      .payload =
          ir::BufferParam{
              .direction = PointerDir::kOut,
              .bounds = ElemCount("*out_len"),
              .lifetime = ir::lifetime::Malloc{},
          },
  };
  ir::Parameter out_len = BufferArg("out_len", ScalarType("int"),
                                    PointerDir::kOut, ir::bounds::Singleton{});
  absl::StatusOr<std::string> elem_src = HostSrc(ir::Function{
      .name = "alloc_elems",
      .return_value = std::move(ret_elems),
      .parameters = {std::move(out_len)},
  });
  ASSERT_THAT(elem_src, IsOk());
  EXPECT_THAT(
      *elem_src,
      ContainsRegex(
          R"(\(out_len == nullptr\s*\?\s*0\s*:\s*sandbox->CheckedMultiply\(\s*sizeof\([^)]*\),\s*sapi::lwbox::CheckedToSizeT\(\*out_len\)\)\))"));
}

TEST(EmitHostSrcTest,
     NonCharNullTerminatedInputParamUsesNullTerminatedByteSize) {
  ir::Function func{
      .name = "xml_check_utf8",
      .parameters =
          {
              BufferArg("utf8", ScalarType("const unsigned char"),
                        PointerDir::kIn, ir::bounds::NullTerminated{}),
              BufferArg("utf16", ScalarType("const char16_t"), PointerDir::kIn,
                        ir::bounds::NullTerminated{}),
          },
  };

  absl::StatusOr<std::string> src = HostSrc(std::move(func));
  ASSERT_THAT(src, IsOk());
  EXPECT_THAT(
      *src,
      AllOf(
          HasSubstr("sapi::lwbox::NullTerminatedByteSize(utf8)"),
          HasSubstr("sapi::lwbox::NullTerminatedByteSize(utf16)"),
          HasSubstr("utf8 == nullptr ? nullptr : sapi_tmp_utf8.PtrBefore()"),
          HasSubstr("utf16 == nullptr ? nullptr : sapi_tmp_utf16.PtrBefore()"),
          Not(HasSubstr("ConstCStr"))));
}

TEST(EmitHostSrcTest,
     CallbackWithNonCharNullTerminatedParamUsesReadNullTerminatedArray) {
  ir::Parameter cb_param{
      .name = "sax_cb",
      .type =
          ir::TypeInfo{.kind = ir::TypeKind::kCallback,
                       .canonical_name =
                           "void (*)(const unsigned char*, const uint16_t*)"},
      .payload =
          ir::CallbackParam{
              .callback_parameters =
                  {
                      BufferArg("tag", ScalarType("const unsigned char"),
                                PointerDir::kIn, ir::bounds::NullTerminated{}),
                      BufferArg("text16", ScalarType("const uint16_t"),
                                PointerDir::kIn, ir::bounds::NullTerminated{}),
                  },
          },
  };

  absl::StatusOr<std::string> src = HostSrc(ir::Function{
      .name = "parse_sax",
      .parameters = {std::move(cb_param)},
  });
  ASSERT_THAT(src, IsOk());
  EXPECT_THAT(
      *src,
      AllOf(HasSubstr("#include <vector>"),
            HasSubstr("sapi::lwbox::ReadNullTerminatedArray(\n"
                      "            *sandbox, reinterpret_cast<uintptr_t>(tag), "
                      "sizeof(*tag))"),
            HasSubstr(
                "sapi::lwbox::ReadNullTerminatedArray(\n"
                "            *sandbox, reinterpret_cast<uintptr_t>(text16), "
                "sizeof(*text16))")));
}

TEST(EmitHostSrcTest,
     CopyFromAndBindNullTerminatedReturnUsesReadNullTerminatedArray) {
  ir::Parameter ret{
      .name = "sapi_ret_arg",
      .type = PointerTo(ScalarType("const enum XmlFilterOp")),
      .is_return_value = true,
      .payload =
          ir::BufferParam{
              .direction = PointerDir::kOut,
              .bounds = ir::bounds::NullTerminated{},
              .context_bound =
                  ContextBoundAnnotations{
                      .copy_from_and_bind =
                          CopyFromAndBindOutPtr{
                              .context = "ctx",
                              .binding_name = "sapi_ret_arg",
                          },
                  },
          },
  };
  ir::Parameter ctx_param{
      .name = "ctx",
      .type = PointerTo(VoidType()),
      .payload =
          ir::OpaquePointerParam{
              .direction = PointerDir::kSandboxOpaque,
          },
  };
  absl::StatusOr<std::string> src = HostSrc(ir::Function{
      .name = "get_ctx_ops",
      .return_value = std::move(ret),
      .parameters = {std::move(ctx_param)},
  });
  ASSERT_THAT(src, IsOk());
  EXPECT_THAT(
      *src,
      AllOf(HasSubstr("#include <vector>"),
            HasSubstr("sapi::lwbox::ReadNullTerminatedArray(*sandbox, "
                      "sapi_sb_ptr,"),
            HasSubstr("sizeof(*sapi_returned_sb_ptr))"),
            HasSubstr("sapi_remote_arr.size() - sizeof(*sapi_returned_sb_ptr)"),
            HasSubstr("calloc(sapi_remote_arr.size(), sizeof(char))"),
            Not(HasSubstr("GetCString"))));
}
TEST(EmitHostSrcTest,
     StructSyncNonCharNullTerminatedMemberUsesNullTerminatedByteSize) {
  ir::Parameter pkt_param{
      .name = "pkt",
      .type = PointerTo(ir::TypeInfo{.kind = ir::TypeKind::kStruct,
                                     .canonical_name = "struct Utf16Packet"}),
      .payload =
          ir::StructSyncParam{
              .direction = PointerDir::kInOut,
              .members =
                  {
                      ir::StructMemberSync{
                          .member_name = "utf16_in",
                          .parent_prefix = "pkt->",
                          .direction = PointerDir::kIn,
                          .bounds = ir::bounds::NullTerminated{},
                      },
                      ir::StructMemberSync{
                          .member_name = "utf16_inout",
                          .parent_prefix = "pkt->",
                          .direction = PointerDir::kInOut,
                          .bounds = ir::bounds::NullTerminated{},
                      },
                  },
          },
  };
  absl::StatusOr<std::string> src = HostSrc(ir::Function{
      .name = "process_utf16_packet",
      .parameters = {std::move(pkt_param)},
  });
  ASSERT_THAT(src, IsOk());
  EXPECT_THAT(
      *src,
      AllOf(HasSubstr("sapi_member_size_pkt_utf16_in =\n"
                      "        sapi::lwbox::NullTerminatedByteSize("
                      "pkt->utf16_in);"),
            HasSubstr("sapi_member_size_pkt_utf16_inout =\n"
                      "        sapi::lwbox::NullTerminatedByteSize("
                      "pkt->utf16_inout);"),
            HasSubstr("if (pkt->utf16_inout != nullptr &&\n"
                      "        sapi_member_size_pkt_utf16_inout >= "
                      "sizeof(*(pkt->utf16_inout)))"),
            HasSubstr("memset(reinterpret_cast<char*>(pkt->utf16_inout) +\n"
                      "                 sapi_member_size_pkt_utf16_inout - "
                      "sizeof(*(pkt->utf16_inout)),\n"
                      "             0, sizeof(*(pkt->utf16_inout)));"),
            Not(HasSubstr("strlen"))));
}

TEST(EmitHostSrcTest,
     NullTerminatedPointerArrayParamAndCallbackMarshalWithNullTerminatedCount) {
  auto cstr_pointee = std::make_shared<ir::Parameter>(
      BufferArg("c_str_in_pointee", ScalarType("const char"), PointerDir::kIn,
                ir::bounds::NullTerminated{}));
  ir::Parameter lst_param{
      .name = "lst",
      .type = PointerTo(cstr_pointee->type),
      .payload =
          ir::BufferParam{
              .direction = PointerDir::kIn,
              .bounds = ir::bounds::NullTerminated{},
              .pointee = cstr_pointee,
          },
  };

  auto xml_str_pointee = std::make_shared<ir::Parameter>(
      BufferArg("xml_str_in_pointee", ScalarType("const unsigned char"),
                PointerDir::kIn, ir::bounds::NullTerminated{}));
  ir::Parameter cb_param{
      .name = "sax_cb",
      .type = ir::TypeInfo{.kind = ir::TypeKind::kCallback,
                           .canonical_name = "void (*)(const unsigned char**)"},
      .payload =
          ir::CallbackParam{
              .callback_parameters =
                  {
                      ir::Parameter{
                          .name = "atts",
                          .type = PointerTo(xml_str_pointee->type),
                          .payload =
                              ir::BufferParam{
                                  .direction = PointerDir::kIn,
                                  .bounds = ir::bounds::NullTerminated{},
                                  .pointee = xml_str_pointee,
                              },
                      },
                  },
          },
  };

  absl::StatusOr<std::string> src = HostSrc(ir::Function{
      .name = "check_option_and_sax",
      .parameters = {std::move(lst_param), std::move(cb_param)},
  });
  ASSERT_THAT(src, IsOk());
  EXPECT_THAT(
      *src,
      AllOf(HasSubstr("size_t sapi_count_lst = "
                      "sapi::lwbox::NullTerminatedCount(lst);"),
            HasSubstr("std::vector<const char*> sapi_ptrs_lst(sapi_count_lst + "
                      "1, nullptr);"),
            HasSubstr(
                "sandbox->CheckedMultiply(sizeof(const char*), sapi_count_lst "
                "+ 1)"),
            HasSubstr("sapi::lwbox::ReadNullTerminatedArray(\n"
                      "                *sandbox, "
                      "reinterpret_cast<uintptr_t>(atts),\n"
                      "                sizeof(const unsigned char*))"),
            HasSubstr("sapi_host_ptrs_atts.assign(sapi_count_atts + 1, "
                      "nullptr);")));
}

TEST(EmitHostSrcTest,
     CallbackWithHostOpaquePointerArrayForwardsElementsDirectly) {
  auto opaque_pointee = std::make_shared<ir::Parameter>(ir::Parameter{
      .name = "host_ctx_pointee",
      .type = PointerTo(
          ir::TypeInfo{.kind = ir::TypeKind::kVoid, .canonical_name = "void"}),
      .payload = ir::OpaquePointerParam{.direction = PointerDir::kHostOpaque},
  });
  ir::Parameter cb_param{
      .name = "ctx_cb",
      .type = ir::TypeInfo{.kind = ir::TypeKind::kCallback,
                           .canonical_name = "void (*)(void**)"},
      .payload =
          ir::CallbackParam{
              .callback_parameters =
                  {
                      ir::Parameter{
                          .name = "ctxs",
                          .type = PointerTo(opaque_pointee->type),
                          .payload =
                              ir::BufferParam{
                                  .direction = PointerDir::kIn,
                                  .bounds = ir::bounds::NullTerminated{},
                                  .pointee = opaque_pointee,
                              },
                      },
                  },
          },
  };

  absl::StatusOr<std::string> src = HostSrc(ir::Function{
      .name = "dispatch_ctxs",
      .parameters = {std::move(cb_param)},
  });
  ASSERT_THAT(src, IsOk());
  EXPECT_THAT(*src, AllOf(HasSubstr("sapi_host_ptrs_ctxs[sapi_i] = "
                                    "sapi_remote_ptrs_ctxs[sapi_i];"),
                          Not(HasSubstr("HostOpaqueRegistry"))));
}

TEST(EmitHostSrcTest,
     NullTerminatedStructSyncArrayMarshalsStringsAndCallbacksWithZeroSentinel) {
  ir::Parameter reg_param{
      .name = "l",
      .type = PointerTo(ir::TypeInfo{.kind = ir::TypeKind::kStruct,
                                     .canonical_name = "const LuaRegEntry",
                                     .is_const = true}),
      .payload =
          ir::StructSyncParam{
              .direction = PointerDir::kIn,
              .members =
                  {
                      ir::StructMemberSync{
                          .member_name = "name",
                          .parent_prefix = "l->",
                          .direction = PointerDir::kIn,
                          .bounds = ir::bounds::NullTerminated{},
                      },
                  },
              .callbacks =
                  {
                      ir::StructCallbackField{
                          .member_name = "func",
                          .typedef_name = "lua_cfunc_cb",
                      },
                  },
              .array_bounds = ir::bounds::NullTerminated{},
          },
  };

  absl::StatusOr<std::string> src = HostSrc(ir::Function{
      .name = "luaL_setfuncs_and_call",
      .parameters = {std::move(reg_param)},
  });
  ASSERT_THAT(src, IsOk());
  EXPECT_THAT(
      *src,
      AllOf(HasSubstr("size_t sapi_count_l = "
                      "sapi::lwbox::NullTerminatedCount(l);"),
            HasSubstr("std::vector<LuaRegEntry> sapi_structs_l(sapi_count_l + "
                      "1);"),
            HasSubstr("sapi::lwbox::NullTerminatedByteSize(l[sapi_i].name)"),
            HasSubstr("sapi_register_callback_lua_cfunc_cb(\n"
                      "                reinterpret_cast<uintptr_t>(l[sapi_i]."
                      "func))")));
}

TEST(EmitHostSrcTest,
     CallbackWithNullTerminatedStructArrayUsesTypedReadNullTerminatedArray) {
  ir::TypeInfo record_type{
      .kind = ir::TypeKind::kStruct,
      .canonical_name = "const KeyValuePair",
      .is_const = true,
  };
  ir::Parameter cb_param{
      .name = "on_pairs",
      .type = ir::TypeInfo{.kind = ir::TypeKind::kCallback,
                           .canonical_name = "int (*)(const KeyValuePair*)"},
      .payload =
          ir::CallbackParam{
              .callback_parameters =
                  {
                      BufferArg("pairs", std::move(record_type),
                                PointerDir::kIn, ir::bounds::NullTerminated{}),
                  },
          },
  };
  absl::StatusOr<std::string> src = HostSrc(ir::Function{
      .name = "eval_key_value_pairs",
      .parameters = {std::move(cb_param)},
  });
  ASSERT_THAT(src, IsOk());
  EXPECT_THAT(
      *src,
      HasSubstr("sapi::lwbox::ReadNullTerminatedArray<KeyValuePair>(\n"
                "            *sandbox, reinterpret_cast<uintptr_t>(pairs))"));
}

TEST(EmitHostSrcTest,
     RetainedInOutBufferEvaluatesCapacityOnceAndClampsPostCallSync) {
  ir::Parameter data = BufferArg("data", ScalarType("int"), PointerDir::kInOut,
                                 ElemCount("*inout_len"));
  data.As<ir::BufferParam>()->context_bound.retain_and_bind =
      RetainAndBind{.context = "ctx", .binding_name = "payload"};

  ir::Function func{
      .name = "ctx_bind_inout_ints",
      .parameters =
          {
              ir::Parameter{
                  .name = "ctx",
                  .type = PointerTo(VoidType()),
                  .payload =
                      ir::OpaquePointerParam{
                          .direction = PointerDir::kSandboxOpaque,
                      },
              },
              std::move(data),
              BufferArg("inout_len", ScalarType("size_t"), PointerDir::kInOut,
                        ir::bounds::Singleton{}),
          },
  };

  absl::StatusOr<std::string> src = HostSrc(std::move(func));
  ASSERT_THAT(src, IsOk());
  EXPECT_THAT(
      *src,
      AllOf(
          ContainsRegex(
              R"(const size_t sapi_cap_data =\s*\(data != nullptr\)\s*\?\s*\(\(inout_len == nullptr\s*\?\s*0\s*:\s*sandbox->CheckedMultiply\(\s*sizeof\(\*data\),\s*sapi::lwbox::CheckedToSizeT\(\*inout_len\)\)\)\)\s*:\s*0;)"),
          HasSubstr("Allocate(sapi_cap_data, &sapi_sb_ptr)"),
          HasSubstr("if (data != nullptr && sapi_cap_data > 0)"),
          ContainsRegex(
              R"(const size_t sapi_post_size_data =\s*\(inout_len == nullptr\s*\?\s*0\s*:\s*sandbox->CheckedMultiply\(\s*sizeof\(\*data\),\s*sapi::lwbox::CheckedToSizeT\(\*inout_len\)\)\);)"),
          HasSubstr("CHECK_LE(sapi_post_size_data, sapi_cap_data);"),
          HasSubstr("if (sapi_post_size_data > 0)"),
          HasSubstr("RetainPointer(\n"
                    "        ctx, \"payload\", sapi_tmp_data.GetRemoteValue(), "
                    "sapi_cap_data)")));
}

TEST(EmitHostSrcTest,
     InputBufferSizedByDerefGuardsNullAndChecksSignedCountWithoutWriteback) {
  ir::Function func{
      .name = "read_in_buffers_with_deref_counts",
      .parameters =
          {
              BufferArg("elems", ScalarType("const int"), PointerDir::kIn,
                        ElemCount("*inout_count")),
              BufferArg("bytes", ScalarType("const char"), PointerDir::kIn,
                        ByteCount("*inout_bytes")),
              BufferArg("inout_count", ScalarType("int"), PointerDir::kInOut,
                        ir::bounds::Singleton{}),
              BufferArg("inout_bytes", ScalarType("int"), PointerDir::kInOut,
                        ir::bounds::Singleton{}),
          },
  };

  absl::StatusOr<std::string> src = HostSrc(std::move(func));
  ASSERT_THAT(src, IsOk());
  EXPECT_THAT(
      *src,
      AllOf(
          ContainsRegex(
              R"(\(inout_count == nullptr\s*\?\s*0\s*:\s*sandbox->CheckedMultiply\(\s*sizeof\(\*elems\),\s*sapi::lwbox::CheckedToSizeT\(\*inout_count\)\)\))"),
          ContainsRegex(
              R"(\(inout_bytes == nullptr\s*\?\s*0\s*:\s*sapi::lwbox::CheckedToSizeT\(\*inout_bytes\)\))"),
          HasSubstr("sapi_tmp_elems.PtrBefore()"),
          HasSubstr("sapi_tmp_bytes.PtrBefore()"),
          Not(HasSubstr("CopyFromSandbox"))));
}

TEST(EmitHostSrcTest,
     CallbackOutparamCountSiblingZerosOutPtrAndValidatesInOutPtrBounds) {
  ir::Function out_invoker{
      .name = "sapi_invoke_fill_signed_out",
      .parameters =
          {
              ScalarArg("sapi_target", "uintptr_t"),
              BufferArg("buf", ScalarType("int"), PointerDir::kOut,
                        ir::bounds::ElemSizedByOutparam{
                            .outparam_name = "out_count",
                            .capacity_expr = "cap_bytes",
                        }),
              BufferArg("out_count", ScalarType("int"), PointerDir::kOut,
                        ir::bounds::Singleton{}),
              ScalarArg("cap_bytes", "size_t"),
          },
      .invoker_for_typedef = "fill_signed_out",
  };

  absl::StatusOr<std::string> out_src = HostSrc(std::move(out_invoker));
  ASSERT_THAT(out_src, IsOk());
  EXPECT_THAT(
      *out_src,
      AllOf(
          Not(HasSubstr("TransferFromSandboxee(&sapi_arr_out_count.value())")),
          ContainsRegex(
              R"(memset\(sapi_arr_out_count->GetData\(\),\s*0,\s*sapi_arr_out_count->GetSize\(\)\);)"),
          ContainsRegex(
              R"(sapi::lwbox::CheckedToSizeT\(\s*\*sapi_host_out_count\))"),
          HasSubstr("TransferToSandboxee(&sapi_arr_out_count.value())")));

  ir::Function inout_invoker{
      .name = "sapi_invoke_xform_signed_inout",
      .parameters =
          {
              ScalarArg("sapi_target", "uintptr_t"),
              BufferArg("buf", ScalarType("char"), PointerDir::kInOut,
                        ir::bounds::ByteSizedByOutparam{
                            .outparam_name = "inout_len",
                            .capacity_expr = "cap_bytes",
                        }),
              BufferArg("inout_len", ScalarType("int"), PointerDir::kInOut,
                        ir::bounds::Singleton{}),
              ScalarArg("cap_bytes", "size_t"),
          },
      .invoker_for_typedef = "xform_signed_inout",
  };

  absl::StatusOr<std::string> inout_src = HostSrc(std::move(inout_invoker));
  ASSERT_THAT(inout_src, IsOk());
  EXPECT_THAT(
      *inout_src,
      AllOf(
          HasSubstr("TransferFromSandboxee(&sapi_arr_inout_len.value())"),
          ContainsRegex(
              R"(size_t sapi_in_bytes_buf =\s*sapi::lwbox::CheckedToSizeT\(\s*\*sapi_host_inout_len\);)"),
          HasSubstr("CHECK_LE(sapi_in_bytes_buf, sapi_arr_buf->GetSize());"),
          ContainsRegex(
              R"(sapi_host_inout_len == nullptr\s*\?\s*0\s*:\s*sapi::lwbox::CheckedToSizeT\(\s*\*sapi_host_inout_len\))"),
          HasSubstr("CHECK_LE(sapi_out_bytes_buf, sapi_arr_buf->GetSize());")));
}

TEST(EmitHostSrcTest,
     OutparamSizedBufferSyncsOutLengthPtrBothAndGuardsZeroByteCopyFromSandbox) {
  ir::Function func{
      .name = "read_items_or_fail",
      .parameters =
          {
              BufferArg("items", ScalarType("int"), PointerDir::kOut,
                        ir::bounds::ElemSizedByOutparam{
                            .outparam_name = "out_count",
                            .capacity_expr = "cap_bytes"}),
              BufferArg("out_count", ScalarType("size_t"), PointerDir::kOut,
                        ir::bounds::Singleton{}),
              BufferArg("unrelated_out", ScalarType("size_t"), PointerDir::kOut,
                        ir::bounds::Singleton{}),
              ScalarArg("cap_bytes", "size_t"),
          },
  };

  absl::StatusOr<std::string> src = HostSrc(std::move(func));
  ASSERT_THAT(src, IsOk());
  EXPECT_THAT(
      *src,
      AllOf(
          HasSubstr("sapi_tmp_out_count.PtrBoth()"),
          HasSubstr("sapi_tmp_unrelated_out.PtrAfter()"),
          ContainsRegex(
              R"(if \(sapi_final_size > 0\) \{\s*sandbox->Check\(\s*sandbox->rpc_channel\(\)\s*->CopyFromSandbox)")));
}

}  // namespace
}  // namespace sapi
