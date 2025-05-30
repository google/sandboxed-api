// Copyright 2025 Google LLC
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

#include "sandboxed_api/sandbox2/util/seccomp_unotify.h"

#include <linux/seccomp.h>

#include <cerrno>
#include <memory>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "sandboxed_api/util/fileops.h"

namespace sandbox2::util {
namespace {

using ::absl_testing::IsOk;
using ::sapi::file_util::fileops::FDCloser;
using ::testing::_;
using ::testing::DoAll;
using ::testing::Not;
using ::testing::Return;
using ::testing::SetArgPointee;

class MockSeccompUnotify : public SeccompUnotify::SeccompUnotifyInterface {
 public:
  MOCK_METHOD(int, GetSizes, (seccomp_notif_sizes*), (override));
  MOCK_METHOD(int, ReceiveNotification, (int, seccomp_notif*), (override));
  MOCK_METHOD(int, SendResponse, (int, const seccomp_notif_resp&), (override));
};

TEST(SeccompUnotifyTest, ReceiveRespondFailWithoutInit) {
  SeccompUnotify unotify(std::make_unique<MockSeccompUnotify>());
  EXPECT_THAT(unotify.Receive(), Not(IsOk()));
  seccomp_notif req = {};
  EXPECT_THAT(unotify.RespondErrno(req, EINVAL), Not(IsOk()));
  EXPECT_THAT(unotify.RespondContinue(req), Not(IsOk()));
}

TEST(SeccompUnotifyTest, Normal) {
  seccomp_notif_sizes sizes = {
      .seccomp_notif = sizeof(seccomp_notif) + 100,
      .seccomp_notif_resp = sizeof(seccomp_notif_resp) + 100,
  };
  auto mock_seccomp_unotify = std::make_unique<MockSeccompUnotify>();
  EXPECT_CALL(*mock_seccomp_unotify, GetSizes(_))
      .WillOnce(DoAll(SetArgPointee<0>(sizes), Return(0)));
  EXPECT_CALL(*mock_seccomp_unotify, ReceiveNotification(1, _))
      .WillOnce([&sizes](int fd, seccomp_notif* req) {
        for (int i = sizeof(seccomp_notif); i < sizes.seccomp_notif; ++i) {
          EXPECT_EQ(reinterpret_cast<const char*>(req)[i], 0) << i;
        }
        req->id = 1;
        return 0;
      });
  EXPECT_CALL(*mock_seccomp_unotify, SendResponse(1, _))
      .WillOnce([&sizes](int fd, const seccomp_notif_resp& resp) {
        for (int i = sizeof(seccomp_notif_resp); i < sizes.seccomp_notif_resp;
             ++i) {
          EXPECT_EQ(reinterpret_cast<const char*>(&resp)[i], 0);
        }
        EXPECT_EQ(resp.id, 1);
        EXPECT_EQ(resp.error, EINVAL);
        EXPECT_EQ(resp.flags, 0);
        EXPECT_EQ(resp.val, 0);
        return 0;
      });

  SeccompUnotify unotify(std::move(mock_seccomp_unotify));
  ASSERT_THAT(unotify.Init(FDCloser(1)), IsOk());
  absl::StatusOr<seccomp_notif> req = unotify.Receive();
  ASSERT_THAT(req.status(), IsOk());
  EXPECT_THAT(unotify.RespondErrno(*req, EINVAL), IsOk());
}

// sapi::google3-begin(unotify continue)
TEST(SeccompUnotifyTest, Continue) {
  EXPECT_TRUE(SeccompUnotify::IsContinueSupported());
}
// sapi::google3-end

}  // namespace
}  // namespace sandbox2::util
