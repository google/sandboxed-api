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

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/time/time.h"
#include "sandboxed_api/examples/stringop/sandbox.h"
#include "sandboxed_api/examples/stringop/stringop_params.pb.h"
#include "sandboxed_api/transaction.h"
#include "sandboxed_api/util/status_macros.h"
#include "sandboxed_api/util/status_matchers.h"
#include "sandboxed_api/vars.h"

namespace {

using ::sapi::IsOk;
using ::testing::Eq;
using ::testing::Ne;
using ::testing::SizeIs;
using ::testing::StrEq;

// Tests using a simple transaction (and function pointers):
TEST(StringopTest, ProtobufStringDuplication) {
  sapi::BasicTransaction st(absl::make_unique<StringopSapiSandbox>());
  EXPECT_THAT(st.Run([](sapi::Sandbox* sandbox) -> absl::Status {
    StringopApi api(sandbox);
    stringop::StringDuplication proto;
    proto.set_input("Hello");
    auto pp = sapi::v::Proto<stringop::StringDuplication>::FromMessage(proto);
    if (!pp.ok()) {
      return pp.status();
    }
    {
      SAPI_ASSIGN_OR_RETURN(int return_value,
                            api.pb_duplicate_string(pp->PtrBoth()));
      TRANSACTION_FAIL_IF_NOT(return_value, "pb_duplicate_string() failed");
    }

    SAPI_ASSIGN_OR_RETURN(auto pb_result, pp->GetMessage());
    LOG(INFO) << "Result PB: " << pb_result.DebugString();
    TRANSACTION_FAIL_IF_NOT(pb_result.output() == "HelloHello",
                            "Incorrect output");
    return absl::OkStatus();
  }),
              IsOk());
}

TEST(StringopTest, ProtobufStringReversal) {
  StringopSapiSandbox sandbox;
  ASSERT_THAT(sandbox.Init(), IsOk());
  StringopApi api(&sandbox);

  stringop::StringReverse proto;
  proto.set_input("Hello");
  auto pp = sapi::v::Proto<stringop::StringReverse>::FromMessage(proto);
  SAPI_ASSERT_OK_AND_ASSIGN(int return_value,
                            api.pb_reverse_string(pp->PtrBoth()));
  EXPECT_THAT(return_value, Ne(0)) << "pb_reverse_string() failed";

  SAPI_ASSERT_OK_AND_ASSIGN(auto pb_result, pp->GetMessage());
  LOG(INFO) << "Result PB: " << pb_result.DebugString();
  EXPECT_THAT(pb_result.output(), StrEq("olleH"));
}

TEST(StringopTest, RawStringDuplication) {
  StringopSapiSandbox sandbox;
  ASSERT_THAT(sandbox.Init(), IsOk());
  StringopApi api(&sandbox);

  sapi::v::LenVal param("0123456789", 10);
  SAPI_ASSERT_OK_AND_ASSIGN(int return_value,
                            api.duplicate_string(param.PtrBoth()));
  EXPECT_THAT(return_value, Eq(1)) << "duplicate_string() failed";

  absl::string_view data(reinterpret_cast<const char*>(param.GetData()),
                         param.GetDataSize());
  EXPECT_THAT(data, SizeIs(20))
      << "duplicate_string() did not return enough data";
  EXPECT_THAT(std::string(data), StrEq("01234567890123456789"));
}

TEST(StringopTest, RawStringReversal) {
  StringopSapiSandbox sandbox;
  ASSERT_THAT(sandbox.Init(), IsOk());
  StringopApi api(&sandbox);

  sapi::v::LenVal param("0123456789", 10);
  {
    SAPI_ASSERT_OK_AND_ASSIGN(int return_value,
                              api.reverse_string(param.PtrBoth()));
    EXPECT_THAT(return_value, Eq(1))
        << "reverse_string() returned incorrect value";
    absl::string_view data(reinterpret_cast<const char*>(param.GetData()),
                           param.GetDataSize());
    EXPECT_THAT(param.GetDataSize(), Eq(10))
        << "reverse_string() did not return enough data";
    EXPECT_THAT(std::string(data), StrEq("9876543210"))
        << "reverse_string() did not return the expected data";
  }
  {
    // Let's call it again with different data as argument, reusing the
    // existing LenVal object.
    EXPECT_THAT(param.ResizeData(sandbox.rpc_channel(), 16), IsOk());
    memcpy(param.GetData() + 10, "ABCDEF", 6);
    absl::string_view data(reinterpret_cast<const char*>(param.GetData()),
                           param.GetDataSize());
    EXPECT_THAT(data, SizeIs(16)) << "Resize did not behave correctly";
    EXPECT_THAT(std::string(data), StrEq("9876543210ABCDEF"));

    SAPI_ASSERT_OK_AND_ASSIGN(int return_value,
                              api.reverse_string(param.PtrBoth()));
    EXPECT_THAT(return_value, Eq(1))
        << "reverse_string() returned incorrect value";
    data = absl::string_view(reinterpret_cast<const char*>(param.GetData()),
                             param.GetDataSize());
    EXPECT_THAT(std::string(data), StrEq("FEDCBA0123456789"));
  }
}

TEST(StringopTest, RawStringLength) {
  StringopSapiSandbox sandbox;
  ASSERT_THAT(sandbox.Init(), IsOk());
  StringopApi api(&sandbox);
  SAPI_ASSERT_OK_AND_ASSIGN(void* target_mem_ptr, api.get_raw_c_string());
  SAPI_ASSERT_OK_AND_ASSIGN(size_t len,
                            sandbox.rpc_channel()->Strlen(target_mem_ptr));
  EXPECT_THAT(len, Eq(10));
}

TEST(StringopTest, RawStringReading) {
  StringopSapiSandbox sandbox;
  ASSERT_THAT(sandbox.Init(), IsOk());
  StringopApi api(&sandbox);
  SAPI_ASSERT_OK_AND_ASSIGN(void* target_mem_ptr, api.get_raw_c_string());
  SAPI_ASSERT_OK_AND_ASSIGN(size_t len,
                            sandbox.rpc_channel()->Strlen(target_mem_ptr));
  EXPECT_THAT(len, Eq(10));

  SAPI_ASSERT_OK_AND_ASSIGN(
      std::string data, sandbox.GetCString(sapi::v::RemotePtr(target_mem_ptr)));
  EXPECT_THAT(data, StrEq("Ten chars."));
}

}  // namespace
