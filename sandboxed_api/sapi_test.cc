// Copyright 2019 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <fcntl.h>

#include "benchmark/benchmark.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/memory/memory.h"
#include "sandboxed_api/examples/stringop/lib/sandbox.h"
#include "sandboxed_api/examples/stringop/lib/stringop-sapi.sapi.h"
#include "sandboxed_api/examples/stringop/lib/stringop_params.pb.h"
#include "sandboxed_api/examples/sum/lib/sandbox.h"
#include "sandboxed_api/examples/sum/lib/sum-sapi.sapi.h"
#include "sandboxed_api/examples/sum/lib/sum-sapi_embed.h"
#include "sandboxed_api/transaction.h"
#include "sandboxed_api/util/status_matchers.h"
#include "sandboxed_api/util/status.h"

using ::sapi::IsOk;
using ::sapi::StatusIs;
using ::testing::Eq;
using ::testing::HasSubstr;

namespace sapi {
namespace {

// Functions that will be used during the benchmarks:

// Function causing no load in the sandboxee.
sapi::Status InvokeNop(Sandbox* sandbox) {
  StringopApi api(sandbox);
  return api.nop();
}

// Function that makes use of our special protobuf (de)-serialization code
// inside SAPI (including the back-synchronization of the structure).
sapi::Status InvokeStringReversal(Sandbox* sandbox) {
  StringopApi api(sandbox);
  stringop::StringReverse proto;
  proto.set_input("Hello");
  v::Proto<stringop::StringReverse> pp(proto);
  SAPI_ASSIGN_OR_RETURN(int return_code, api.pb_reverse_string(pp.PtrBoth()));
  TRANSACTION_FAIL_IF_NOT(return_code != 0, "pb_reverse_string failed");
  SAPI_ASSIGN_OR_RETURN(auto pb_result, pp.GetMessage());
  TRANSACTION_FAIL_IF_NOT(pb_result.output() == "olleH", "Incorrect output");
  return sapi::OkStatus();
}

// Benchmark functions:

// Restart SAPI sandbox by letting the sandbox object go out of scope.
// Minimal case for measuring the minimum overhead of restarting the sandbox.
void BenchmarkSandboxRestartOverhead(benchmark::State& state) {
  for (auto _ : state) {
    BasicTransaction st(absl::make_unique<StringopSandbox>());
    // Invoke nop() to make sure that our sandbox is running.
    EXPECT_THAT(st.Run(InvokeNop), IsOk());
  }
}
BENCHMARK(BenchmarkSandboxRestartOverhead);

void BenchmarkSandboxRestartForkserverOverhead(benchmark::State& state) {
  sapi::BasicTransaction st{absl::make_unique<StringopSandbox>()};
  for (auto _ : state) {
    EXPECT_THAT(st.Run(InvokeNop), IsOk());
    EXPECT_THAT(st.GetSandbox()->Restart(true), IsOk());
  }
}
BENCHMARK(BenchmarkSandboxRestartForkserverOverhead);

void BenchmarkSandboxRestartForkserverOverheadForced(benchmark::State& state) {
  sapi::BasicTransaction st{absl::make_unique<StringopSandbox>()};
  for (auto _ : state) {
    EXPECT_THAT(st.Run(InvokeNop), IsOk());
    EXPECT_THAT(st.GetSandbox()->Restart(false), IsOk());
  }
}
BENCHMARK(BenchmarkSandboxRestartForkserverOverheadForced);

// Reuse the sandbox. Used to measure the overhead of the call invocation.
void BenchmarkCallOverhead(benchmark::State& state) {
  BasicTransaction st(absl::make_unique<StringopSandbox>());
  for (auto _ : state) {
    EXPECT_THAT(st.Run(InvokeNop), IsOk());
  }
}
BENCHMARK(BenchmarkCallOverhead);

// Make use of protobufs.
void BenchmarkProtobufHandling(benchmark::State& state) {
  BasicTransaction st(absl::make_unique<StringopSandbox>());
  for (auto _ : state) {
    EXPECT_THAT(st.Run(InvokeStringReversal), IsOk());
  }
}
BENCHMARK(BenchmarkProtobufHandling);

// Measure overhead of synchronizing data.
void BenchmarkIntDataSynchronization(benchmark::State& state) {
  auto sandbox = absl::make_unique<StringopSandbox>();
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
TEST(SAPITest, HasStackTraces) {
  auto sandbox = absl::make_unique<StringopSandbox>();
  ASSERT_THAT(sandbox->Init(), IsOk());
  StringopApi api(sandbox.get());
  EXPECT_THAT(api.violate(), StatusIs(sapi::StatusCode::kUnavailable));
  const auto& result = sandbox->AwaitResult();
  EXPECT_THAT(result.GetStackTrace(), HasSubstr("violate"));
  EXPECT_THAT(result.final_status(), Eq(sandbox2::Result::VIOLATION));
}

// Various tests:

// Leaks a file descriptor inside the sandboxee.
int leak_file_descriptor(sapi::Sandbox* sandbox, const char* path) {
  int raw_fd = open(path, O_RDONLY);
  sapi::v::Fd fd(raw_fd);  // Takes ownership of the raw fd.
  EXPECT_THAT(sandbox->TransferToSandboxee(&fd), IsOk());
  // We want to leak the remote FD. The local FD will still be closed.
  fd.OwnRemoteFd(false);
  return fd.GetRemoteFd();
}

// Make sure that restarting the sandboxee works (= fresh set of FDs).
TEST(SandboxTest, RestartSandboxFD) {
  sapi::BasicTransaction st{absl::make_unique<SumSandbox>()};

  auto test_body = [](sapi::Sandbox* sandbox) -> sapi::Status {
    // Open some FDs and check their value.
    EXPECT_THAT(leak_file_descriptor(sandbox, "/proc/self/exe"), Eq(3));
    EXPECT_THAT(leak_file_descriptor(sandbox, "/proc/self/exe"), Eq(4));
    SAPI_RETURN_IF_ERROR(sandbox->Restart(false));
    // We should have a fresh sandbox now = FDs open previously should be
    // closed now.
    EXPECT_THAT(leak_file_descriptor(sandbox, "/proc/self/exe"), Eq(3));
    return sapi::OkStatus();
  };

  EXPECT_THAT(st.Run(test_body), IsOk());
}

TEST(SandboxTest, RestartTransactionSandboxFD) {
  sapi::BasicTransaction st{absl::make_unique<SumSandbox>()};

  EXPECT_THAT(st.Run([](sapi::Sandbox* sandbox) -> sapi::Status {
    EXPECT_THAT(leak_file_descriptor(sandbox, "/proc/self/exe"), Eq(3));
    return sapi::OkStatus();
  }),
              IsOk());

  EXPECT_THAT(st.Run([](sapi::Sandbox* sandbox) -> sapi::Status {
    EXPECT_THAT(leak_file_descriptor(sandbox, "/proc/self/exe"), Eq(4));
    return sapi::OkStatus();
  }),
              IsOk());

  EXPECT_THAT(st.Restart(), IsOk());

  EXPECT_THAT(st.Run([](sapi::Sandbox* sandbox) -> sapi::Status {
    EXPECT_THAT(leak_file_descriptor(sandbox, "/proc/self/exe"), Eq(3));
    return sapi::OkStatus();
  }),
              IsOk());
}

// Make sure we can recover from a dying sandbox.
TEST(SandboxTest, RestartSandboxAfterCrash) {
  sapi::BasicTransaction st{absl::make_unique<SumSandbox>()};

  auto test_body = [](sapi::Sandbox* sandbox) -> sapi::Status {
    SumApi sumapi(sandbox);
    // Crash the sandbox.
    EXPECT_THAT(sumapi.crash(), StatusIs(sapi::StatusCode::kUnavailable));
    EXPECT_THAT(sumapi.sum(1, 2).status(),
                StatusIs(sapi::StatusCode::kUnavailable));
    EXPECT_THAT(sandbox->AwaitResult().final_status(),
                Eq(sandbox2::Result::SIGNALED));

    // Restart the sandbox.
    EXPECT_THAT(sandbox->Restart(false), IsOk());
    // The sandbox should now be responsive again.
    auto result_or = sumapi.sum(1, 2);
    EXPECT_THAT(result_or.ValueOrDie(), Eq(3));
    return sapi::OkStatus();
  };

  EXPECT_THAT(st.Run(test_body), IsOk());
}

TEST(SandboxTest, RestartSandboxAfterViolation) {
  sapi::BasicTransaction st{absl::make_unique<SumSandbox>()};

  auto test_body = [](sapi::Sandbox* sandbox) -> sapi::Status {
    SumApi sumapi(sandbox);
    // Violate the sandbox policy.
    EXPECT_THAT(sumapi.violate(), StatusIs(sapi::StatusCode::kUnavailable));
    EXPECT_THAT(sumapi.sum(1, 2).status(),
                StatusIs(sapi::StatusCode::kUnavailable));
    EXPECT_THAT(sandbox->AwaitResult().final_status(),
                Eq(sandbox2::Result::VIOLATION));

    // Restart the sandbox.
    EXPECT_THAT(sandbox->Restart(false), IsOk());
    // The sandbox should now be responsive again.
    auto result_or = sumapi.sum(1, 2);
    EXPECT_THAT(result_or, IsOk());
    EXPECT_THAT(result_or.ValueOrDie(), Eq(3));
    return sapi::OkStatus();
  };

  EXPECT_THAT(st.Run(test_body), IsOk());
}

TEST(SandboxTest, NoRaceInAwaitResult) {
  auto sandbox = absl::make_unique<StringopSandbox>();
  ASSERT_THAT(sandbox->Init(), IsOk());
  StringopApi api(sandbox.get());
  EXPECT_THAT(api.violate(), StatusIs(sapi::StatusCode::kUnavailable));
  absl::SleepFor(absl::Milliseconds(200));  // make sure we lose the race
  const auto& result = sandbox->AwaitResult();
  EXPECT_THAT(result.final_status(), Eq(sandbox2::Result::VIOLATION));
}

}  // namespace
}  // namespace sapi
