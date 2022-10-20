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
#include <sys/stat.h>
#include <sys/types.h>

#include <memory>

#include "absl/base/log_severity.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/check.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "sandboxed_api/examples/sum/sandbox.h"
#include "sandboxed_api/examples/sum/sum-sapi.sapi.h"
#include "sandboxed_api/examples/sum/sum_params.pb.h"
#include "sandboxed_api/transaction.h"
#include "sandboxed_api/vars.h"

namespace {

class SumParams : public sapi::v::Struct<sum_params> {};

class SumTransaction : public sapi::Transaction {
 public:
  SumTransaction(std::unique_ptr<sapi::Sandbox> sandbox, bool crash,
                 bool violate, bool time_out)
      : sapi::Transaction(std::move(sandbox)),
        crash_(crash),
        violate_(violate),
        time_out_(time_out) {
    sapi::Transaction::SetTimeLimit(kTimeOutVal);
  }

 private:
  // Default timeout value for each transaction run.
  const time_t kTimeOutVal = 2;
  // Should the sandboxee crash at some point?
  bool crash_;
  // Should the sandboxee invoke a disallowed syscall?
  bool violate_;
  // Should the sandboxee time_out_?
  bool time_out_;

  // The main processing function.
  absl::Status Main() override;
};

absl::Status SumTransaction::Main() {
  SumApi f(sandbox());
  SAPI_ASSIGN_OR_RETURN(int v, f.sum(1000, 337));
  LOG(INFO) << "1000 + 337 = " << v;
  TRANSACTION_FAIL_IF_NOT(v == 1337, "1000 + 337 != 1337");

  // Sums two int's held in a structure.
  SumParams params;
  params.mutable_data()->a = 1111;
  params.mutable_data()->b = 222;
  params.mutable_data()->ret = 0;
  SAPI_RETURN_IF_ERROR(f.sums(params.PtrBoth()));
  LOG(INFO) << "1111 + 222 = " << params.data().ret;
  TRANSACTION_FAIL_IF_NOT(params.data().ret == 1333, "1111 + 222 != 1333");

  params.mutable_data()->b = -1000;
  SAPI_RETURN_IF_ERROR(f.sums(params.PtrBoth()));
  LOG(INFO) << "1111 - 1000 = " << params.data().ret;
  TRANSACTION_FAIL_IF_NOT(params.data().ret == 111, "1111 - 1000 != 111");

  // Without the wrapper class for struct.
  sapi::v::Struct<sum_params> p;
  p.mutable_data()->a = 1234;
  p.mutable_data()->b = 5678;
  p.mutable_data()->ret = 0;
  SAPI_RETURN_IF_ERROR(f.sums(p.PtrBoth()));
  LOG(INFO) << "1234 + 5678 = " << p.data().ret;
  TRANSACTION_FAIL_IF_NOT(p.data().ret == 6912, "1234 + 5678 != 6912");

  // Gets symbol address and prints its value.
  int* ssaddr;
  SAPI_RETURN_IF_ERROR(
      sandbox()->Symbol("sumsymbol", reinterpret_cast<void**>(&ssaddr)));
  sapi::v::Int sumsymbol;
  sumsymbol.SetRemote(ssaddr);
  SAPI_RETURN_IF_ERROR(sandbox()->TransferFromSandboxee(&sumsymbol));
  LOG(INFO) << "sumsymbol value (exp: 5): " << sumsymbol.GetValue()
            << ", address: " << ssaddr;
  TRANSACTION_FAIL_IF_NOT(sumsymbol.GetValue() == 5,
                          "sumsymbol.GetValue() != 5");

  // Sums all int's inside an array.
  int arr[10];
  sapi::v::Array<int> iarr(arr, ABSL_ARRAYSIZE(arr));
  for (size_t i = 0; i < ABSL_ARRAYSIZE(arr); i++) {
    iarr[i] = i;
  }
  SAPI_ASSIGN_OR_RETURN(v, f.sumarr(iarr.PtrBefore(), iarr.GetNElem()));
  LOG(INFO) << "Sum(iarr, 10 elem, from 0 to 9, exp: 45) = " << v;
  TRANSACTION_FAIL_IF_NOT(v == 45, "Sum(iarr, 10 elem, from 0 to 9) != 45");

  float a = 0.99999f;
  double b = 1.5423432l;
  long double c = 1.1001L;
  SAPI_ASSIGN_OR_RETURN(long double r, f.addf(a, b, c));
  LOG(INFO) << "Addf(" << a << ", " << b << ", " << c << ") = " << r;
  // TODO(szwl): floating point comparison.

  // Prints "Hello World!!!" via puts()
  const char hwstr[] = "Hello World!!!";
  LOG(INFO) << "Print: '" << hwstr << "' via puts()";
  sapi::v::Array<const char> hwarr(hwstr, sizeof(hwstr));
  sapi::v::Int ret;
  SAPI_RETURN_IF_ERROR(sandbox()->Call("puts", &ret, hwarr.PtrBefore()));
  TRANSACTION_FAIL_IF_NOT(ret.GetValue() == 15, "puts('Hello World!!!') != 15");

  sapi::v::Int vp;
  sapi::v::NullPtr nptr;
  LOG(INFO) << "Test whether pointer is NOT NULL - new pointers";
  SAPI_RETURN_IF_ERROR(f.testptr(vp.PtrBefore()));
  LOG(INFO) << "Test whether pointer is NULL";
  SAPI_RETURN_IF_ERROR(f.testptr(&nptr));

  // Protobuf test.
  sumsapi::SumParamsProto proto;
  proto.set_a(10);
  proto.set_b(20);
  proto.set_c(30);
  auto pp = sapi::v::Proto<sumsapi::SumParamsProto>::FromMessage(proto);
  if (!pp.ok()) {
    return pp.status();
  }
  SAPI_ASSIGN_OR_RETURN(v, f.sumproto(pp->PtrBefore()));
  LOG(INFO) << "sumproto(proto {a = 10; b = 20; c = 30}) = " << v;
  TRANSACTION_FAIL_IF_NOT(v == 60,
                          "sumproto(proto {a = 10; b = 20; c = 30}) != 60");

  // Fd transfer test.
  int fdesc = open("/proc/self/exe", O_RDONLY);
  sapi::v::Fd fd(fdesc);
  SAPI_RETURN_IF_ERROR(sandbox()->TransferToSandboxee(&fd));
  LOG(INFO) << "remote_fd = " << fd.GetRemoteFd();
  TRANSACTION_FAIL_IF_NOT(fd.GetRemoteFd() == 3, "remote_fd != 3");

  fdesc = open("/proc/self/comm", O_RDONLY);
  sapi::v::Fd fd2(fdesc);
  SAPI_RETURN_IF_ERROR(sandbox()->TransferToSandboxee(&fd2));
  LOG(INFO) << "remote_fd2 = " << fd2.GetRemoteFd();
  TRANSACTION_FAIL_IF_NOT(fd2.GetRemoteFd() == 4, "remote_fd2 != 4");

  // Read from fd test.
  char buffer[1024] = {0};
  sapi::v::Array<char> buf(buffer, sizeof(buffer));
  sapi::v::UInt size(128);
  SAPI_RETURN_IF_ERROR(
      sandbox()->Call("read", &ret, &fd2, buf.PtrBoth(), &size));
  LOG(INFO) << "Read from /proc/self/comm = [" << buffer << "]";

  // Close test.
  SAPI_RETURN_IF_ERROR(fd2.CloseRemoteFd(sandbox()->rpc_channel()));
  memset(buffer, 0, sizeof(buffer));
  SAPI_RETURN_IF_ERROR(
      sandbox()->Call("read", &ret, &fd2, buf.PtrBoth(), &size));
  LOG(INFO) << "Read from closed /proc/self/comm = [" << buffer << "]";

  // Pass fd as function arg example.
  fdesc = open("/proc/self/statm", O_RDONLY);
  sapi::v::Fd fd3(fdesc);
  SAPI_RETURN_IF_ERROR(sandbox()->TransferToSandboxee(&fd3));
  SAPI_ASSIGN_OR_RETURN(int r2, f.read_int(fd3.GetRemoteFd()));
  LOG(INFO) << "statm value (should not be 0) = " << r2;

  if (crash_) {
    // Crashes the sandboxed part with SIGSEGV
    LOG(INFO) << "Crash with SIGSEGV";
    SAPI_RETURN_IF_ERROR(f.crash());
  }

  if (violate_) {
    LOG(INFO) << "Cause a sandbox (syscall) violation";
    SAPI_RETURN_IF_ERROR(f.violate());
  }

  if (time_out_) {
    SAPI_RETURN_IF_ERROR(f.sleep_for_sec(kTimeOutVal * 2));
  }
  return absl::OkStatus();
}

absl::Status test_addition(sapi::Sandbox* sandbox, int a, int b, int c) {
  SumApi f(sandbox);

  SAPI_ASSIGN_OR_RETURN(int v, f.sum(a, b));
  TRANSACTION_FAIL_IF_NOT(v == c, absl::StrCat(a, " + ", b, " != ", c));
  return absl::OkStatus();
}

}  // namespace

