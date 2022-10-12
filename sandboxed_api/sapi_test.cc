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

#include <memory>
#include <thread>  // NOLINT(build/c++11)

#include "benchmark/benchmark.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "sandboxed_api/examples/stringop/sandbox.h"
#include "sandboxed_api/examples/stringop/stringop-sapi.sapi.h"
#include "sandboxed_api/examples/stringop/stringop_params.pb.h"
#include "sandboxed_api/examples/sum/sandbox.h"
#include "sandboxed_api/examples/sum/sum-sapi.sapi.h"
#include "sandboxed_api/testing.h"
#include "sandboxed_api/transaction.h"
#include "sandboxed_api/util/status_matchers.h"

namespace sapi {
namespace {

using ::sapi::IsOk;
using ::sapi::StatusIs;
using ::testing::Eq;
using ::testing::HasSubstr;

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
  auto sandbox = std::make_unique<StringopSandbox>();
  ASSERT_THAT(sandbox->Init(), IsOk());

  long current_val = 0;  // NOLINT
  v::Long long_var;
  // Allocate remote memory.
  ASSERT_THAT(sandbox->Allocate(&long_var, false), IsOk());

  for (auto _ : state) {
    // Write current_val to the process.
    long_var.SetValue(current_val);
    EXPECT_THAT(sandbox->TransferToSandboxee(&long_var), IsOk());
    // Invalidate value to make sure that the next call
    // is not simply a noop.
    long_var.SetValue(-1);
    // Read value back.
    EXPECT_THAT(sandbox->TransferFromSandboxee(&long_var), IsOk());
    EXPECT_THAT(long_var.GetValue(), Eq(current_val));

    current_val++;
  }
}
BENCHMARK(BenchmarkIntDataSynchronization);

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

// Make sure that restarting the sandboxee works (= fresh set of FDs).
TEST(SandboxTest, RestartSandboxFD) {
  sapi::BasicTransaction st{std::make_unique<SumSandbox>()};

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

TEST(SandboxTest, RestartTransactionSandboxFD) {
  sapi::BasicTransaction st{std::make_unique<SumSandbox>()};

  EXPECT_THAT(st.Run([](sapi::Sandbox* sandbox) -> absl::Status {
    EXPECT_THAT(LeakFileDescriptor(sandbox, "/proc/self/exe"), Eq(3));
    return absl::OkStatus();
  }),
              IsOk());

  EXPECT_THAT(st.Run([](sapi::Sandbox* sandbox) -> absl::Status {
    EXPECT_THAT(LeakFileDescriptor(sandbox, "/proc/self/exe"), Eq(4));
    return absl::OkStatus();
  }),
              IsOk());

  EXPECT_THAT(st.Restart(), IsOk());

  EXPECT_THAT(st.Run([](sapi::Sandbox* sandbox) -> absl::Status {
    EXPECT_THAT(LeakFileDescriptor(sandbox, "/proc/self/exe"), Eq(3));
    return absl::OkStatus();
  }),
              IsOk());
}

// Make sure we can recover from a dying sandbox.
TEST(SandboxTest, RestartSandboxAfterCrash) {
  SumSandbox sandbox;
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

TEST(SandboxTest, RestartSandboxAfterViolation) {
  SumSandbox sandbox;
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

TEST(SandboxTest, NoRaceInAwaitResult) {
  StringopSandbox sandbox;
  ASSERT_THAT(sandbox.Init(), IsOk());
  StringopApi api(&sandbox);

  EXPECT_THAT(api.violate(), StatusIs(absl::StatusCode::kUnavailable));
  absl::SleepFor(absl::Milliseconds(200));  // Make sure we lose the race
  const auto& result = sandbox.AwaitResult();
  EXPECT_THAT(result.final_status(), Eq(sandbox2::Result::VIOLATION));
}

TEST(SandboxTest, NoRaceInConcurrentTerminate) {
  SumSandbox sandbox;
  ASSERT_THAT(sandbox.Init(), IsOk());
  SumApi api(&sandbox);
  std::thread th([&sandbox] {
    // Sleep so that the call already starts
    absl::SleepFor(absl::Seconds(1));
    sandbox.Terminate(/*attempt_graceful_exit=*/false);
  });
  EXPECT_THAT(api.sleep_for_sec(10), StatusIs(absl::StatusCode::kUnavailable));
  th.join();
  const auto& result = sandbox.AwaitResult();
  EXPECT_THAT(result.final_status(), Eq(sandbox2::Result::EXTERNAL_KILL));
}

}  // namespace
}  // namespace sapi
