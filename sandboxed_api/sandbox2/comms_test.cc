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

// Unittest for the sandbox2::Comms class.

#include "sandboxed_api/sandbox2/comms.h"

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <thread>  // NOLINT(build/c++11)
#include <utility>

#include "google/protobuf/text_format.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/container/fixed_array.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/string_view.h"
#include "sandboxed_api/sandbox2/comms_test.pb.h"
#include "sandboxed_api/util/status_matchers.h"

using ::sapi::IsOk;
using ::sapi::StatusIs;
using ::testing::Eq;
using ::testing::IsFalse;
using ::testing::IsTrue;

namespace sandbox2 {

using CommunicationHandler = std::function<void(Comms* comms)>;

class CommsTest : public ::testing::Test {
  void SetUp() override {
    // Comms channel using an abstract socket namespace (initialized with socket
    // name).
    timespec ts1, ts2;
    CHECK_NE(clock_gettime(CLOCK_REALTIME, &ts1), -1);
    CHECK_NE(clock_gettime(CLOCK_REALTIME, &ts2), -1);
    snprintf(
        sockname_, sizeof(sockname_), "comms-test-%u-%u-%u-%u",
        static_cast<uint32_t>(ts1.tv_sec), static_cast<uint32_t>(ts1.tv_nsec),
        static_cast<uint32_t>(ts2.tv_sec), static_cast<uint32_t>(ts2.tv_nsec));
    LOG(INFO) << "Sockname: " << sockname_;

    // Comms channel using a descriptor (initialized with a file descriptor).
    int sv[2];
    CHECK_NE(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), -1);
    fd_server_ = sv[0];
    fd_client_ = sv[1];
    LOG(INFO) << "FD(client): " << fd_client_ << ", FD(server): " << fd_server_;
  }
  void TearDown() override {
    close(fd_server_);
    close(fd_client_);
  }

 protected:
  char sockname_[256];
  int fd_client_;
  int fd_server_;
};

constexpr char kProtoStr[] = "ABCD";
static const absl::string_view NullTestString() {
  static constexpr char kHelperStr[] = "test\0\n\r\t\x01\x02";
  return absl::string_view(kHelperStr, sizeof(kHelperStr) - 1);
}

// Helper function that handles the communication between the two handler
// functions.
void HandleCommunication(const std::string& socketname,
                         const CommunicationHandler& a,
                         const CommunicationHandler& b) {
  Comms comms(socketname);
  comms.Listen();

  // Start handler a.
  std::thread remote([&socketname, &a]() {
    Comms my_comms(socketname);
    CHECK(my_comms.Connect());
    a(&my_comms);
  });

  // Accept connection and run handler b.
  CHECK(comms.Accept());
  b(&comms);
  remote.join();
}

TEST_F(CommsTest, TestSendRecv8) {
  auto a = [](Comms* comms) {
    // Send Uint8.
    ASSERT_THAT(comms->SendUint8(192), IsTrue());

    // Recv Int8.
    int8_t tmp8;
    ASSERT_THAT(comms->RecvInt8(&tmp8), IsTrue());
    EXPECT_THAT(tmp8, Eq(-7));
  };
  auto b = [](Comms* comms) {
    // Recv Uint8.
    uint8_t tmpu8;
    ASSERT_THAT(comms->RecvUint8(&tmpu8), IsTrue());
    EXPECT_THAT(tmpu8, Eq(192));

    // Send Int8.
    ASSERT_THAT(comms->SendInt8(-7), IsTrue());
  };
  HandleCommunication(sockname_, a, b);
}

TEST_F(CommsTest, TestSendRecv16) {
  auto a = [](Comms* comms) {
    // Send Uint16.
    ASSERT_THAT(comms->SendUint16(40001), IsTrue());

    // Recv Int16.
    int16_t tmp16;
    ASSERT_THAT(comms->RecvInt16(&tmp16), IsTrue());
    EXPECT_THAT(tmp16, Eq(-22050));
  };
  auto b = [](Comms* comms) {
    // Recv Uint16.
    uint16_t tmpu16;
    ASSERT_THAT(comms->RecvUint16(&tmpu16), IsTrue());
    EXPECT_THAT(tmpu16, Eq(40001));

    // Send Int16.
    ASSERT_THAT(comms->SendInt16(-22050), IsTrue());
  };
  HandleCommunication(sockname_, a, b);
}

