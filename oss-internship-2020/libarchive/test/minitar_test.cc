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

#include <fstream>

#include "sapi_minitar.h"  // NOLINT(build/include)
#include "gtest/gtest.h"
#include "sandboxed_api/sandbox2/util.h"
#include "sandboxed_api/util/fileops.h"
#include "sandboxed_api/util/path.h"
#include "sandboxed_api/util/status_matchers.h"

namespace {

using ::sandbox2::util::VecStringToCharPtrArr;
using ::sapi::IsOk;
using ::sapi::file::JoinPath;
using ::sapi::file_util::fileops::Exists;
using ::testing::Eq;
using ::testing::IsTrue;
using ::testing::StrEq;

// We will use a fixture class for testing which allows us to override the
// SetUp and TearDown functions. Also, data that needs to be initialized
// or destroyed only once (the test files and directories) will be handled
// in the SetUpTestSuite and TearDownTestSuite functions which are executed
// only once.
// All of the testing data will be placed in a temporary directory and each
// test will have it's own temporary directory. At the end of each test
// and all of the tests, the temporary data is deleted.
class MiniTarTest : public ::testing::Test {
 protected:
  // Before running the tests, we create a temporary directory which will
  // store generated files and directories used for testing.
  // The directory will look as follows:
  // -file1
  // -dir1 - file2
  //       - dir2 - file3
  static void SetUpTestSuite() {
    absl::StatusOr<std::string> tmp_status = CreateTempDirAtCWD();
    ASSERT_THAT(tmp_status, IsOk());
    data_dir_ = new std::string(std::move(tmp_status).value());

    init_wd_ = new std::string(sandbox2::file_util::fileops::GetCWD());
    ASSERT_THAT(Exists(data_dir_, false), IsTrue())
        << "Test data directory was not created";
    ASSERT_THAT(chdir(data_dir_.data()), Eq(0))
        << "Could not chdir into test data directory";

    CreateAndWriteToFile(kFile1);
    ASSERT_THAT(mkdir(kDir1.data(), 0755), Eq(0)) << "Could not create dir1";
    CreateAndWriteToFile(kFile2);
    ASSERT_THAT(mkdir(kDir2.data(), 0755), Eq(0)) << "Could not create dir2";
    CreateAndWriteToFile(kFile3);

    test_count_ = 0;
  }

  static void TearDownTestSuite() {
    // The tests have the data directory as their working directory at the end
    // so we move to the initial working directory in order to not delete the
    // directory that we are inside of.
    ASSERT_THAT(chdir(init_wd_->data()), Eq(0))
        << "Could not chdir into initial working directory";
    EXPECT_THAT(sandbox2::file_util::fileops::DeleteRecursively(*data_dir_),
                IsTrue)
        << "Error during test data deletion";
    delete init_wd_;
    delete data_dir_;
  }

  void SetUp() override {
    // We use a unique id based on test count to make sure that files created
    // during tests do not overlap.
    id_ = "test" + std::to_string(test_count_);

    absl::StatusOr<std::string> tmp_status = CreateTempDirAtCWD();
    ASSERT_THAT(tmp_status, IsOk());
    tmp_dir_ = tmp_status.value();

    ASSERT_THAT(Exists(tmp_dir_, false), IsTrue)
        << "Could not create test specific temporary directory";
    ASSERT_THAT(chdir(data_dir_->data()), Eq(0))
        << "Could not chdir into test data directory";
  }

  void TearDown() override {
    // Move to another directory before deleting the temporary folder.
    ASSERT_THAT(chdir(data_dir_->data()), Eq(0))
        << "Could not chdir into test data directory";

    EXPECT_THAT(sandbox2::file_util::fileops::DeleteRecursively(tmp_dir_),
                IsTrue)
        << "Error during test temporary directory deletion";
    ++test_count_;
  }

  // Creates the file specified and writes the same filename.
  // This is done in order to not have completely empty files for the
  // archiving step.
  static void CreateAndWriteToFile(absl::string_view file) {
    std::ofstream fin(file.data());
    ASSERT_THAT(fin.is_open(), IsTrue()) << "Could not create" << file;
    fin << file;
    fin.close();
  }

