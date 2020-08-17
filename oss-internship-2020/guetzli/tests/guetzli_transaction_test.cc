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
constexpr const char* kOutJpegFilename = "out_jpeg.jpg";
constexpr const char* kOutPngFilename = "out_png.png";
constexpr const char* kPngReferenceFilename = "bees_reference.jpg";
constexpr const char* kJpegReferenceFIlename = "nature_reference.jpg";

constexpr int kPngExpectedSize = 38'625;
constexpr int kJpegExpectedSize = 10'816;

constexpr int kDefaultQualityTarget = 95;
constexpr int kDefaultMemlimitMb = 6000;

constexpr const char* kRelativePathToTestdata =
  "/guetzli_sandboxed/tests/testdata/";

std::string GetPathToFile(const char* filename) {
  return absl::StrCat(getenv("TEST_SRCDIR"), kRelativePathToTestdata, filename);
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

// Helper class to delete file after opening
class FileRemover {
 public:
  explicit FileRemover(const char* path) 
    : path_(path)
    , fd_(open(path, O_RDONLY))
  {}

  ~FileRemover() {
    close(fd_);
    remove(path_);
  }

  int get() const { return fd_; }

 private:
  const char* path_;
  int fd_;
};

}  // namespace

TEST(GuetzliTransactionTest, TestTransactionJpg) {
  std::string in_path = GetPathToFile(kInJpegFilename);
  std::string out_path = GetPathToFile(kOutJpegFilename);

  TransactionParams params = {
    in_path.c_str(),
    out_path.c_str(),
    0,
    kDefaultQualityTarget,
    kDefaultMemlimitMb
  };
  {
    GuetzliTransaction transaction(std::move(params));
    auto result = transaction.Run();

    ASSERT_TRUE(result.ok()) << result.ToString();
  }
  auto reference_data = ReadFromFile(GetPathToFile(kJpegReferenceFIlename));
  FileRemover file_remover(out_path.c_str());
  ASSERT_TRUE(file_remover.get() != -1) << "Error opening output file";
  auto output_size =  lseek(file_remover.get(), 0, SEEK_END);
  ASSERT_EQ(reference_data.size(), output_size) 
    << "Different sizes of reference and returned data";
  ASSERT_EQ(lseek(file_remover.get(), 0, SEEK_SET), 0) 
    << "Error repositioning out file";
  
  std::unique_ptr<char[]> buf(new char[output_size]);
  auto status = read(file_remover.get(), buf.get(), output_size);
  ASSERT_EQ(status, output_size) << "Error reading data from temp output file";

  ASSERT_TRUE(
    std::equal(buf.get(), buf.get() + output_size, reference_data.begin()))
    << "Returned data doesn't match reference";
}

TEST(GuetzliTransactionTest, TestTransactionPng) {
  std::string in_path = GetPathToFile(kInPngFilename);
  std::string out_path = GetPathToFile(kOutPngFilename);

  TransactionParams params = {
    in_path.c_str(),
    out_path.c_str(),
    0, 
    kDefaultQualityTarget,
    kDefaultMemlimitMb
  };
  {
    GuetzliTransaction transaction(std::move(params));
    auto result = transaction.Run();

    ASSERT_TRUE(result.ok()) << result.ToString();
  }
  auto reference_data = ReadFromFile(GetPathToFile(kPngReferenceFilename));
  FileRemover file_remover(out_path.c_str());
  ASSERT_TRUE(file_remover.get() != -1) << "Error opening output file";
  auto output_size = lseek(file_remover.get(), 0, SEEK_END);
  ASSERT_EQ(reference_data.size(), output_size) 
    << "Different sizes of reference and returned data";
  ASSERT_EQ(lseek(file_remover.get(), 0, SEEK_SET), 0) 
    << "Error repositioning out file";
  
  std::unique_ptr<char[]> buf(new char[output_size]);
  auto status = read(file_remover.get(), buf.get(), output_size);
  ASSERT_EQ(status, output_size) << "Error reading data from temp output file";

  ASSERT_TRUE(
    std::equal(buf.get(), buf.get() + output_size, reference_data.begin()))
    << "Returned data doesn't match refernce";
}

}  // namespace tests
}  // namespace sandbox
}  // namespace guetzli
