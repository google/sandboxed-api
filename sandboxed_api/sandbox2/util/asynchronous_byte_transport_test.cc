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

#include "sandboxed_api/sandbox2/util/asynchronous_byte_transport.h"

#include <linux/prctl.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/wait.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "sandboxed_api/sandbox2/buffer.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/testing.h"
#include "sandboxed_api/util/fileops.h"
#include "sandboxed_api/util/thread.h"

namespace {

using ::absl_testing::StatusIs;

class ScopedTimeout {
 public:
  ScopedTimeout(sandbox2::AsynchronousByteTransport* transport,
                absl::Duration timeout)
      : transport_(transport), timeout_(timeout) {
    thread_ = sapi::Thread(this, &ScopedTimeout::Run, "ScopedTimeout");
    start_notification_.WaitForNotification();
  }
  ~ScopedTimeout() {
    finish_notification_.Notify();
    thread_.Join();
  }
  void Run() {
    start_notification_.Notify();
    bool done = finish_notification_.WaitForNotificationWithTimeout(timeout_);
    if (!done) {
      transport_->Terminate();
    }
  }

 private:
  sandbox2::AsynchronousByteTransport* transport_;
  sapi::Thread thread_;
  absl::Notification start_notification_, finish_notification_;
  absl::Duration timeout_;
};

// Helper class to communicate with a fake sandboxee process that reads commands
// from a pipe and communicates with the host process through the
// AsynchronousByteTransport.
class TestHelper {
 public:
  enum class ActionType { kSend, kExchange, kRecv, kTerminate };
  explicit TestHelper() {}

  void StartAsync(int memfd, size_t data_size) {
    int pipe_fds[2];
    pipe(pipe_fds);
    pid_ = fork();
    CHECK_NE(pid_, -1);
    if (pid_ == 0) {
      CHECK_EQ(prctl(PR_SET_PDEATHSIG, SIGKILL, 0, 0, 0), 0);
      close(pipe_fds[1]);
      Communicate(pipe_fds[0], memfd, data_size);
      close(pipe_fds[0]);
      _exit(0);
    }
    close(pipe_fds[0]);
    comms_ = std::make_unique<sandbox2::Comms>(pipe_fds[1], "test_comms");
  }

  void Stop() {
    if (pid_ == -1) {
      return;
    }
    comms_->Terminate();
    comms_.reset();
    int status;
    ASSERT_EQ(waitpid(pid_, &status, 0), pid_);
    ASSERT_TRUE(WIFEXITED(status));
    ASSERT_EQ(WEXITSTATUS(status), 0);
    pid_ = -1;
  }

  void RequestSend(absl::Span<const uint8_t> data) {
    SendAction(TestHelper::ActionType::kSend, data);
  }

  void RequestExchange(absl::Span<const uint8_t> data,
                       absl::Span<const uint8_t> data_to_recv) {
    SendAction(TestHelper::ActionType::kExchange, data, data_to_recv);
  }

  void RequestRecv(absl::Span<const uint8_t> data) {
    SendAction(TestHelper::ActionType::kRecv, data);
  }

  void RequestTerminate() {
    ActionType action_type = ActionType::kTerminate;
    ASSERT_TRUE(comms_->SendInt32(static_cast<int32_t>(action_type)));
  }

 private:
  void Communicate(int socket_fd, int memfd_fd, size_t data_size) {
    SAPI_ASSERT_OK_AND_ASSIGN(
        auto buffer_client,
        sandbox2::Buffer::CreateFromFd(
            sapi::file_util::fileops::FDCloser(memfd_fd), data_size));
    SAPI_ASSERT_OK_AND_ASSIGN(
        auto transport_client,
        sandbox2::AsynchronousByteTransport::CreateSandboxeeSide(
            std::move(buffer_client)));
    sandbox2::Comms comms(socket_fd, "test_comms_sandboxee");
    ActionType action_type;
    while (comms.RecvInt32(reinterpret_cast<int32_t*>(&action_type))) {
      if (action_type == ActionType::kSend) {
        std::vector<uint8_t> data;
        CHECK_EQ(comms.RecvBytes(&data), true);
        CHECK_OK(transport_client->Send(data));
      } else if (action_type == ActionType::kExchange) {
        std::vector<uint8_t> data_to_send;
        CHECK_EQ(comms.RecvBytes(&data_to_send), true);
        std::vector<uint8_t> data_to_recv;
        CHECK_EQ(comms.RecvBytes(&data_to_recv), true);
        std::vector<uint8_t> recv_data(data_to_recv.size());
        CHECK_OK(transport_client->Exchange(
            data_to_send,
            absl::Span<uint8_t>(recv_data.data(), recv_data.size())));
        ASSERT_EQ(recv_data, data_to_recv);
      } else if (action_type == ActionType::kRecv) {
        std::vector<uint8_t> data;
        CHECK_EQ(comms.RecvBytes(&data), true);
        std::vector<uint8_t> data_recv(data.size());
        CHECK_OK(transport_client->Recv(
            absl::Span<uint8_t>(data_recv.data(), data_recv.size())));
        CHECK_EQ(data, data_recv);
      } else if (action_type == ActionType::kTerminate) {
        transport_client->Terminate();
      }
    }
  }

