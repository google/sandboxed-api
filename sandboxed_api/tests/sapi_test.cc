// Copyright 2019 Google LLC
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

#include <fcntl.h>
#include <limits.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "benchmark/benchmark.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "sandboxed_api/examples/stringop/stringop-sapi.sapi.h"
#include "sandboxed_api/examples/stringop/stringop_params.pb.h"
#include "sandboxed_api/examples/sum/sum-sapi.sapi.h"
#include "sandboxed_api/sandbox.h"
#include "sandboxed_api/sandbox2/result.h"
#include "sandboxed_api/sandbox_config.h"
#include "sandboxed_api/testing.h"
#include "sandboxed_api/tests/sapi_test-sapi.sapi.h"
#include "sandboxed_api/transaction.h"
#include "sandboxed_api/util/fileops.h"
#include "sandboxed_api/util/status_macros.h"
#include "sandboxed_api/util/thread.h"
#include "sandboxed_api/var_array.h"
#include "sandboxed_api/var_int.h"
#include "sandboxed_api/var_lenval.h"
#include "sandboxed_api/var_ptr.h"
#include "sandboxed_api/var_reg.h"
#include "sandboxed_api/var_struct.h"

namespace sapi {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::testing::ContainerEq;
using ::testing::Eq;
using ::testing::Gt;
using ::testing::HasSubstr;
using ::testing::NotNull;

// Functions that will be used during the benchmarks:

// Function causing no load in the sandboxee.
absl::Status InvokeNop(Sandbox* sandbox) {
  StringopApi api(sandbox);
  return api.nop();
}

// Function that makes use of our special protobuf (de)-serialization code
// inside SAPI (including the back-synchronization of the structure).
absl::Status InvokeStringReversal(Sandbox* sandbox) {
  StringopApi api(sandbox);
  stringop::StringReverse proto;
  proto.set_input("Hello");
  absl::StatusOr<v::Proto<stringop::StringReverse>> pp(
      v::Proto<stringop::StringReverse>::FromMessage(proto));
  SAPI_RETURN_IF_ERROR(pp.status());
  SAPI_ASSIGN_OR_RETURN(int return_code, api.pb_reverse_string(pp->PtrBoth()));
  TRANSACTION_FAIL_IF_NOT(return_code != 0, "pb_reverse_string failed");
  SAPI_ASSIGN_OR_RETURN(auto pb_result, pp->GetMessage());
  TRANSACTION_FAIL_IF_NOT(pb_result.output() == "olleH", "Incorrect output");
  return absl::OkStatus();
}

// Benchmark functions:

// Restart SAPI sandbox by letting the sandbox object go out of scope.
// Minimal case for measuring the minimum overhead of restarting the sandbox.
void BenchmarkSandboxRestartOverhead(benchmark::State& state) {
  for (auto _ : state) {
    BasicTransaction st(std::make_unique<StringopSandbox>());
    // Invoke nop() to make sure that our sandbox is running.
    EXPECT_THAT(st.Run(InvokeNop), IsOk());
  }
}
BENCHMARK(BenchmarkSandboxRestartOverhead);

void BenchmarkSandboxRestartForkserverOverhead(benchmark::State& state) {
  sapi::BasicTransaction st(std::make_unique<StringopSandbox>());
  for (auto _ : state) {
    EXPECT_THAT(st.Run(InvokeNop), IsOk());
    EXPECT_THAT(st.sandbox()->Restart(true), IsOk());
  }
}
BENCHMARK(BenchmarkSandboxRestartForkserverOverhead);

void BenchmarkSandboxRestartForkserverOverheadForced(benchmark::State& state) {
  sapi::BasicTransaction st{std::make_unique<StringopSandbox>()};
  for (auto _ : state) {
    EXPECT_THAT(st.Run(InvokeNop), IsOk());
    EXPECT_THAT(st.sandbox()->Restart(false), IsOk());
  }
}
BENCHMARK(BenchmarkSandboxRestartForkserverOverheadForced);

// Reuse the sandbox. Used to measure the overhead of the call invocation.
void BenchmarkCallOverhead(benchmark::State& state) {
  BasicTransaction st(std::make_unique<StringopSandbox>());
  for (auto _ : state) {
    EXPECT_THAT(st.Run(InvokeNop), IsOk());
  }
}
BENCHMARK(BenchmarkCallOverhead);

// Make use of protobufs.
void BenchmarkProtobufHandling(benchmark::State& state) {
  BasicTransaction st(std::make_unique<StringopSandbox>());
  for (auto _ : state) {
    EXPECT_THAT(st.Run(InvokeStringReversal), IsOk());
  }
}
BENCHMARK(BenchmarkProtobufHandling);

// Measure overhead of synchronizing data.
void BenchmarkIntDataSynchronization(benchmark::State& state) {
  auto sandbox = StringopSandbox();
  ASSERT_THAT(sandbox.Init(), IsOk());

  long current_val = 0;  // NOLINT
  v::Long long_var;
  // Allocate remote memory.
  ASSERT_THAT(sandbox.Allocate(&long_var, false), IsOk());

  for (auto _ : state) {
    // Write current_val to the process.
    long_var.SetValue(current_val);
    EXPECT_THAT(sandbox.TransferToSandboxee(&long_var), IsOk());
    // Invalidate value to make sure that the next call
    // is not simply a noop.
    long_var.SetValue(-1);
    // Read value back.
    EXPECT_THAT(sandbox.TransferFromSandboxee(&long_var), IsOk());
    EXPECT_THAT(long_var.GetValue(), Eq(current_val));

    ++current_val;
  }
}
BENCHMARK(BenchmarkIntDataSynchronization);

// Measure overhead of synchronizing `sapi::v::Ptr`s. The numbers are on
// par with BenchmarkCallOverhead when invoked with "0" (no pointer
// synchronization).
void BenchmarkVariableSynchronizationOverhead(benchmark::State& state) {
  auto sandbox = SapiTestSandbox();
  ASSERT_THAT(sandbox.Init(), IsOk());
  SapiTestApi api(&sandbox);
  const int expected_sum = (state.range(0) - 1) * state.range(0) / 2;
  for (auto _ : state) {
    // We are allocating new sapi::v::Int for each iteration on purpose, since
    // code analysis indicates that this is a common pattern in some of our
    // clients' code (i.e. having wrapper functions allocating sapi::v::* on
    // stack and pass their pointers to sapi functions).
    std::vector<sapi::v::Ptr*> ptrs(8, nullptr);
    std::vector<sapi::v::Int> vars(state.range(0));
    for (int i = 0; i < state.range(0); ++i) {
      vars[i].SetValue(i);
      ptrs[i] = vars[i].PtrBefore();
    }
    SAPI_ASSERT_OK_AND_ASSIGN(
        int res, api.accumulate(ptrs[0], ptrs[1], ptrs[2], ptrs[3], ptrs[4],
                                ptrs[5], ptrs[6], ptrs[7]));
    EXPECT_EQ(res, expected_sum);
  }
}
BENCHMARK(BenchmarkVariableSynchronizationOverhead)->Range(0, 8);

// Test whether stack trace generation works.
TEST(SapiTest, HasStackTraces) {
  SKIP_SANITIZERS_AND_COVERAGE;

  auto sandbox = std::make_unique<StringopSandbox>();
  ASSERT_THAT(sandbox->Init(), IsOk());
  StringopApi api(sandbox.get());
  EXPECT_THAT(api.violate(), StatusIs(absl::StatusCode::kUnavailable));
  const auto& result = sandbox->AwaitResult();
  EXPECT_THAT(
      result.GetStackTrace(),
      // Check that at least one expected function is present in the stack
      // trace.
      // Note: Typically, in optimized builds, on x86-64, only
      // "ViolateIndirect()" will be present in the stack trace. On POWER, all
      // stack frames are generated, but libunwind will be unable to track
      // "ViolateIndirect()" on the stack and instead show its IP as zero.
      AnyOf(HasSubstr("ViolateIndirect"), HasSubstr("violate")));
  EXPECT_THAT(result.final_status(), Eq(sandbox2::Result::VIOLATION));
}

// Various tests:

// Leaks a file descriptor inside the sandboxee.
int LeakFileDescriptor(sapi::Sandbox* sandbox, const char* path) {
  int raw_fd = open(path, O_RDONLY);
  sapi::v::Fd fd(raw_fd);  // Takes ownership of the raw fd.
  EXPECT_THAT(sandbox->TransferToSandboxee(&fd), IsOk());
  // We want to leak the remote FD. The local FD will still be closed.
  fd.OwnRemoteFd(false);
  return fd.GetRemoteFd();
}

class SandboxTest : public ::testing::TestWithParam<bool> {
 protected:
  SandboxConfig GetDefaultConfig() {
    SandboxConfig config;
    config.sandbox2.enable_shared_memory = GetParam();
    return config;
  }
};

// Make sure that restarting the sandboxee works (= fresh set of FDs).
TEST_P(SandboxTest, RestartSandboxFD) {
  sapi::BasicTransaction st{std::make_unique<SumSandbox>(GetDefaultConfig())};

  auto test_body = [](sapi::Sandbox* sandbox) -> absl::Status {
    // Open some FDs and check their value.
    int first_remote_fd = LeakFileDescriptor(sandbox, "/proc/self/exe");
    EXPECT_THAT(LeakFileDescriptor(sandbox, "/proc/self/exe"),
                Eq(first_remote_fd + 1));
    SAPI_RETURN_IF_ERROR(sandbox->Restart(false));
    // We should have a fresh sandbox now = FDs open previously should be
    // closed now.
    EXPECT_THAT(LeakFileDescriptor(sandbox, "/proc/self/exe"),
                Eq(first_remote_fd));
    return absl::OkStatus();
  };

  EXPECT_THAT(st.Run(test_body), IsOk());
}

TEST_P(SandboxTest, RestartTransactionSandboxFD) {
  sapi::BasicTransaction st{std::make_unique<SumSandbox>(GetDefaultConfig())};

  int fd_no = -1;
  ASSERT_THAT(st.Run([&fd_no](sapi::Sandbox* sandbox) -> absl::Status {
    fd_no = LeakFileDescriptor(sandbox, "/proc/self/exe");
    return absl::OkStatus();
  }),
              IsOk());

  EXPECT_THAT(st.Run([fd_no](sapi::Sandbox* sandbox) -> absl::Status {
    EXPECT_THAT(LeakFileDescriptor(sandbox, "/proc/self/exe"), Gt(fd_no));
    return absl::OkStatus();
  }),
              IsOk());

  EXPECT_THAT(st.Restart(), IsOk());

  EXPECT_THAT(st.Run([fd_no](sapi::Sandbox* sandbox) -> absl::Status {
    EXPECT_THAT(LeakFileDescriptor(sandbox, "/proc/self/exe"), Eq(fd_no));
    return absl::OkStatus();
  }),
              IsOk());
}

// Make sure we can recover from a dying sandbox.
TEST_P(SandboxTest, RestartSandboxAfterCrash) {
  SumSandbox sandbox(GetDefaultConfig());
  ASSERT_THAT(sandbox.Init(), IsOk());
  SumApi api(&sandbox);

  // Crash the sandbox.
  EXPECT_THAT(api.crash(), StatusIs(absl::StatusCode::kUnavailable));
  EXPECT_THAT(api.sum(1, 2).status(), StatusIs(absl::StatusCode::kUnavailable));
  EXPECT_THAT(sandbox.AwaitResult().final_status(),
              Eq(sandbox2::Result::SIGNALED));

  // Restart the sandbox.
  ASSERT_THAT(sandbox.Restart(false), IsOk());

  // The sandbox should now be responsive again.
  SAPI_ASSERT_OK_AND_ASSIGN(int result, api.sum(1, 2));
  EXPECT_THAT(result, Eq(3));
}

TEST_P(SandboxTest, RestartSandboxAfterViolation) {
  SumSandbox sandbox(GetDefaultConfig());
  ASSERT_THAT(sandbox.Init(), IsOk());
  SumApi api(&sandbox);

  // Violate the sandbox policy.
  EXPECT_THAT(api.violate(), StatusIs(absl::StatusCode::kUnavailable));
  EXPECT_THAT(api.sum(1, 2).status(), StatusIs(absl::StatusCode::kUnavailable));
  EXPECT_THAT(sandbox.AwaitResult().final_status(),
              Eq(sandbox2::Result::VIOLATION));

  // Restart the sandbox.
  ASSERT_THAT(sandbox.Restart(false), IsOk());

  // The sandbox should now be responsive again.
  SAPI_ASSERT_OK_AND_ASSIGN(int result, api.sum(1, 2));
  EXPECT_THAT(result, Eq(3));
}

TEST_P(SandboxTest, NoRaceInAwaitResult) {
  StringopSandbox sandbox(GetDefaultConfig());
  ASSERT_THAT(sandbox.Init(), IsOk());
  StringopApi api(&sandbox);

  EXPECT_THAT(api.violate(), StatusIs(absl::StatusCode::kUnavailable));
  absl::SleepFor(absl::Milliseconds(200));  // Make sure we lose the race
  const auto& result = sandbox.AwaitResult();
  EXPECT_THAT(result.final_status(), Eq(sandbox2::Result::VIOLATION));
}

TEST_P(SandboxTest, NoRaceInConcurrentTerminate) {
  SumSandbox sandbox(GetDefaultConfig());
  ASSERT_THAT(sandbox.Init(), IsOk());
  SumApi api(&sandbox);
  sapi::Thread th([&sandbox] {
    // Sleep so that the call already starts
    absl::SleepFor(absl::Seconds(1));
    sandbox.Terminate(/*attempt_graceful_exit=*/false);
  });
  EXPECT_THAT(api.sleep_for_sec(10), StatusIs(absl::StatusCode::kUnavailable));
  th.Join();
  const auto& result = sandbox.AwaitResult();
  EXPECT_THAT(result.final_status(), Eq(sandbox2::Result::EXTERNAL_KILL));
}

TEST_P(SandboxTest, UseUnotifyMonitor) {
  SandboxConfig config = GetDefaultConfig();
  config.sandbox2.use_unotify_monitor = true;
  SumSandbox sandbox(std::move(config));
  ASSERT_THAT(sandbox.Init(), IsOk());
  SumApi api(&sandbox);

  // Violate the sandbox policy.
  EXPECT_THAT(api.violate(), StatusIs(absl::StatusCode::kUnavailable));
  EXPECT_THAT(api.sum(1, 2).status(), StatusIs(absl::StatusCode::kUnavailable));
  EXPECT_THAT(sandbox.AwaitResult().final_status(),
              Eq(sandbox2::Result::VIOLATION));

  // Restart the sandbox.
  ASSERT_THAT(sandbox.Restart(false), IsOk());

  // The sandbox should now be responsive again.
  SAPI_ASSERT_OK_AND_ASSIGN(int result, api.sum(1, 2));
  EXPECT_THAT(result, Eq(3));
}

TEST_P(SandboxTest, AllocateAndTransferTest) {
  std::string test_string("This is a test");
  std::vector<uint8_t> test_string_vector(test_string.begin(),
                                          test_string.end());

  absl::Span<uint8_t> buffer_input(
      reinterpret_cast<uint8_t*>(test_string_vector.data()),
      test_string_vector.size());
  std::vector<uint8_t> buffer_output(test_string_vector.size());

  SumSandbox sandbox(GetDefaultConfig());
  ASSERT_THAT(sandbox.Init(), IsOk());
  SumApi api(&sandbox);

  SAPI_ASSERT_OK_AND_ASSIGN(
      auto sapi_array, sandbox.AllocateAndTransferToSandboxee(buffer_input));
  ASSERT_THAT(sapi_array, NotNull());
  sapi::v::Array<const uint8_t> sapi_buffer_output(
      reinterpret_cast<const uint8_t*>(buffer_output.data()),
      buffer_output.size());
  sapi_buffer_output.SetRemote(sapi_array->GetRemote());
  ASSERT_THAT(sandbox.TransferFromSandboxee(&sapi_buffer_output), IsOk());
  EXPECT_THAT(test_string_vector, ContainerEq(buffer_output));
}

TEST_P(SandboxTest, AllocateAndTransferTestLarge) {
  const size_t kLargeSize = getpagesize() * (IOV_MAX + 1);
  const std::string test_string(kLargeSize, 'A');
  std::vector<uint8_t> test_string_vector(test_string.begin(),
                                          test_string.end());

  absl::Span<uint8_t> buffer_input(
      reinterpret_cast<uint8_t*>(test_string_vector.data()),
      test_string_vector.size());
  std::vector<uint8_t> buffer_output(test_string_vector.size());

  SumSandbox sandbox(GetDefaultConfig());
  ASSERT_THAT(sandbox.Init(), IsOk());
  SumApi api(&sandbox);

  SAPI_ASSERT_OK_AND_ASSIGN(
      auto sapi_array, sandbox.AllocateAndTransferToSandboxee(buffer_input));
  ASSERT_THAT(sapi_array, NotNull());
  sapi::v::Array<const uint8_t> sapi_buffer_output(
      reinterpret_cast<const uint8_t*>(buffer_output.data()),
      buffer_output.size());
  sapi_buffer_output.SetRemote(sapi_array->GetRemote());
  ASSERT_THAT(sandbox.TransferFromSandboxee(&sapi_buffer_output), IsOk());
  EXPECT_THAT(test_string_vector, ContainerEq(buffer_output));
}

TEST(VarsTest, MoveOperations) {
  {
    v::Array<const uint8_t> array_orig(128);  // Allocates locally
    const uint8_t* data_before = array_orig.GetData();

    v::Array<const uint8_t> array_new(std::move(array_orig));
    array_orig = std::move(array_new);  // Move back

    const uint8_t* data_after = array_orig.GetData();
    EXPECT_THAT(data_before, Eq(data_after));
  }
  {
    constexpr absl::string_view kData = "Physcially fit";
    v::LenVal len_val_orig(kData.data(), kData.size());
    const uint8_t* data_before = len_val_orig.GetData();

    v::LenVal len_val_new(std::move(len_val_orig));
    len_val_orig = std::move(len_val_new);  // Move back

    const uint8_t* data_after = len_val_orig.GetData();
    EXPECT_THAT(data_before, Eq(data_after));
  }
  {
    stringop::StringDuplication underlying_proto;
    SAPI_ASSERT_OK_AND_ASSIGN(
        auto proto_orig,
        v::Proto<stringop::StringDuplication>::FromMessage(underlying_proto));

    v::Proto<stringop::StringDuplication> proto_new(std::move(proto_orig));
    proto_orig = std::move(proto_new);  // Move back
  }
  {
    v::Reg<uint64_t> reg_orig(0x414141);
    uint64_t value_before = reg_orig.GetValue();

    v::Reg<uint64_t> reg_new(std::move(reg_orig));
    reg_orig = std::move(reg_new);  // Move back

    uint64_t value_after = reg_orig.GetValue();
    EXPECT_THAT(value_before, Eq(value_after));
  }
  {
    struct MyStruct {
      int member = 0x414141;
    };
    v::Struct<MyStruct> struct_orig;
    MyStruct* data_before = struct_orig.mutable_data();

    v::Struct<MyStruct> struct_new(std::move(struct_orig));
    struct_orig = std::move(struct_new);  // Move back

    MyStruct* data_after = struct_orig.mutable_data();
    EXPECT_THAT(data_before, Eq(data_after));
  }
}

TEST_P(SandboxTest, MapFd) {
  sapi::file_util::fileops::FDCloser dev_null(open("/dev/null", O_WRONLY));
  ASSERT_NE(dev_null.get(), -1);
  std::vector<std::pair<sapi::file_util::fileops::FDCloser, int>> fds;
  fds.push_back(std::make_pair(std::move(dev_null), STDERR_FILENO));

  SandboxConfig config = GetDefaultConfig();
  config.fd_mappings = std::move(fds);
  SumSandbox sandbox(std::move(config));
  ASSERT_THAT(sandbox.Init(), IsOk());
  SumApi api(&sandbox);

  SAPI_ASSERT_OK_AND_ASSIGN(int result, api.sum(1, 2));
  EXPECT_THAT(result, Eq(3));

  // Restart multiple times
  for (int i = 0; i < 5; ++i) {
    ASSERT_THAT(sandbox.Restart(true), IsOk());
    SAPI_ASSERT_OK_AND_ASSIGN(result, api.sum(1, 2));
    EXPECT_THAT(result, Eq(3));
  }
}

INSTANTIATE_TEST_SUITE_P(SAPI, SandboxTest, ::testing::Values(false, true),
                         [](const ::testing::TestParamInfo<bool>& info) {
                           return info.param ? "EnableSharedMemory"
                                             : "DisableSharedMemory";
                         });

}  // namespace
}  // namespace sapi
