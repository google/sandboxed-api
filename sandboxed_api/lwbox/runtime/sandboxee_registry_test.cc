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

#include "sandboxed_api/lwbox/runtime/sandboxee_registry.h"

#include <cstdint>
#include <cstring>

#include "gtest/gtest.h"
#include "sandboxed_api/call.h"
#include "sandboxed_api/var_type.h"

namespace sapi::client {
void HandleCallMsg(const ::sapi::FuncCall& call, ::sapi::FuncRet* ret);
void HandleSymbolMsg(const char* symname, ::sapi::FuncRet* ret);
}  // namespace sapi::client

namespace sapi::lwbox {
namespace {

int g_dummy_symbol = 42;
int g_second_symbol = 99;

FuncRet HandleTestAdd(const SandboxeeCallArgs& call) {
  int a = call.GetArg<int>(0);
  int b = call.GetArg<int>(1);
  return FuncRet::FromValue(a + b);
}

FuncRet HandleTestMul(const SandboxeeCallArgs& call) {
  int a = call.GetArg<int>(0);
  int b = call.GetArg<int>(1);
  return FuncRet::FromValue(a * b);
}

[[maybe_unused]] const bool kRegisteredFirst = [] {
  RegisterSandboxeeFunctions({{"test_add", &HandleTestAdd}},
                             {{"g_dummy_symbol", &g_dummy_symbol}});
  return true;
}();

[[maybe_unused]] const bool kRegisteredSecond = [] {
  RegisterSandboxeeFunctions({{"test_mul", &HandleTestMul}},
                             {{"g_second_symbol", &g_second_symbol}});
  return true;
}();

TEST(SandboxeeRegistryTest, DispatchesAcrossMultipleRegistrations) {
  FuncCall add_call{};
  add_call.argc = 2;
  std::strncpy(add_call.func, "test_add", sizeof(add_call.func) - 1);
  add_call.arg_type[0] = v::Type::kInt;
  add_call.arg_size[0] = sizeof(int);
  add_call.args[0].arg_int = 10;
  add_call.arg_type[1] = v::Type::kInt;
  add_call.arg_size[1] = sizeof(int);
  add_call.args[1].arg_int = 32;

  FuncRet ret{};
  sapi::client::HandleCallMsg(add_call, &ret);
  EXPECT_TRUE(ret.success);
  EXPECT_EQ(ret.int_val, 42u);

  FuncCall mul_call = add_call;
  std::strncpy(mul_call.func, "test_mul", sizeof(mul_call.func) - 1);
  sapi::client::HandleCallMsg(mul_call, &ret);
  EXPECT_TRUE(ret.success);
  EXPECT_EQ(ret.int_val, 320u);
}

TEST(SandboxeeRegistryTest, ResolvesSymbolsAndUnknownNames) {
  FuncRet ret{};
  sapi::client::HandleSymbolMsg("g_dummy_symbol", &ret);
  EXPECT_TRUE(ret.success);
  EXPECT_EQ(ret.ret_type, v::Type::kPointer);
  EXPECT_EQ(ret.int_val, reinterpret_cast<uintptr_t>(&g_dummy_symbol));

  sapi::client::HandleSymbolMsg("g_second_symbol", &ret);
  EXPECT_TRUE(ret.success);
  EXPECT_EQ(ret.int_val, reinterpret_cast<uintptr_t>(&g_second_symbol));

#if defined(__x86_64__) || defined(__aarch64__) || defined(__riscv)
  // Registered by `:sandbox2_merged_sandboxee`.
  sapi::client::HandleSymbolMsg("sapi_callback_trampolines", &ret);
  EXPECT_TRUE(ret.success);
  EXPECT_EQ(ret.ret_type, v::Type::kPointer);
  EXPECT_NE(ret.int_val, 0u);
#endif

  sapi::client::HandleSymbolMsg("nonexistent_symbol", &ret);
  EXPECT_FALSE(ret.success);

  FuncCall unknown_call{};
  std::strncpy(unknown_call.func, "nonexistent_fn",
               sizeof(unknown_call.func) - 1);
  sapi::client::HandleCallMsg(unknown_call, &ret);
  EXPECT_FALSE(ret.success);
}

TEST(SandboxeeRegistryTest, RejectsDuplicateRegistration) {
  EXPECT_DEATH(RegisterSandboxeeFunctions({{"test_add", &HandleTestAdd}}, {}),
               "Duplicate sandboxee function registered: test_add");
  EXPECT_DEATH(
      RegisterSandboxeeFunctions({}, {{"g_dummy_symbol", &g_dummy_symbol}}),
      "Duplicate sandboxee symbol registered: g_dummy_symbol");
}

TEST(SandboxeeCallArgsTest, ReadsArguments) {
  FuncCall call{};
  call.argc = 3;
  call.arg_type[0] = v::Type::kInt;
  call.args[0].arg_int = 7;
  // Like `SandboxBase::Call()`, store the raw bytes of the `double`.
  const double d = 2.5;
  call.arg_type[1] = v::Type::kFloat;
  std::memcpy(&call.args[1].arg_float, &d, sizeof(d));
  call.arg_type[2] = v::Type::kPointer;
  call.aux_type[2] = v::Type::kInt;
  call.args[2].arg_int = reinterpret_cast<uintptr_t>(&g_dummy_symbol);

  const SandboxeeCallArgs args(call);
  EXPECT_EQ(args.arg_count(), 3u);
  EXPECT_TRUE(args.HasCompatibleArg<int>(0));
  EXPECT_EQ(args.GetArg<int>(0), 7);
  EXPECT_TRUE(args.HasCompatibleArg<double>(1));
  EXPECT_FALSE(args.HasCompatibleArg<int>(1));
  EXPECT_DOUBLE_EQ(args.GetArg<double>(1), 2.5);
  EXPECT_TRUE(args.HasCompatibleArg<int*>(2));
  EXPECT_EQ(args.GetArg<int*>(2), &g_dummy_symbol);
}

TEST(SandboxeeCallArgsTest, DecodesBoolArguments) {
  FuncCall call{};
  call.argc = 3;
  call.arg_type[0] = v::Type::kInt;
  call.args[0].arg_int = 0;
  call.arg_type[1] = v::Type::kInt;
  call.args[1].arg_int = 1;
  call.arg_type[2] = v::Type::kInt;
  call.args[2].arg_int = 2;

  const SandboxeeCallArgs args(call);
  EXPECT_TRUE(args.HasCompatibleArg<bool>(0));
  EXPECT_FALSE(args.GetArg<bool>(0));
  EXPECT_TRUE(args.GetArg<bool>(1));
  EXPECT_TRUE(args.GetArg<bool>(2));
}

TEST(SandboxeeCallArgsTest, RejectsProtobufArguments) {
  FuncCall call{};
  call.argc = 1;
  call.arg_type[0] = v::Type::kPointer;
  call.aux_type[0] = v::Type::kProto;
  EXPECT_FALSE(SandboxeeCallArgs(call).HasCompatibleArg<void*>(0));
}

TEST(SandboxeeCallArgsDeathTest, ChecksArgumentCountAndIndex) {
  FuncCall call{};
  call.argc = FuncCall::kArgsMax + 1;
  EXPECT_DEATH(SandboxeeCallArgs{call}, "exceeds limits");

  call.argc = 1;
  call.arg_type[0] = v::Type::kInt;
  const SandboxeeCallArgs args(call);
  EXPECT_DEATH(args.GetArg<int>(1), "idx < call_.argc");
  EXPECT_DEATH(args.HasCompatibleArg<int>(1), "idx < call_.argc");
}

}  // namespace
}  // namespace sapi::lwbox
