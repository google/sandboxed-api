// Copyright 2019 Google LLC. All Rights Reserved.
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
#include <sys/stat.h>
#include <sys/types.h>

#include <glog/logging.h>
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "sandboxed_api/util/flag.h"
#include "absl/memory/memory.h"
#include "absl/time/time.h"
#include "sandboxed_api/examples/stringop/lib/sandbox.h"
#include "sandboxed_api/examples/stringop/lib/stringop-sapi.sapi.h"
#include "sandboxed_api/examples/stringop/lib/stringop_params.pb.h"
#include "sandboxed_api/transaction.h"
#include "sandboxed_api/util/status_matchers.h"
#include "sandboxed_api/vars.h"
#include "sandboxed_api/util/canonical_errors.h"
#include "sandboxed_api/util/status.h"

using ::sapi::IsOk;

namespace {

// Tests using the simple transaction (and function pointers):
TEST(StringopTest, ProtobufStringDuplication) {
  sapi::BasicTransaction st(absl::make_unique<StringopSapiSandbox>());
  EXPECT_THAT(st.Run([](sapi::Sandbox* sandbox) -> sapi::Status {
    StringopApi f(sandbox);
    stringop::StringDuplication proto;
    proto.set_input("Hello");
    sapi::v::Proto<stringop::StringDuplication> pp(proto);
    SAPI_ASSIGN_OR_RETURN(int v, f.pb_duplicate_string(pp.PtrBoth()));
    TRANSACTION_FAIL_IF_NOT(v, "pb_duplicate_string failed");
    auto pb_result = pp.GetProtoCopy();
    TRANSACTION_FAIL_IF_NOT(pb_result, "Could not deserialize pb result");
    LOG(INFO) << "Result PB: " << pb_result->DebugString();
    TRANSACTION_FAIL_IF_NOT(pb_result->output() == "HelloHello",
                            "Incorrect output");
    return sapi::OkStatus();
  }),
              IsOk());
}

TEST(StringopTest, ProtobufStringReversal) {
  sapi::BasicTransaction st(absl::make_unique<StringopSapiSandbox>());
  EXPECT_THAT(st.Run([](sapi::Sandbox* sandbox) -> sapi::Status {
    StringopApi f(sandbox);
    stringop::StringReverse proto;
    proto.set_input("Hello");
    sapi::v::Proto<stringop::StringReverse> pp(proto);
    SAPI_ASSIGN_OR_RETURN(int v, f.pb_reverse_string(pp.PtrBoth()));
    TRANSACTION_FAIL_IF_NOT(v, "pb_reverse_string failed");
    auto pb_result = pp.GetProtoCopy();
    TRANSACTION_FAIL_IF_NOT(pb_result, "Could not deserialize pb result");
    LOG(INFO) << "Result PB: " << pb_result->DebugString();
    TRANSACTION_FAIL_IF_NOT(pb_result->output() == "olleH", "Incorrect output");
    return sapi::OkStatus();
  }),
              IsOk());
}

// Tests using raw dynamic buffers.
TEST(StringopTest, RawStringDuplication) {
  sapi::BasicTransaction st(absl::make_unique<StringopSapiSandbox>());
  EXPECT_THAT(st.Run([](sapi::Sandbox* sandbox) -> sapi::Status {
    StringopApi f(sandbox);
    sapi::v::LenVal param("0123456789", 10);
    SAPI_ASSIGN_OR_RETURN(int return_value, f.duplicate_string(param.PtrBoth()));
    TRANSACTION_FAIL_IF_NOT(return_value == 1,
                            "duplicate_string() returned incorrect value");
    TRANSACTION_FAIL_IF_NOT(param.GetDataSize() == 20,
                            "duplicate_string() did not return enough data");
    absl::string_view data(reinterpret_cast<const char*>(param.GetData()),
                           param.GetDataSize());
    TRANSACTION_FAIL_IF_NOT(
        data == "01234567890123456789",
        "duplicate_string() did not return the expected data");
    return sapi::OkStatus();
  }),
              IsOk());
}

TEST(StringopTest, RawStringReversal) {
  sapi::BasicTransaction st(absl::make_unique<StringopSapiSandbox>());
  EXPECT_THAT(st.Run([](sapi::Sandbox* sandbox) -> sapi::Status {
    StringopApi f(sandbox);
    sapi::v::LenVal param("0123456789", 10);
    {
      SAPI_ASSIGN_OR_RETURN(int return_value, f.reverse_string(param.PtrBoth()));
      TRANSACTION_FAIL_IF_NOT(return_value == 1,
                              "reverse_string() returned incorrect value");
      TRANSACTION_FAIL_IF_NOT(param.GetDataSize() == 10,
                              "reverse_string() did not return enough data");
      absl::string_view data(reinterpret_cast<const char*>(param.GetData()),
                             param.GetDataSize());
      TRANSACTION_FAIL_IF_NOT(
          data == "9876543210",
          "reverse_string() did not return the expected data");
    }
    {
      // Let's call it again with different data as argument, reusing the
      // existing LenVal object.
      SAPI_RETURN_IF_ERROR(param.ResizeData(sandbox->GetRpcChannel(), 16));
      memcpy(param.GetData() + 10, "ABCDEF", 6);
      TRANSACTION_FAIL_IF_NOT(param.GetDataSize() == 16,
                              "Resize did not behave correctly");
      absl::string_view data(reinterpret_cast<const char*>(param.GetData()),
                             param.GetDataSize());
      TRANSACTION_FAIL_IF_NOT(data == "9876543210ABCDEF",
                              "Data not as expected");
      SAPI_ASSIGN_OR_RETURN(int return_value, f.reverse_string(param.PtrBoth()));
      TRANSACTION_FAIL_IF_NOT(return_value == 1,
                              "reverse_string() returned incorrect value");
      data = absl::string_view(reinterpret_cast<const char*>(param.GetData()),
                               param.GetDataSize());
      TRANSACTION_FAIL_IF_NOT(
          data == "FEDCBA0123456789",
          "reverse_string() did not return the expected data");
    }
    return sapi::OkStatus();
  }),
              IsOk());
}

}  // namespace