TEST_F(CommsTest, TestSendRecv32) {
  auto a = [](Comms* comms) {
    // SendUint32.
    ASSERT_THAT(comms->SendUint32(3221225472UL), IsTrue());

    // Recv Int32.
    int32_t tmp32;
    ASSERT_THAT(comms->RecvInt32(&tmp32), IsTrue());
    EXPECT_THAT(tmp32, Eq(-1073741824));
  };
  auto b = [](Comms* comms) {
    // Recv Uint32.
    uint32_t tmpu32;
    ASSERT_THAT(comms->RecvUint32(&tmpu32), IsTrue());
    EXPECT_THAT(tmpu32, Eq(3221225472UL));

    // Send Int32.
    ASSERT_THAT(comms->SendInt32(-1073741824), IsTrue());
  };
  HandleCommunication(sockname_, a, b);
}

TEST_F(CommsTest, TestSendRecv64) {
  auto a = [](Comms* comms) {
    // SendUint64.
    ASSERT_THAT(comms->SendUint64(1099511627776ULL), IsTrue());

    // Recv Int64.
    int64_t tmp64;
    ASSERT_THAT(comms->RecvInt64(&tmp64), IsTrue());
    EXPECT_THAT(tmp64, Eq(-1099511627776LL));
  };
  auto b = [](Comms* comms) {
    // Recv Uint64.
    uint64_t tmpu64;
    ASSERT_THAT(comms->RecvUint64(&tmpu64), IsTrue());
    EXPECT_THAT(tmpu64, Eq(1099511627776ULL));

    // Send Int64.
    ASSERT_THAT(comms->SendInt64(-1099511627776LL), IsTrue());
  };
  HandleCommunication(sockname_, a, b);
}

TEST_F(CommsTest, TestTypeMismatch) {
  auto a = [](Comms* comms) {
    uint8_t tmpu8;
    // Receive Int8 (but Uint8 expected).
    EXPECT_THAT(comms->RecvUint8(&tmpu8), IsFalse());
  };
  auto b = [](Comms* comms) {
    // Send Int8 (but Uint8 expected).
    ASSERT_THAT(comms->SendInt8(-93), IsTrue());
  };
  HandleCommunication(sockname_, a, b);
}

TEST_F(CommsTest, TestSendRecvString) {
  auto a = [](Comms* comms) {
    std::string tmps;
    ASSERT_THAT(comms->RecvString(&tmps), IsTrue());
    EXPECT_TRUE(tmps == NullTestString());
    EXPECT_THAT(tmps.size(), Eq(NullTestString().size()));
  };
  auto b = [](Comms* comms) {
    ASSERT_THAT(comms->SendString(std::string(NullTestString())), IsTrue());
  };
  HandleCommunication(sockname_, a, b);
}

TEST_F(CommsTest, TestSendRecvArray) {
  auto a = [](Comms* comms) {
    // Receive 1M bytes.
    std::vector<uint8_t> buffer;
    ASSERT_THAT(comms->RecvBytes(&buffer), IsTrue());
    EXPECT_THAT(buffer.size(), Eq(1024 * 1024));
  };
  auto b = [](Comms* comms) {
    // Send 1M bytes.
    std::vector<uint8_t> buffer(1024 * 1024);
    memset(buffer.data(), 0, buffer.size());
    ASSERT_THAT(comms->SendBytes(buffer), IsTrue());
  };
  HandleCommunication(sockname_, a, b);
}

TEST_F(CommsTest, TestSendRecvFD) {
  auto a = [](Comms* comms) {
    // Receive FD and test it.
    int fd = -1;
    ASSERT_THAT(comms->RecvFD(&fd), IsTrue());
    EXPECT_GE(fd, 0);
    EXPECT_NE(fcntl(fd, F_GETFD), -1);
  };
  auto b = [](Comms* comms) {
    // Send our STDERR to the thread.
    ASSERT_THAT(comms->SendFD(STDERR_FILENO), IsTrue());
  };
  HandleCommunication(sockname_, a, b);
}

TEST_F(CommsTest, TestSendRecvEmptyTLV) {
  auto a = [](Comms* comms) {
    // Receive TLV without a value.
    uint32_t tag;
    std::vector<uint8_t> value;
    ASSERT_THAT(comms->RecvTLV(&tag, &value), IsTrue());  // NOLINT
    EXPECT_THAT(tag, Eq(0x00DEADBE));
    EXPECT_THAT(value.size(), Eq(0));
  };
  auto b = [](Comms* comms) {
    // Send TLV without a value.
    ASSERT_THAT(comms->SendTLV(0x00DEADBE, 0, nullptr), IsTrue());
  };
  HandleCommunication(sockname_, a, b);
}

