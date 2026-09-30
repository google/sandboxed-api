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
                            .binding_name = "$frame_size * 2",
                        }),
          },
  };

  absl::StatusOr<std::string> src = HostSrc(std::move(func));
  ASSERT_THAT(src, IsOk());
  EXPECT_THAT(
      *src,
      ContainsRegex(
          R"(ContextBindingRegistry::Instance\(\)->GetSize\(ctx,\s*"frame_size"\)\s*\*\s*2)"));
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
      AllOf(HasSubstr("(items)), cap_bytes)"),
            HasSubstr("sapi_tmp_items.PtrNone()"),
            Not(HasSubstr("CopyToSandbox")),
            HasSubstr("sandbox->CheckedMultiply(sizeof(*items), (*out_count))"),
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
          HasSubstr(
              "absl::MakeSpan(reinterpret_cast<const char*>(pkt), *inout_len)"),
          HasSubstr("sapi_tmp_inout_len.PtrBoth()"),
          HasSubstr("size_t sapi_final_size = *inout_len;"),
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

}  // namespace
}  // namespace sapi
