// Copyright 2020 Google LLC
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

#include "guetzli_transaction.h"  // NOLINT(build/include)

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <fstream>
#include <memory>
#include <sstream>

#include "gtest/gtest.h"

namespace guetzli::sandbox::tests {

namespace {

constexpr absl::string_view kInPngFilename = "bees.png";
constexpr absl::string_view kInJpegFilename = "nature.jpg";
constexpr absl::string_view kOutJpegFilename = "out_jpeg.jpg";
constexpr absl::string_view kOutPngFilename = "out_png.png";
constexpr absl::string_view kPngReferenceFilename = "bees_reference.jpg";
constexpr absl::string_view kJpegReferenceFIlename = "nature_reference.jpg";

constexpr int kPngExpectedSize = 38'625;
constexpr int kJpegExpectedSize = 10'816;

constexpr int kDefaultQualityTarget = 95;
constexpr int kDefaultMemlimitMb = 6000;

constexpr absl::string_view kRelativePathToTestdata =
    "/guetzli_sandboxed/testdata/";

std::string GetPathToFile(absl::string_view filename) {
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
      : path_(path), fd_(open(path, O_RDONLY)) {}

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

  TransactionParams params = {in_path.c_str(), out_path.c_str(), 0,
                              kDefaultQualityTarget, kDefaultMemlimitMb};
  {
    GuetzliTransaction transaction(std::move(params));
    absl::Status result = transaction.Run();

    ASSERT_TRUE(result.ok()) << result.ToString();
  }
  std::string reference_data =
      ReadFromFile(GetPathToFile(kJpegReferenceFIlename));
  FileRemover file_remover(out_path.c_str());
  ASSERT_TRUE(file_remover.get() != -1) << "Error opening output file";
  off_t output_size = lseek(file_remover.get(), 0, SEEK_END);
  ASSERT_EQ(reference_data.size(), output_size)
      << "Different sizes of reference and returned data";
  ASSERT_EQ(lseek(file_remover.get(), 0, SEEK_SET), 0)
      << "Error repositioning out file";

  std::string output;
  output.resize(output_size);
  ssize_t status = read(file_remover.get(), output.data(), output_size);
  ASSERT_EQ(status, output_size) << "Error reading data from temp output file";

  ASSERT_EQ(output, reference_data) << "Returned data doesn't match reference";
}

TEST(GuetzliTransactionTest, TestTransactionPng) {
  std::string in_path = GetPathToFile(kInPngFilename);
  std::string out_path = GetPathToFile(kOutPngFilename);

  TransactionParams params = {in_path.c_str(), out_path.c_str(), 0,
                              kDefaultQualityTarget, kDefaultMemlimitMb};
  {
    GuetzliTransaction transaction(std::move(params));
    absl::Status result = transaction.Run();

    ASSERT_TRUE(result.ok()) << result.ToString();
  }
  std::string reference_data =
      ReadFromFile(GetPathToFile(kPngReferenceFilename));
  FileRemover file_remover(out_path.c_str());
  ASSERT_TRUE(file_remover.get() != -1) << "Error opening output file";
  off_t output_size = lseek(file_remover.get(), 0, SEEK_END);
  ASSERT_EQ(reference_data.size(), output_size)
      << "Different sizes of reference and returned data";
  ASSERT_EQ(lseek(file_remover.get(), 0, SEEK_SET), 0)
      << "Error repositioning out file";

  std::string output;
  output.resize(output_size);
  ssize_t status = read(file_remover.get(), output.data(), output_size);
  ASSERT_EQ(status, output_size) << "Error reading data from temp output file";

  ASSERT_EQ(output, reference_data) << "Returned data doesn't match refernce";
}

}  // namespace guetzli::sandbox::tests
