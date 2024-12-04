// Copyright 2024 Google LLC
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

#include "sandboxed_api/sandbox2/util/pid_waiter.h"

#include <cerrno>
#include <memory>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace sandbox2 {
namespace {

using ::testing::_;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::SetErrnoAndReturn;

constexpr int kPrioStatus = 7 << 8;
constexpr int kFirstStatus = 5 << 8;
constexpr int kSecondStatus = 8 << 8;
constexpr pid_t kPrioPid = 1;
constexpr pid_t kFirstPid = 2;
constexpr pid_t kSecondPid = 3;

class MockWaitPid : public PidWaiter::WaitPidInterface {
 public:
  MOCK_METHOD(int, WaitPid, (pid_t, int*, int), (override));
};

TEST(PidWaiterTest, NoEvents) {
  auto mock_wait_pid = std::make_unique<MockWaitPid>();
  EXPECT_CALL(*mock_wait_pid, WaitPid(_, _, _)).WillRepeatedly(Return(0));
  PidWaiter waiter(1, std::move(mock_wait_pid));
  int status;
  EXPECT_EQ(waiter.Wait(&status), 0);
}

TEST(PidWaiterTest, NoProcess) {
  auto mock_wait_pid = std::make_unique<MockWaitPid>();
  EXPECT_CALL(*mock_wait_pid, WaitPid(_, _, _))
      .WillRepeatedly(SetErrnoAndReturn(ECHILD, -1));
  PidWaiter waiter(1, std::move(mock_wait_pid));
  int status;
  EXPECT_EQ(waiter.Wait(&status), -1);
  EXPECT_EQ(errno, ECHILD);
}

TEST(PidWaiterTest, PriorityRespected) {
  auto mock_wait_pid = std::make_unique<MockWaitPid>();
  EXPECT_CALL(*mock_wait_pid, WaitPid(-1, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(kFirstStatus), Return(kFirstPid)))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_wait_pid, WaitPid(kPrioPid, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(kPrioStatus), Return(kPrioPid)))
      .WillOnce(Return(0))
      .WillRepeatedly(DoAll(SetArgPointee<1>(kPrioStatus), Return(kPrioPid)));
  PidWaiter waiter(kPrioPid, std::move(mock_wait_pid));
  int status;
  EXPECT_EQ(waiter.Wait(&status), kPrioPid);
  EXPECT_EQ(status, kPrioStatus);
  EXPECT_EQ(waiter.Wait(&status), kFirstPid);
  EXPECT_EQ(status, kFirstStatus);
  EXPECT_EQ(waiter.Wait(&status), kPrioPid);
  EXPECT_EQ(status, kPrioStatus);
}

TEST(PidWaiterTest, BatchesWaits) {
  auto mock_wait_pid = std::make_unique<MockWaitPid>();
  EXPECT_CALL(*mock_wait_pid, WaitPid(kPrioPid, _, _))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_wait_pid, WaitPid(-1, _, _))
      .Times(3)
      .WillOnce(DoAll(SetArgPointee<1>(kFirstStatus), Return(kFirstPid)))
      .WillOnce(DoAll(SetArgPointee<1>(kSecondStatus), Return(kSecondPid)))
      .WillRepeatedly(Return(0));
  PidWaiter waiter(kPrioPid, std::move(mock_wait_pid));
  int status;
  EXPECT_EQ(waiter.Wait(&status), kFirstPid);
  EXPECT_EQ(status, kFirstStatus);
}

TEST(PidWaiterTest, ReturnsFromBatch) {
  auto mock_wait_pid = std::make_unique<MockWaitPid>();
  EXPECT_CALL(*mock_wait_pid, WaitPid(kPrioPid, _, _))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_wait_pid, WaitPid(-1, _, _))
      .Times(3)
      .WillOnce(DoAll(SetArgPointee<1>(kFirstStatus), Return(kFirstPid)))
      .WillOnce(DoAll(SetArgPointee<1>(kSecondStatus), Return(kSecondPid)))
      .WillRepeatedly(Return(0));
  PidWaiter waiter(kPrioPid, std::move(mock_wait_pid));
  int status;
  EXPECT_EQ(waiter.Wait(&status), kFirstPid);
  EXPECT_EQ(status, kFirstStatus);
  EXPECT_EQ(waiter.Wait(&status), kSecondPid);
  EXPECT_EQ(status, kSecondStatus);
}

TEST(PidWaiterTest, ChangePriority) {
  auto mock_wait_pid = std::make_unique<MockWaitPid>();
  EXPECT_CALL(*mock_wait_pid, WaitPid(kFirstPid, _, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(kFirstStatus), Return(kFirstPid)));
  EXPECT_CALL(*mock_wait_pid, WaitPid(kSecondPid, _, _))
      .WillRepeatedly(
          DoAll(SetArgPointee<1>(kSecondStatus), Return(kSecondPid)));
  PidWaiter waiter(kFirstPid, std::move(mock_wait_pid));
  int status;
  EXPECT_EQ(waiter.Wait(&status), kFirstPid);
  EXPECT_EQ(status, kFirstStatus);
  EXPECT_EQ(waiter.Wait(&status), kFirstPid);
  EXPECT_EQ(status, kFirstStatus);
  waiter.SetPriorityPid(kSecondPid);
  EXPECT_EQ(waiter.Wait(&status), kSecondPid);
  EXPECT_EQ(status, kSecondStatus);
}

}  // namespace
}  // namespace sandbox2
