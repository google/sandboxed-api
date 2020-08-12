// Copyright 2020 Google LLC
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
#include <unistd.h>

#include <fstream>
#include <memory>
#include <sstream>

#include "gtest/gtest.h"
#include "sandboxed_api/sandbox2/util/fileops.h"

#include "guetzli_transaction.h"

namespace guetzli {
namespace sandbox {
namespace tests {

namespace {

constexpr const char* kInPngFilename = "bees.png";
constexpr const char* kInJpegFilename = "nature.jpg";
constexpr const char* kPngReferenceFilename = "bees_reference.jpg";
constexpr const char* kJpegReferenceFIlename = "nature_reference.jpg";

constexpr int kPngExpectedSize = 38'625;
constexpr int kJpegExpectedSize = 10'816;

constexpr int kDefaultQualityTarget = 95;
constexpr int kDefaultMemlimitMb = 6000;

constexpr const char* kRelativePathToTestdata =
  "/guetzli_sandboxed/tests/testdata/";

std::string GetPathToInputFile(const char* filename) {
  return std::string(getenv("TEST_SRCDIR")) 
    + std::string(kRelativePathToTestdata)
    + std::string(filename);
}

std::string ReadFromFile(const std::string& filename) {
  std::ifstream stream(filename, std::ios::binary);

  if (!stream.is_open()) {
    return "";
  }

  std::stringstream result;
  result << stream.rdbuf();
  return result.str();
}

}  // namespace

TEST(GuetzliTransactionTest, TestTransactionJpg) {
  sandbox2::file_util::fileops::FDCloser in_fd_closer(
    open(GetPathToInputFile(kInJpegFilename).c_str(), O_RDONLY));
  ASSERT_TRUE(in_fd_closer.get() != -1) << "Error opening input jpg file";
  sandbox2::file_util::fileops::FDCloser out_fd_closer(
    open(".", O_TMPFILE | O_RDWR, S_IRUSR | S_IWUSR));
  ASSERT_TRUE(out_fd_closer.get() != -1) << "Error creating temp output file";
  TransactionParams params = {
    in_fd_closer.get(),
    out_fd_closer.get(),
    0,
    kDefaultQualityTarget,
    kDefaultMemlimitMb
  };
  {
    GuetzliTransaction transaction(std::move(params));
    auto result = transaction.Run();

    ASSERT_TRUE(result.ok()) << result.ToString();
  }
  ASSERT_TRUE(fcntl(out_fd_closer.get(), F_GETFD) != -1 || errno != EBADF)
    << "Local output fd closed";
  auto reference_data = ReadFromFile(GetPathToInputFile(kJpegReferenceFIlename));
  auto output_size = lseek(out_fd_closer.get(), 0, SEEK_END);
  ASSERT_EQ(reference_data.size(), output_size) 
    << "Different sizes of reference and returned data";
  ASSERT_EQ(lseek(out_fd_closer.get(), 0, SEEK_SET), 0) 
    << "Error repositioning out file";
  
  std::unique_ptr<char[]> buf(new char[output_size]);
  auto status = read(out_fd_closer.get(), buf.get(), output_size);
  ASSERT_EQ(status, output_size) << "Error reading data from temp output file";

  ASSERT_TRUE(
    std::equal(buf.get(), buf.get() + output_size, reference_data.begin()))
    << "Returned data doesn't match reference";
}

TEST(GuetzliTransactionTest, TestTransactionPng) {
  sandbox2::file_util::fileops::FDCloser in_fd_closer(
    open(GetPathToInputFile(kInPngFilename).c_str(), O_RDONLY));
  ASSERT_TRUE(in_fd_closer.get() != -1) << "Error opening input png file";
  sandbox2::file_util::fileops::FDCloser out_fd_closer(
    open(".", O_TMPFILE | O_RDWR, S_IRUSR | S_IWUSR));
  ASSERT_TRUE(out_fd_closer.get() != -1) << "Error creating temp output file";
  TransactionParams params = {
    in_fd_closer.get(),
    out_fd_closer.get(),
    0,
    kDefaultQualityTarget,
    kDefaultMemlimitMb
  };
  {
    GuetzliTransaction transaction(std::move(params));
    auto result = transaction.Run();

    ASSERT_TRUE(result.ok()) << result.ToString();
  }
  ASSERT_TRUE(fcntl(out_fd_closer.get(), F_GETFD) != -1 || errno != EBADF)
    << "Local output fd closed";
  auto reference_data = ReadFromFile(GetPathToInputFile(kPngReferenceFilename));
  auto output_size = lseek(out_fd_closer.get(), 0, SEEK_END);
  ASSERT_EQ(reference_data.size(), output_size) 
    << "Different sizes of reference and returned data";
  ASSERT_EQ(lseek(out_fd_closer.get(), 0, SEEK_SET), 0) 
    << "Error repositioning out file";
  
  std::unique_ptr<char[]> buf(new char[output_size]);
  auto status = read(out_fd_closer.get(), buf.get(), output_size);
  ASSERT_EQ(status, output_size) << "Error reading data from temp output file";

  ASSERT_TRUE(
    std::equal(buf.get(), buf.get() + output_size, reference_data.begin()))
    << "Returned data doesn't match refernce";
}

}  // namespace tests
}  // namespace sandbox
}  // namespace guetzli