TEST_F(CommsTest, TestSendRecvEmptyTLV2) {
  auto a = [](Comms* comms) {
    // Receive TLV without a value.
    uint32_t tag;
    std::vector<uint8_t> data;
    ASSERT_THAT(comms->RecvTLV(&tag, &data), IsTrue());
    EXPECT_THAT(tag, Eq(0x00DEADBE));
    EXPECT_THAT(data.size(), Eq(0));
  };
  auto b = [](Comms* comms) {
    // Send TLV without a value.
    ASSERT_THAT(comms->SendTLV(0x00DEADBE, 0, nullptr), IsTrue());
  };
  HandleCommunication(sockname_, a, b);
}

TEST_F(CommsTest, TestSendRecvProto) {
  auto a = [](Comms* comms) {
    // Receive a ProtoBuf.
    std::unique_ptr<CommsTestMsg> comms_msg(new CommsTestMsg());
    ASSERT_THAT(comms->RecvProtoBuf(comms_msg.get()), IsTrue());
    ASSERT_THAT(comms_msg->value_size(), Eq(1));
    EXPECT_THAT(comms_msg->value(0), Eq(kProtoStr));
  };
  auto b = [](Comms* comms) {
    // Send a ProtoBuf.
    std::unique_ptr<CommsTestMsg> comms_msg(new CommsTestMsg());
    comms_msg->add_value(kProtoStr);
    ASSERT_THAT(comms_msg->value_size(), Eq(1));
    ASSERT_THAT(comms->SendProtoBuf(*comms_msg), IsTrue());
  };
  HandleCommunication(sockname_, a, b);
}

TEST_F(CommsTest, TestSendRecvStatusOK) {
  auto a = [](Comms* comms) {
    // Receive a good status.
    absl::Status status;
    ASSERT_THAT(comms->RecvStatus(&status), IsTrue());
    EXPECT_THAT(status, IsOk());
  };
  auto b = [](Comms* comms) {
    // Send a good status.
    ASSERT_THAT(comms->SendStatus(absl::OkStatus()), IsTrue());
  };
  HandleCommunication(sockname_, a, b);
}

TEST_F(CommsTest, TestSendRecvStatusFailing) {
  auto a = [](Comms* comms) {
    // Receive a failing status.
    absl::Status status;
    ASSERT_THAT(comms->RecvStatus(&status), IsTrue());
    EXPECT_THAT(status, Not(IsOk()));
    EXPECT_THAT(status, StatusIs(absl::StatusCode::kInternal, "something odd"));
  };
  auto b = [](Comms* comms) {
    // Send a failing status.
    ASSERT_THAT(comms->SendStatus(
                    absl::Status{absl::StatusCode::kInternal, "something odd"}),
                IsTrue());
  };
  HandleCommunication(sockname_, a, b);
}

TEST_F(CommsTest, TestUsesDistinctBuffers) {
  auto a = [](Comms* comms) {
    // Receive 1M bytes.
    std::vector<uint8_t> buffer1, buffer2;
    ASSERT_THAT(comms->RecvBytes(&buffer1), IsTrue());  // NOLINT
    EXPECT_THAT(buffer1.size(), Eq(1024 * 1024));

    ASSERT_THAT(comms->RecvBytes(&buffer2), IsTrue());  // NOLINT
    EXPECT_THAT(buffer2.size(), Eq(1024 * 1024));

    // Make sure we can access the buffer (memory was not free'd).
    // Probably only useful when running with ASAN/MSAN.
    EXPECT_THAT(buffer1[1024 * 1024 - 1], Eq(buffer1[1024 * 1024 - 1]));
    EXPECT_THAT(buffer2[1024 * 1024 - 1], Eq(buffer2[1024 * 1024 - 1]));
    EXPECT_NE(buffer1.data(), buffer2.data());
  };
  auto b = [](Comms* comms) {
    // Send 1M bytes.
    absl::FixedArray<uint8_t> buf(1024 * 1024);
    memset(buf.data(), 0, buf.size());
    ASSERT_THAT(comms->SendBytes(buf.data(), buf.size()), IsTrue());
    ASSERT_THAT(comms->SendBytes(buf.data(), buf.size()), IsTrue());
  };
  HandleCommunication(sockname_, a, b);
}

TEST_F(CommsTest, TestSendRecvCredentials) {
  auto a = [](Comms* comms) {
    // Check credentials.
    pid_t pid;
    uid_t uid;
    gid_t gid;
    ASSERT_THAT(comms->RecvCreds(&pid, &uid, &gid), IsTrue());
    EXPECT_THAT(pid, Eq(getpid()));
    EXPECT_THAT(uid, Eq(getuid()));
    EXPECT_THAT(gid, Eq(getgid()));
  };
  auto b = [](Comms* comms) {
    // Nothing to do here.
  };
  HandleCommunication(sockname_, a, b);
}