  void SendAction(TestHelper::ActionType action_type,
                  absl::Span<const uint8_t> data,
                  absl::Span<const uint8_t> data_to_recv = {}) {
    ASSERT_TRUE(comms_->SendInt32(static_cast<int32_t>(action_type)));
    ASSERT_TRUE(comms_->SendBytes(data.data(), data.size()));
    if (!data_to_recv.empty()) {
      ASSERT_TRUE(comms_->SendBytes(data_to_recv.data(), data_to_recv.size()));
    }
  }

  std::unique_ptr<sandbox2::Comms> comms_;
  pid_t pid_ = -1;
};

class AsynchronousByteTransportTest
    : public ::testing::TestWithParam<
          sandbox2::AsynchronousByteTransport::SynchronizationType> {
 protected:
  void SetUp() override {
    SAPI_ASSERT_OK_AND_ASSIGN(auto buffer,
                              sandbox2::Buffer::CreateWithSize(buffer_size_));
    memset(buffer->data(), 0x41, 4 << 10);
    int memfd = buffer->fd();
    size_t size = buffer->size();
    sandbox2::AsynchronousByteTransport::SynchronizationType
        synchronization_type = GetParam();
    SAPI_ASSERT_OK_AND_ASSIGN(
        transport_, sandbox2::AsynchronousByteTransport::CreateHostSide(
                        std::move(buffer), synchronization_type));
    test_helper_.StartAsync(memfd, size);
    timeout_ =
        std::make_unique<ScopedTimeout>(GetTransport(), absl::Seconds(5));
  }

  void TearDown() override {
    test_helper_.Stop();
    timeout_.reset();
  }

  sandbox2::AsynchronousByteTransport* GetTransport() {
    return transport_.get();
  }

  size_t GetBufferSize() const { return buffer_size_; }

 protected:
  TestHelper test_helper_;

 private:
  size_t buffer_size_ = 132 << 10;
  std::unique_ptr<ScopedTimeout> timeout_;
  std::unique_ptr<sandbox2::AsynchronousByteTransport> transport_;
};

TEST_P(AsynchronousByteTransportTest, SendRecv) {
  std::vector<uint8_t> data(10, 'a');
  test_helper_.RequestSend(data);
  data = std::vector<uint8_t>(10, 'b');
  test_helper_.RequestRecv(data);

  std::vector<uint8_t> data_recv(10);
  SAPI_ASSERT_OK(GetTransport()->Recv(
      absl::Span<uint8_t>(data_recv.data(), data_recv.size())));
  ASSERT_EQ(data_recv, std::vector<uint8_t>(10, 'a'));
  data = std::vector<uint8_t>(10, 'b');
  SAPI_ASSERT_OK(GetTransport()->Send(data));
}

TEST_P(AsynchronousByteTransportTest, SendRecvMultiple) {
  static constexpr int kNumberOfIterations = 100;
  for (int i = 0; i < kNumberOfIterations; ++i) {
    std::vector<uint8_t> data_a(10, 'a' + i);
    std::vector<uint8_t> data_b(10, 'b' + i);

    test_helper_.RequestSend(data_a);
    std::vector<uint8_t> data_recv(10);
    SAPI_ASSERT_OK(GetTransport()->Recv(
        absl::Span<uint8_t>(data_recv.data(), data_recv.size())));
    ASSERT_EQ(data_recv, data_a);
    test_helper_.RequestRecv(data_b);
    SAPI_ASSERT_OK(GetTransport()->Send(data_b));
  }
}

TEST_P(AsynchronousByteTransportTest, SendWillRead) {
  test_helper_.RequestRecv(std::vector<uint8_t>(10, 'a'));
  test_helper_.RequestExchange(std::vector<uint8_t>(10, 'b'),
                               std::vector<uint8_t>(10, 'c'));
  std::vector<uint8_t> data_recv(10);
  SAPI_ASSERT_OK(GetTransport()->Exchange(
      std::vector<uint8_t>(10, 'a'),
      absl::Span<uint8_t>(data_recv.data(), data_recv.size())));
  ASSERT_EQ(data_recv, std::vector<uint8_t>(10, 'b'));
  SAPI_ASSERT_OK(GetTransport()->Send(std::vector<uint8_t>(10, 'c')));
}

TEST_P(AsynchronousByteTransportTest, SendRecvMultipleWillRead) {
  static constexpr int kNumberOfIterations = 100;
  for (int i = 0; i < kNumberOfIterations; ++i) {
    std::vector<uint8_t> data_a(10, 'a' + i);
    std::vector<uint8_t> data_b(10, 'b' + i);
    std::vector<uint8_t> data_c(10, 'c' + i);

    test_helper_.RequestRecv(data_a);
    test_helper_.RequestExchange(data_b, data_c);

    std::vector<uint8_t> data_recv(10);
    SAPI_ASSERT_OK(GetTransport()->Exchange(
        data_a, absl::Span<uint8_t>(data_recv.data(), data_recv.size())));
    ASSERT_EQ(data_recv, data_b);
    SAPI_ASSERT_OK(GetTransport()->Send(data_c));
  }
}

// On one side, we're using will then read, but on the other side, we're not.
TEST_P(AsynchronousByteTransportTest, OneSideWillThenRead) {
  static constexpr int kNumberOfIterations = 100;
  for (int i = 0; i < kNumberOfIterations; ++i) {
    std::vector<uint8_t> data_a(10, 'a' + i);
    std::vector<uint8_t> data_b(10, 'b' + i);
    std::vector<uint8_t> data_c(10, 'c' + i);

    test_helper_.RequestRecv(data_a);
    test_helper_.RequestExchange(data_b, data_c);

    SAPI_ASSERT_OK(GetTransport()->Send(data_a));
    std::vector<uint8_t> data_recv(10);
    SAPI_ASSERT_OK(GetTransport()->Recv(
        absl::Span<uint8_t>(data_recv.data(), data_recv.size())));
    ASSERT_EQ(data_recv, data_b);
    SAPI_ASSERT_OK(GetTransport()->Send(data_c));
  }
}

// Sending multiple large data works, even if sending multiple large messages
// that combined are larger than the buffer.
TEST_P(AsynchronousByteTransportTest, SendingMultipleLargeDataWorks) {
  size_t max_size = GetBufferSize();
  std::vector<uint8_t> data_1(max_size, 'a');
  std::vector<uint8_t> data_2(max_size, 'b');
  std::vector<uint8_t> data_3(max_size, 'c');
  std::vector<uint8_t> data_4(max_size, 'd');

  std::vector<uint8_t> data_recv(max_size);

  test_helper_.RequestRecv(data_1);
  SAPI_ASSERT_OK(GetTransport()->Send(data_1));
  test_helper_.RequestRecv(data_2);
  SAPI_ASSERT_OK(GetTransport()->Send(data_2));
  test_helper_.RequestSend(data_3);
  SAPI_ASSERT_OK(GetTransport()->Recv(
      absl::Span<uint8_t>(data_recv.data(), data_recv.size())));
  ASSERT_EQ(data_recv, data_3);
  test_helper_.RequestSend(data_4);
  SAPI_ASSERT_OK(GetTransport()->Recv(
      absl::Span<uint8_t>(data_recv.data(), data_recv.size())));
  ASSERT_EQ(data_recv, data_4);
}

TEST_P(AsynchronousByteTransportTest, SendLargeData) {
  // That's bigger than the buffer size, so we'll have to send and receive in
  // chunks.
  size_t max_size = GetBufferSize() * 4;
  std::vector<uint8_t> data_to_send(max_size, 'a');
  test_helper_.RequestRecv(data_to_send);
  SAPI_ASSERT_OK(GetTransport()->Send(data_to_send));
}

TEST_P(AsynchronousByteTransportTest, TerminateBeforeRecvWillNotBlock) {
  GetTransport()->Terminate();
  std::vector<uint8_t> data(100);
  ASSERT_THAT(
      GetTransport()->Recv(absl::Span<uint8_t>(data.data(), data.size())),
      StatusIs(absl::StatusCode::kAborted));
}

TEST_P(AsynchronousByteTransportTest, TerminateBeforeSendWillReturnError) {
  GetTransport()->Terminate();
  std::vector<uint8_t> data(100, 'a');
  ASSERT_THAT(
      GetTransport()->Send(absl::Span<const uint8_t>(data.data(), data.size())),
      StatusIs(absl::StatusCode::kAborted));
}

TEST_P(AsynchronousByteTransportTest, TerminateAfterRecvWakesUpWaiting) {
  ScopedTimeout timeout(GetTransport(), absl::Milliseconds(5));
  std::vector<uint8_t> data(100);
  ASSERT_THAT(
      GetTransport()->Recv(absl::Span<uint8_t>(data.data(), data.size())),
      StatusIs(absl::StatusCode::kAborted));
}

TEST_P(AsynchronousByteTransportTest, ReadDataThenTerminate) {
  std::vector<uint8_t> data(100, 'a');
  test_helper_.RequestSend(data);
  test_helper_.RequestTerminate();
  std::vector<uint8_t> data_recv(100);
  SAPI_ASSERT_OK(GetTransport()->Recv(
      absl::Span<uint8_t>(data_recv.data(), data_recv.size())));
  ASSERT_EQ(data_recv, data);
  ASSERT_THAT(GetTransport()->Recv(
                  absl::Span<uint8_t>(data_recv.data(), data_recv.size())),
              StatusIs(absl::StatusCode::kAborted));
}

TEST_P(AsynchronousByteTransportTest, ExchangeWhenDataIsAlreadyInTheBuffer) {
  std::vector<uint8_t> data(100, 'a');
  SAPI_ASSERT_OK(GetTransport()->Send(data));
  std::vector<uint8_t> data_to_send(100, 'b');
  test_helper_.RequestExchange(data_to_send, data);
  std::vector<uint8_t> data_recv(100);
  SAPI_ASSERT_OK(GetTransport()->Recv(
      absl::Span<uint8_t>(data_recv.data(), data_recv.size())));
  ASSERT_EQ(data_recv, data_to_send);
}

TEST_P(AsynchronousByteTransportTest, LastReadAfterConnectionClosedWorks) {
  std::vector<uint8_t> data(100, 'a');
  test_helper_.RequestSend(data);
  test_helper_.RequestTerminate();
  std::vector<uint8_t> data_recv(100);
  SAPI_ASSERT_OK(GetTransport()->Recv(
      absl::Span<uint8_t>(data_recv.data(), data_recv.size())));
  ASSERT_EQ(data_recv, data);
}

TEST_P(AsynchronousByteTransportTest, TwoReadsAfterConnectionClosedFails) {
  std::vector<uint8_t> data(100, 'a');
  test_helper_.RequestSend(data);
  test_helper_.RequestTerminate();
  std::vector<uint8_t> data_recv(100);
  SAPI_ASSERT_OK(GetTransport()->Recv(
      absl::Span<uint8_t>(data_recv.data(), data_recv.size())));
  ASSERT_EQ(data_recv, data);
  ASSERT_THAT(GetTransport()->Recv(
                  absl::Span<uint8_t>(data_recv.data(), data_recv.size())),
              StatusIs(absl::StatusCode::kAborted));
}

TEST_P(AsynchronousByteTransportTest,
       ReadMoreThanBufferWillNotTriggerOutOfBounds) {
  std::vector<uint8_t> data(10, 'a');
  SAPI_ASSERT_OK(GetTransport()->Send(
      absl::Span<const uint8_t>(data.data(), data.size() / 2)));
  test_helper_.RequestRecv(data);
  absl::SleepFor(absl::Milliseconds(10));
  SAPI_ASSERT_OK(GetTransport()->Send(absl::Span<const uint8_t>(
      data.data() + data.size() / 2, data.size() / 2)));
}

INSTANTIATE_TEST_SUITE_P(
    AsynchronousByteTransportTest, AsynchronousByteTransportTest,
    ::testing::Values(
        sandbox2::AsynchronousByteTransport::kFutex),
    [](const ::testing::TestParamInfo<
        sandbox2::AsynchronousByteTransport::SynchronizationType>& info) {
      switch (info.param) {
        case sandbox2::AsynchronousByteTransport::kFutex:
          return "Futex";
        default:
          return "Invalid";
      }
    });

}  // namespace