  // Checks if the files exists and if the contents are correct.
  // In these tests, each file contains the relative path from the test
  // directory.
  // Example: dir1/dir2/file3 will contain dir1/dir2/file3.
  // What the files contain does not matter as much, the only important thing
  // is that they are not empty so we can check if the contents are preserved.
  static void CheckFile(const std::string& file) {
    ASSERT_THAT(Exists(file, false), IsTrue()) << "Could not find " << file;
    std::ifstream fin(file);
    ASSERT_THAT(fin.is_open(), IsTrue()) << "Error when opening " << file;

    std::string file_contents((std::istreambuf_iterator<char>(fin)),
                              std::istreambuf_iterator<char>());

    EXPECT_THAT(file_contents, StrEq(file))
        << "Contents of " << file << " are different after extraction";
    fin.close();
  }

  static int test_count_;
  static std::string* data_dir_;
  static std::string* init_wd_;
  std::string tmp_dir_;
  std::string id_;

  static constexpr absl::string_view kFile1 = "file1";
  static constexpr absl::string_view kFile2 = "dir1/file2";
  static constexpr absl::string_view kFile3 = "dir1/dir2/file3";
  static constexpr absl::string_view kDir1 = "dir1";
  static constexpr absl::string_view kDir2 = "dir1/dir2";
};

int MiniTarTest::test_count_;
std::string* MiniTarTest::data_dir_;
std::string* MiniTarTest::init_wd_;

// The tests have the following pattern:
// 1) From inside the test data directory, call the create function with
// different arguments.
// 2) Move to the test specific temporary directory created during the
// set up phase.
// 3) Extract the archive created at step 1.
// 4) Check that the files in the archive have been extracted correctly
// by first checking if they exist and then checking if the content is the
// same as in the original file.
TEST_F(MiniTarTest, TestFileSimple) {
  std::vector<std::string> v = {kFile1.data()};

  ASSERT_THAT(CreateArchive(id_.data(), 0, VecStringToCharPtrArr(v), false),
              IsOk());

  ASSERT_THAT(chdir(tmp_dir_.data()), Eq(0))
      << "Could not chdir into test data directory";

  ASSERT_THAT(ExtractArchive(JoinPath(*data_dir_, id_).data(), 1, 0, false),
              IsOk());

  CheckFile(std::string(kFile1));
}

TEST_F(MiniTarTest, TestMultipleFiles) {
  std::vector<std::string> v = {kFile1.data(), kFile2.data(), kFile3.data()};
  ASSERT_THAT(CreateArchive(id_.data(), 0, VecStringToCharPtrArr(v), false),
              IsOk());
  ASSERT_THAT(Exists(id_.data(), false), IsTrue())
      << "Archive file was not created";

  ASSERT_THAT(chdir(tmp_dir_.data()), Eq(0))
      << "Could not chdir into test data directory";

  ASSERT_THAT(ExtractArchive(JoinPath(*data_dir_, id_).data(), 1, 0, false),
              IsOk());

  CheckFile(std::string(kFile1));
  CheckFile(std::string(kFile2));
  CheckFile(std::string(kFile3));
}

TEST_F(MiniTarTest, TestDirectorySimple) {
  std::vector<std::string> v = {kDir2.data()};
  ASSERT_THAT(CreateArchive(id_.data(), 0, VecStringToCharPtrArr(v), false),
              IsOk());

  ASSERT_THAT(chdir(tmp_dir_.data()), Eq(0))
      << "Could not chdir into test data directory";

  ASSERT_THAT(ExtractArchive(JoinPath(*data_dir_, id_).data(), 1, 0, false),
              IsOk());

  CheckFile(std::string(kFile3));
}

TEST_F(MiniTarTest, TestDirectoryNested) {
  std::vector<std::string> v = {kDir1.data()};
  ASSERT_THAT(CreateArchive(id_.data(), 0, VecStringToCharPtrArr(v), false),
              IsOk());

  ASSERT_THAT(chdir(tmp_dir_.data()), Eq(0))
      << "Could not chdir into test data directory";

  ASSERT_THAT(ExtractArchive(JoinPath(*data_dir_, id_).data(), 1, 0, false),
              IsOk());

  CheckFile(std::string(kFile2));
  CheckFile(std::string(kFile3));
}

TEST_F(MiniTarTest, TestComplex) {
  std::vector<std::string> v = {kFile1.data(), kDir1.data()};
  ASSERT_THAT(CreateArchive(id_.data(), 0, VecStringToCharPtrArr(v), false),
              IsOk());

  ASSERT_THAT(chdir(tmp_dir_.data()), Eq(0))
      << "Could not chdir into test data directory";

  ASSERT_THAT(ExtractArchive(JoinPath(*data_dir_, id_).data(), 1, 0, false),
              IsOk());

  CheckFile(std::string(kFile1));
  CheckFile(std::string(kFile2));
  CheckFile(std::string(kFile3));
}

TEST_F(MiniTarTest, TestCompress) {
  std::vector<std::string> v = {kFile1.data(), kDir1.data()};
  int compress = 'Z';
  ASSERT_THAT(
      CreateArchive(id_.data(), compress, VecStringToCharPtrArr(v), false),
      IsOk());

  ASSERT_THAT(chdir(tmp_dir_.data()), Eq(0))
      << "Could not chdir into test data directory";
  ASSERT_THAT(ExtractArchive(JoinPath(*data_dir_, id_).data(), 1, 0, false),
              IsOk());

  CheckFile(std::string(kFile1));
  CheckFile(std::string(kFile2));
  CheckFile(std::string(kFile3));
}

TEST_F(MiniTarTest, TestGZIP) {
  std::vector<std::string> v = {kFile1.data(), kDir1.data()};
  int compress = 'z';
  ASSERT_THAT(
      CreateArchive(id_.data(), compress, VecStringToCharPtrArr(v), false),
      IsOk());

  ASSERT_THAT(chdir(tmp_dir_.data()), Eq(0))
      << "Could not chdir into test data directory";
  ASSERT_THAT(ExtractArchive(JoinPath(*data_dir_, id_).data(), 1, 0, false),
              IsOk());

  CheckFile(std::string(kFile1));
  CheckFile(std::string(kFile2));
  CheckFile(std::string(kFile3));
}

TEST_F(MiniTarTest, TestBZIP2) {
  std::vector<std::string> v = {kFile1.data(), kDir1.data()};
  int compress = 'j';
  ASSERT_THAT(
      CreateArchive(id_.data(), compress, VecStringToCharPtrArr(v), false),
      IsOk());

  ASSERT_THAT(chdir(tmp_dir_.data()), Eq(0))
      << "Could not chdir into test data directory";
  ASSERT_THAT(ExtractArchive(JoinPath(*data_dir_, id_).data(), 1, 0, false),
              IsOk());

  CheckFile(std::string(kFile1));
  CheckFile(std::string(kFile2));
  CheckFile(std::string(kFile3));
}

TEST_F(MiniTarTest, TestPaths) {
  // These should be equivalent to kFile1 and kDir1 after cleaning.
  std::vector<std::string> v = {JoinPath("a/b/../../c/../", kFile1).data(),
                                JoinPath("d/../e/././///../", kDir1).data()};
  ASSERT_THAT(CreateArchive(id_.data(), 0, VecStringToCharPtrArr(v), false),
              IsOk());

  ASSERT_THAT(chdir(tmp_dir_.data()), Eq(0))
      << "Could not chdir into test data directory";
  ASSERT_THAT(ExtractArchive(JoinPath(*data_dir_, id_).data(), 1, 0, false),
              IsOk());

  CheckFile(std::string(kFile1));
  CheckFile(std::string(kFile2));
  CheckFile(std::string(kFile3));
}

}  // namespace