TEST_F(CommsTest, TestSendTooMuchData) {
  auto a = [](Comms* comms) {
    // Nothing to do here.
  };
  auto b = [](Comms* comms) {
    // Send too much data.
    ASSERT_THAT(comms->SendBytes(nullptr, comms->GetMaxMsgSize() + 1),
                IsFalse());
  };
  HandleCommunication(sockname_, a, b);
}

TEST_F(CommsTest, TestSendRecvBytes) {
  auto a = [](Comms* comms) {
    std::vector<uint8_t> buffer;
    ASSERT_THAT(comms->RecvBytes(&buffer), IsTrue());
    ASSERT_THAT(comms->SendBytes(buffer), IsTrue());
  };
  auto b = [](Comms* comms) {
    const std::vector<uint8_t> request = {0, 1, 2, 3, 7};
    ASSERT_THAT(comms->SendBytes(request), IsTrue());

    std::vector<uint8_t> response;
    ASSERT_THAT(comms->RecvBytes(&response), IsTrue());
    EXPECT_THAT(request, Eq(response));
  };
  HandleCommunication(sockname_, a, b);
}

class SenderThread {
 public:
  SenderThread(Comms* comms, size_t rounds) : comms_(comms), rounds_(rounds) {}
  void operator()() {
    for (size_t i = 0; i < rounds_; i++) {
      ASSERT_THAT(
          comms_->SendBytes(reinterpret_cast<const uint8_t*>("Test"), 4),
          IsTrue());
    }
  }

 private:
  Comms* comms_;
  size_t rounds_;
};

class ReceiverThread {
 public:
  ReceiverThread(Comms* comms, size_t rounds)
      : comms_(comms), rounds_(rounds) {}
  void operator()() {
    for (size_t i = 0; i < rounds_; i++) {
      std::vector<uint8_t> buffer;
      EXPECT_THAT(comms_->RecvBytes(&buffer), IsTrue());
      EXPECT_THAT(buffer.size(), Eq(4));
    }
  }

 private:
  Comms* comms_;
  size_t rounds_;
};

TEST_F(CommsTest, TestMultipleThreads) {
  // The comms object should be thread safe, this testcase covers this.
  constexpr size_t kNumThreads = 20;
  constexpr size_t kNumRoundsPerThread = 50;
  constexpr size_t kNumRounds = kNumThreads * kNumRoundsPerThread;
  Comms c(sockname_);
  c.Listen();

  // Start the client thread.
  std::string socketname = sockname_;
  std::thread ct([&socketname]() {
    Comms comms(socketname);
    CHECK(comms.Connect());
    std::vector<uint8_t> buffer;

    // Receive N_ROUND times. We keep the local buffer and send it back
    // later to increase our A/MSAN coverage.
    for (size_t i = 0; i < kNumRounds; i++) {
      ASSERT_THAT(comms.RecvBytes(&buffer), IsTrue());
    }

    for (size_t i = 0; i < kNumRounds; i++) {
      ASSERT_THAT(comms.SendBytes(buffer), IsTrue());
    }
  });

  // Accept connection.
  ASSERT_THAT(c.Accept(), IsTrue());

  // Start sender threads.
  {
    std::thread sender_threads[kNumThreads];
    for (size_t i = 0; i < kNumThreads; i++) {
      sender_threads[i] = std::thread(SenderThread(&c, kNumRoundsPerThread));
    }

    // Join threads.
    for (size_t i = 0; i < kNumThreads; i++) {
      sender_threads[i].join();
    }
  }

  // Start receiver threads.
  {
    std::thread receiver_threads[kNumThreads];
    for (size_t i = 0; i < kNumThreads; i++) {
      receiver_threads[i] =
          std::thread(ReceiverThread(&c, kNumRoundsPerThread));
    }

    // Join threads.
    for (size_t i = 0; i < kNumThreads; i++) {
      receiver_threads[i].join();
    }
  }

  ct.join();
}

// We cannot test this in the Client or Server tests, as the endpoint needs to
// be unconnected.
TEST_F(CommsTest, TestMsgSize) {
  // There will be no actual connection to this socket.
  const std::string socket_name = "sandbox2_comms_msg_size_test";
  Comms c(socket_name);
}

}  // namespace sandbox2
