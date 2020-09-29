#include <gmock/gmock-more-matchers.h>
#include "sapi_minitar.h"
#include "gtest/gtest.h"
#include "gmock/gmock.h"
#include "sandboxed_api/util/status_matchers.h"
// #include "testing/base/public/gunit.h"
// #include "testing/base/public/gunit.h"

using ::testing::IsTrue;
using ::testing::Eq;

using ::sandbox2::file_util::fileops::Exists;

namespace {

class MiniTarTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            std::cout << "SETUP INITIAL" << std::endl;
            data_dir_ = CreateTempDirAtCWD();
            ASSERT_THAT(Exists(data_dir_, false), IsTrue()) << "Test data directory was not created";
            std::cout << "tmpdir = " << data_dir_ << std::endl;
            cnt = 0;
        }

        static void TearDownTestSuite() {
            sandbox2::file_util::fileops::DeleteRecursively(data_dir_);
            std::cout << "TEARDOWN END" << std::endl;
            std::cout << "cnt = " << cnt << std::endl;
        }


        void SetUp() override {
            std::cout << "setup every test" << std::endl;
            ++cnt;
        }

        void TearDown() override {
            std::cout << "teardown every test" << std::endl;
            ++cnt;
        }
        static int cnt;
        static std::string data_dir_;
};

int MiniTarTest::cnt;
std::string MiniTarTest::data_dir_;

TEST_F(MiniTarTest, Test1) {
        ASSERT_THAT(true, IsTrue()) << "TEST";
}


TEST_F(MiniTarTest, Test2) {
        ASSERT_THAT(true, IsTrue()) << "TEST";
}



TEST(TESTEX1, TESTEX2) {
    ASSERT_THAT(true, IsTrue()) << "TEST";
}

}  // namespace
