// #include <gmock/gmock-more-matchers.h>
#include <gmock/gmock-more-matchers.h>
#include <unistd.h>

#include <fstream>
#include <string>

#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "sandboxed_api/sandbox2/util/fileops.h"
#include "sandboxed_api/sandbox2/util/path.h"
#include "sandboxed_api/util/status_matchers.h"
#include "sapi_minitar.h"
// #include "testing/base/public/gunit.h"
// #include "testing/base/public/gunit.h"

using ::sandbox2::file::JoinPath;
using ::testing::Eq;
using ::testing::IsTrue;

using ::sandbox2::file_util::fileops::Exists;

namespace {

class MiniTarTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    data_dir_ = CreateTempDirAtCWD();
    init_wd_ = sandbox2::file_util::fileops::GetCWD();
    ASSERT_THAT(Exists(data_dir_, false), IsTrue())
        << "Test data directory was not created";
    ASSERT_THAT(chdir(data_dir_.c_str()), Eq(0))
        << "Could not chdir into test data directory";

    CreateAndWriteToFile("file1");

    test_count_ = 0;
  }

  static void TearDownTestSuite() {
    // The tests have the data directory as their working directory at the end
    // so we move to the initial working directory in order to not delete the
    // directory that we are inside of.
    ASSERT_THAT(chdir(init_wd_.c_str()), Eq(0))
        << "Could not chdir into initial working directory";
    EXPECT_THAT(sandbox2::file_util::fileops::DeleteRecursively(data_dir_),
                IsTrue)
        << "Error during test data deletion";
  }

  void SetUp() override {
    id_ = "test" + std::to_string(test_count_);
    tmp_dir_ = CreateTempDirAtCWD();
    ASSERT_THAT(Exists(tmp_dir_, false), IsTrue)
        << "Could not create test specific temporary directory";
    ASSERT_THAT(chdir(data_dir_.c_str()), Eq(0))
        << "Could not chdir into test data directory";
  }

  void TearDown() override {
    // Move to another directory before deleting the temporary folder
    ASSERT_THAT(chdir(data_dir_.c_str()), Eq(0))
        << "Could not chdir into test data directory";

    EXPECT_THAT(sandbox2::file_util::fileops::DeleteRecursively(tmp_dir_),
                IsTrue)
        << "Error during test temporary directory deletion";
    ++test_count_;
  }

  // Creates the file specified and writes the same filename.
  // This is done in order to not have completely empty files for the archiving
  // step.
  static void CreateAndWriteToFile(absl::string_view file) {
    std::ofstream fin(file.data());
    ASSERT_THAT(fin.is_open(), IsTrue()) << "Could not create" << file;
    fin << file;
    fin.close();
  }

  static int test_count_;
  static std::string data_dir_;
  static std::string init_wd_;
  std::string tmp_dir_, id_;
};

int MiniTarTest::test_count_ = 0;
std::string MiniTarTest::data_dir_;
std::string MiniTarTest::init_wd_;

TEST_F(MiniTarTest, Test1) {
  // ASSERT_THAT(true, IsTrue()) << "TEST";
  const char* args[] = {"file1", nullptr};
  create(id_.c_str(), 0, args, false);

  ASSERT_THAT(chdir(tmp_dir_.c_str()), Eq(0))
      << "Could not chdir into test data directory";

  extract(JoinPath(data_dir_, id_).c_str(), 1, 0, false);
  EXPECT_THAT(Exists("file1", false), IsTrue()) << "Could not find file1";
}

TEST_F(MiniTarTest, Test2) { ASSERT_THAT(true, IsTrue()) << "TEST"; }

TEST(TESTEX1, TESTEX2) { ASSERT_THAT(true, IsTrue()) << "TEST"; }

}  // namespace