int main(int argc, char* argv[]) {
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();

  absl::Status status;

  sapi::BasicTransaction st(std::make_unique<SumSapiSandbox>());
  // Using the simple transaction (and function pointers):
  CHECK(st.Run(test_addition, 1, 1, 2).ok());
  CHECK(st.Run(test_addition, 1336, 1, 1337).ok());
  CHECK(st.Run(test_addition, 1336, 1, 7).code() ==
        absl::StatusCode::kFailedPrecondition);

  status = st.Run([](sapi::Sandbox* sandbox) -> absl::Status {
    SumApi f(sandbox);

    // Sums two int's held in a structure.
    SumParams params;
    params.mutable_data()->a = 1111;
    params.mutable_data()->b = 222;
    params.mutable_data()->ret = 0;
    SAPI_RETURN_IF_ERROR(f.sums(params.PtrBoth()));
    LOG(INFO) << "1111 + 222 = " << params.data().ret;
    TRANSACTION_FAIL_IF_NOT(params.data().ret == 1333, "1111 + 222 != 1333");
    return absl::OkStatus();
  });
  CHECK(status.ok()) << status.message();

  status = st.Run([](sapi::Sandbox* sandbox) -> absl::Status {
    SumApi f(sandbox);
    SumParams params;
    params.mutable_data()->a = 1111;
    params.mutable_data()->b = -1000;
    params.mutable_data()->ret = 0;
    SAPI_RETURN_IF_ERROR(f.sums(params.PtrBoth()));
    LOG(INFO) << "1111 - 1000 = " << params.data().ret;
    TRANSACTION_FAIL_IF_NOT(params.data().ret == 111, "1111 - 1000 != 111");

    // Without the wrapper class for struct.
    sapi::v::Struct<sum_params> p;
    p.mutable_data()->a = 1234;
    p.mutable_data()->b = 5678;
    p.mutable_data()->ret = 0;
    SAPI_RETURN_IF_ERROR(f.sums(p.PtrBoth()));
    LOG(INFO) << "1234 + 5678 = " << p.data().ret;
    TRANSACTION_FAIL_IF_NOT(p.data().ret == 6912, "1234 + 5678 != 6912");
    return absl::OkStatus();
  });
  CHECK(status.ok()) << status.message();

  // Using overloaded transaction class:
  SumTransaction sapi_crash{std::make_unique<SumSapiSandbox>(), /*crash=*/true,
                            /*violate=*/false,
                            /*time_out=*/false};
  status = sapi_crash.Run();
  LOG(INFO) << "Final run result for crash: " << status;
  CHECK(status.code() == absl::StatusCode::kUnavailable);

  SumTransaction sapi_violate{std::make_unique<SumSapiSandbox>(),
                              /*crash=*/false,
                              /*violate=*/true,
                              /*time_out=*/false};
  status = sapi_violate.Run();
  LOG(INFO) << "Final run result for violate: " << status;
  CHECK(status.code() == absl::StatusCode::kUnavailable);

  SumTransaction sapi_timeout(std::make_unique<SumSapiSandbox>(),
                              /*crash=*/false,
                              /*violate=*/false,
                              /*time_out=*/true);
  status = sapi_timeout.Run();
  LOG(INFO) << "Final run result for timeout: " << status;
  CHECK(status.code() == absl::StatusCode::kUnavailable);

  SumTransaction sapi{std::make_unique<SumSapiSandbox>(), /*crash=*/false,
                      /*violate=*/false, /*time_out=*/false};
  for (int i = 0; i < 32; ++i) {
    status = sapi.Run();
    LOG(INFO) << "Final run result for not a crash: " << status.message();
    CHECK(status.ok());
  }
  return 0;
}
