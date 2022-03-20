// Copyright 2022 Google LLC
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

#include <fstream>

#include <fcntl.h>

#include "contrib/libpcap/sandboxed.h"
#include "contrib/libpcap/utils/utils_libpcap.h"
#include "sandboxed_api/util/path.h"
#include "sandboxed_api/util/status_matchers.h"
#include "sandboxed_api/util/temp_file.h"

#define PKG_COUNT 3
#define BUF_SIZE 5

namespace {

using ::sapi::IsOk;

struct PkgInfo {
  size_t size;
  size_t sec;
  size_t usec;
  uint8_t buf[BUF_SIZE];
};

const struct TestCase {
  std::string filename;
  size_t pkg_count;
  std::string client_ip;
  std::string server_ip;
  std::string random_ip;
  uint16_t port;
  size_t port_count;
  uint16_t random_port;
  size_t dst_client_ip_filter_count;
  size_t src_client_ip_filter_count;
  size_t dst_server_ip_filter_count;
  size_t src_server_ip_filter_count;
  PkgInfo pkg_info[PKG_COUNT];
} kTestData[] = {
    {
        .filename = "rdp.pcap",
        .pkg_count = 448,
        .client_ip = "10.226.41.226",
        .server_ip = "10.226.24.52",
        .random_ip = "127.127.127.127",
        .port = 3389,
        .port_count = 448,
        .random_port = 80,
        .dst_client_ip_filter_count = 241,
        .src_client_ip_filter_count = 207,
        .dst_server_ip_filter_count = 207,
        .src_server_ip_filter_count = 241,
        .pkg_info = {
          {
            .size = 62,
            .sec = 1193266689,
            .usec = 110734,
            .buf = { 0x00, 0x00, 0x0c, 0x07, 0xac }
          },
          {
            .size = 62,
            .sec = 1193266689,
            .usec = 111112,
            .buf = { 0x00, 0x06, 0x1b, 0xc7, 0x86 }
          },
          {
            .size = 54,
            .sec = 1193266689,
            .usec = 111153,
            .buf = { 0x00, 0x00, 0x0c, 0x07, 0xac }
          },
        },
    },
    {
        .filename = "http.cap",
        .pkg_count = 43,
        .client_ip = "145.254.160.237",
        .server_ip = "65.208.228.223",
        .random_ip = "127.127.127.127",
        .port = 80,
        .port_count = 41,
        .random_port = 1337,
        .dst_client_ip_filter_count = 23,
        .src_client_ip_filter_count = 20,
        .dst_server_ip_filter_count = 16,
        .src_server_ip_filter_count = 18,
        .pkg_info = {
          {
            .size = 62,
            .sec = 1084443427,
            .usec = 311224,
            .buf = { 0xfe, 0xff, 0x20, 0x00, 0x01 }
          },
          {
            .size = 62,
            .sec = 1084443428,
            .usec = 222534,
            .buf = { 0x00, 0x00, 0x01, 0x00, 0x00 }
          },
          {
            .size = 54,
            .sec = 1084443428,
            .usec = 222534,
            .buf = { 0xfe, 0xff, 0x20, 0x00, 0x01 }
          },
        },
    },
};

class LibPcapBase : public testing::Test {
 protected:
  std::string GetTestFilePath(const std::string& filename) {
    return sapi::file::JoinPath(test_dir_, filename);
  }

  void SetUp() override;

  std::unique_ptr<LibpcapSapiSandbox> sandbox_;
  const char* test_dir_;
};

class LibPcapTestFiles : public LibPcapBase,
                         public testing::WithParamInterface<TestCase> {};

void LibPcapBase::SetUp() {
  sandbox_ = std::make_unique<LibpcapSapiSandbox>();
  ASSERT_THAT(sandbox_->Init(), IsOk());

  test_dir_ = getenv("TEST_FILES_DIR");
  ASSERT_NE(test_dir_, nullptr);
}

absl::StatusOr<size_t> PkgCount(LibPcap* pcap) {
  size_t count = 0;
  bool finished = false;
  while (!finished) {
    SAPI_ASSIGN_OR_RETURN(LibPcapPacket pkg, pcap->Next());
    finished = pkg.Finished();
    if (!finished) {
      count += 1;
    }
  }

  return count;
}

TEST_F(LibPcapBase, FailToOpen) {
  LibPcap pcap(sandbox_.get(), "unexistings_file.pcap");

  ASSERT_FALSE(pcap.IsInit());
}

TEST_P(LibPcapTestFiles, TestOpen) {
  const TestCase& tv = GetParam();

  LibPcap pcap(sandbox_.get(), GetTestFilePath(tv.filename).c_str());
  SAPI_ASSERT_OK(pcap.CheckIsInit());
}

TEST_P(LibPcapTestFiles, PkgCount) {
  const TestCase& tv = GetParam();

  LibPcap pcap(sandbox_.get(), GetTestFilePath(tv.filename).c_str());
  SAPI_ASSERT_OK(pcap.CheckIsInit());
  SAPI_ASSERT_OK_AND_ASSIGN(size_t count, PkgCount(&pcap));
  ASSERT_EQ(count, tv.pkg_count);
}

TEST_P(LibPcapTestFiles, HostFilterClient) {
  const TestCase& tv = GetParam();

  LibPcap pcap(sandbox_.get(), GetTestFilePath(tv.filename).c_str());
  SAPI_ASSERT_OK(pcap.CheckIsInit());
  SAPI_ASSERT_OK(pcap.SetFilter(absl::StrCat("host ", tv.client_ip)));
  SAPI_ASSERT_OK_AND_ASSIGN(size_t count, PkgCount(&pcap));
  ASSERT_EQ(count,
            tv.dst_client_ip_filter_count + tv.src_client_ip_filter_count);
}

TEST_P(LibPcapTestFiles, HostFilterServer) {
  const TestCase& tv = GetParam();

  LibPcap pcap(sandbox_.get(), GetTestFilePath(tv.filename).c_str());
  SAPI_ASSERT_OK(pcap.CheckIsInit());
  SAPI_ASSERT_OK(pcap.SetFilter(absl::StrCat("host ", tv.server_ip)));
  SAPI_ASSERT_OK_AND_ASSIGN(size_t count, PkgCount(&pcap));
  ASSERT_EQ(count,
            tv.dst_server_ip_filter_count + tv.src_server_ip_filter_count);
}

TEST_P(LibPcapTestFiles, RandomHostFilterServer) {
  const TestCase& tv = GetParam();

  LibPcap pcap(sandbox_.get(), GetTestFilePath(tv.filename).c_str());
  SAPI_ASSERT_OK(pcap.CheckIsInit());
  SAPI_ASSERT_OK(pcap.SetFilter(absl::StrCat("host ", tv.random_ip)));
  SAPI_ASSERT_OK_AND_ASSIGN(size_t count, PkgCount(&pcap));
  ASSERT_EQ(count, 0);
}

TEST_P(LibPcapTestFiles, PortFilter) {
  const TestCase& tv = GetParam();

  LibPcap pcap(sandbox_.get(), GetTestFilePath(tv.filename).c_str());
  SAPI_ASSERT_OK(pcap.CheckIsInit());
  SAPI_ASSERT_OK(pcap.SetFilter(absl::StrCat("port ", tv.port)));
  SAPI_ASSERT_OK_AND_ASSIGN(size_t count, PkgCount(&pcap));
  ASSERT_EQ(count, tv.port_count);
}

TEST_P(LibPcapTestFiles, RandomPortFilter) {
  const TestCase& tv = GetParam();

  LibPcap pcap(sandbox_.get(), GetTestFilePath(tv.filename).c_str());
  SAPI_ASSERT_OK(pcap.CheckIsInit());
  SAPI_ASSERT_OK(pcap.SetFilter(absl::StrCat("port ", tv.random_port)));
  SAPI_ASSERT_OK_AND_ASSIGN(size_t count, PkgCount(&pcap));
  ASSERT_EQ(count, 0);
}

TEST_P(LibPcapTestFiles, DstClient) {
  const TestCase& tv = GetParam();

  LibPcap pcap(sandbox_.get(), GetTestFilePath(tv.filename).c_str());
  SAPI_ASSERT_OK(pcap.CheckIsInit());
  SAPI_ASSERT_OK(pcap.SetFilter(absl::StrCat("dst ", tv.client_ip)));
  SAPI_ASSERT_OK_AND_ASSIGN(size_t count, PkgCount(&pcap));
  ASSERT_EQ(count, tv.dst_client_ip_filter_count);
}

TEST_P(LibPcapTestFiles, SrcClient) {
  const TestCase& tv = GetParam();

  LibPcap pcap(sandbox_.get(), GetTestFilePath(tv.filename).c_str());
  SAPI_ASSERT_OK(pcap.CheckIsInit());
  SAPI_ASSERT_OK(pcap.SetFilter(absl::StrCat("src ", tv.client_ip)));
  SAPI_ASSERT_OK_AND_ASSIGN(size_t count, PkgCount(&pcap));
  ASSERT_EQ(count, tv.src_client_ip_filter_count);
}

TEST_P(LibPcapTestFiles, DstServer) {
  const TestCase& tv = GetParam();

  LibPcap pcap(sandbox_.get(), GetTestFilePath(tv.filename).c_str());
  SAPI_ASSERT_OK(pcap.CheckIsInit());
  SAPI_ASSERT_OK(pcap.SetFilter(absl::StrCat("dst ", tv.server_ip)));
  SAPI_ASSERT_OK_AND_ASSIGN(size_t count, PkgCount(&pcap));
  ASSERT_EQ(count, tv.dst_server_ip_filter_count);
}

TEST_P(LibPcapTestFiles, SrcServer) {
  const TestCase& tv = GetParam();

  LibPcap pcap(sandbox_.get(), GetTestFilePath(tv.filename).c_str());
  SAPI_ASSERT_OK(pcap.CheckIsInit());
  SAPI_ASSERT_OK(pcap.SetFilter(absl::StrCat("src ", tv.server_ip)));
  SAPI_ASSERT_OK_AND_ASSIGN(size_t count, PkgCount(&pcap));
  ASSERT_EQ(count, tv.src_server_ip_filter_count);
}

TEST_P(LibPcapTestFiles, PkgSize) {
  const TestCase& tv = GetParam();

  LibPcap pcap(sandbox_.get(), GetTestFilePath(tv.filename).c_str());
  SAPI_ASSERT_OK(pcap.CheckIsInit());
  for (int i = 0; i < PKG_COUNT; i++) {
    SAPI_ASSERT_OK_AND_ASSIGN(LibPcapPacket pkg, pcap.Next());
    ASSERT_FALSE(pkg.Finished());
    SAPI_ASSERT_OK_AND_ASSIGN(absl::Span<const uint8_t> data,
      pkg.GetData());
    ASSERT_EQ(data.size(), tv.pkg_info[i].size);
  }
}

TEST_P(LibPcapTestFiles, PkgSec) {
  const TestCase& tv = GetParam();

  LibPcap pcap(sandbox_.get(), GetTestFilePath(tv.filename).c_str());
  SAPI_ASSERT_OK(pcap.CheckIsInit());
  for (int i = 0; i < PKG_COUNT; i++) {
    SAPI_ASSERT_OK_AND_ASSIGN(LibPcapPacket pkg, pcap.Next());
    ASSERT_FALSE(pkg.Finished());
    SAPI_ASSERT_OK_AND_ASSIGN(size_t sec, pkg.GetSec());
    ASSERT_EQ(sec, tv.pkg_info[i].sec);
  }
}

TEST_P(LibPcapTestFiles, PkgUsec) {
  const TestCase& tv = GetParam();

  LibPcap pcap(sandbox_.get(), GetTestFilePath(tv.filename).c_str());
  SAPI_ASSERT_OK(pcap.CheckIsInit());
  for (int i = 0; i < PKG_COUNT; i++) {
    SAPI_ASSERT_OK_AND_ASSIGN(LibPcapPacket pkg, pcap.Next());
    ASSERT_FALSE(pkg.Finished());
    SAPI_ASSERT_OK_AND_ASSIGN(size_t usec, pkg.GetUsec());
    ASSERT_EQ(usec, tv.pkg_info[i].usec);
  }
}

TEST_P(LibPcapTestFiles, PkgVal) {
  const TestCase& tv = GetParam();

  LibPcap pcap(sandbox_.get(), GetTestFilePath(tv.filename).c_str());
  SAPI_ASSERT_OK(pcap.CheckIsInit());
  for (int i = 0; i < PKG_COUNT; i++) {
    SAPI_ASSERT_OK_AND_ASSIGN(LibPcapPacket pkg, pcap.Next());
    ASSERT_FALSE(pkg.Finished());
    SAPI_ASSERT_OK_AND_ASSIGN(absl::Span<const uint8_t> data,
      pkg.GetData());
    for (int j = 0; j < BUF_SIZE; j++) {
      ASSERT_EQ(data[j], tv.pkg_info[i].buf[j]);
    }
  }
}

INSTANTIATE_TEST_SUITE_P(LibPcapBase, LibPcapTestFiles,
                         testing::ValuesIn(kTestData));

}  // namespace
