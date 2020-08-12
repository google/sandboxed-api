#include "gtest/gtest.h"
#include "guetzli_sandbox.h"
#include "sandboxed_api/sandbox2/util/fileops.h"
#include "sandboxed_api/vars.h"

#include <fstream>
#include <memory>
#include <syscall.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sstream>
#include <algorithm>

namespace guetzli {
namespace sandbox {
namespace tests {

namespace {

constexpr const char* IN_PNG_FILENAME = "bees.png";
constexpr const char* IN_JPG_FILENAME = "nature.jpg";
constexpr const char* PNG_REFERENCE_FILENAME = "bees_reference.jpg";
constexpr const char* JPG_REFERENCE_FILENAME = "nature_reference.jpg";

constexpr int PNG_EXPECTED_SIZE = 38'625;
constexpr int JPG_EXPECTED_SIZE = 10'816;

constexpr int DEFAULT_QUALITY_TARGET = 95;
constexpr int DEFAULT_MEMLIMIT_MB = 6000;

constexpr const char* RELATIVE_PATH_TO_TESTDATA =
  "/guetzli_sandboxed/tests/testdata/";

std::string GetPathToInputFile(const char* filename) {
  return std::string(getenv("TEST_SRCDIR")) 
    + std::string(RELATIVE_PATH_TO_TESTDATA)
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

template<typename Container>
bool CompareBytesInLenValAndContainer(const sapi::v::LenVal& lenval, 
                                      const Container& container) {
  return std::equal(
    lenval.GetData(), lenval.GetData() + lenval.GetDataSize(),
    container.begin(),
    [](const uint8_t lhs, const auto rhs) {
      return lhs == static_cast<uint8_t>(rhs);
    }
  );
}

} // namespace

class GuetzliSapiTest : public ::testing::Test {
protected:
  void SetUp() override {
    sandbox_ = std::make_unique<GuetzliSapiSandbox>();
    sandbox_->Init().IgnoreError();
    api_ = std::make_unique<GuetzliApi>(sandbox_.get());
  }
  
  std::unique_ptr<GuetzliSapiSandbox> sandbox_;
  std::unique_ptr<GuetzliApi> api_;
};

// This test can take up to few minutes depending on your hardware
TEST_F(GuetzliSapiTest, ProcessRGB) {
  sapi::v::Fd in_fd(open(GetPathToInputFile(IN_PNG_FILENAME).c_str(), 
    O_RDONLY));
  ASSERT_TRUE(in_fd.GetValue() != -1) << "Error opening input file";
  ASSERT_EQ(api_->sandbox()->TransferToSandboxee(&in_fd), absl::OkStatus())
    << "Error transfering fd to sandbox";
  ASSERT_TRUE(in_fd.GetRemoteFd() != -1) << "Error opening remote fd";
  sapi::v::Struct<ProcessingParams> processing_params;
  *processing_params.mutable_data() = {in_fd.GetRemoteFd(), 
                                      0,
                                      DEFAULT_QUALITY_TARGET,
                                      DEFAULT_MEMLIMIT_MB
  };
  sapi::v::LenVal output(0);
  auto processing_result = api_->ProcessRgb(processing_params.PtrBefore(), 
                                            output.PtrBoth());
  ASSERT_TRUE(processing_result.value_or(false)) << "Error processing rgb data";
  ASSERT_EQ(output.GetDataSize(), PNG_EXPECTED_SIZE) 
    << "Incorrect result data size";
  std::string reference_data = 
    ReadFromFile(GetPathToInputFile(PNG_REFERENCE_FILENAME));
  ASSERT_EQ(output.GetDataSize(), reference_data.size()) 
    << "Incorrect result data size";
  ASSERT_TRUE(CompareBytesInLenValAndContainer(output, reference_data))
      << "Processed data doesn't match reference output";
}

// This test can take up to few minutes depending on your hardware
TEST_F(GuetzliSapiTest, ProcessJpeg) {
  sapi::v::Fd in_fd(open(GetPathToInputFile(IN_JPG_FILENAME).c_str(), 
    O_RDONLY));
  ASSERT_TRUE(in_fd.GetValue() != -1) << "Error opening input file";
  ASSERT_EQ(api_->sandbox()->TransferToSandboxee(&in_fd), absl::OkStatus())
    << "Error transfering fd to sandbox";
  ASSERT_TRUE(in_fd.GetRemoteFd() != -1) << "Error opening remote fd";
  sapi::v::Struct<ProcessingParams> processing_params;
  *processing_params.mutable_data() = {in_fd.GetRemoteFd(), 
                                      0,
                                      DEFAULT_QUALITY_TARGET,
                                      DEFAULT_MEMLIMIT_MB
  };
  sapi::v::LenVal output(0);
  auto processing_result = api_->ProcessJpeg(processing_params.PtrBefore(), 
                                            output.PtrBoth());
  ASSERT_TRUE(processing_result.value_or(false)) << "Error processing jpg data";
  ASSERT_EQ(output.GetDataSize(), JPG_EXPECTED_SIZE) 
    << "Incorrect result data size";
  std::string reference_data = 
    ReadFromFile(GetPathToInputFile(JPG_REFERENCE_FILENAME));
  ASSERT_EQ(output.GetDataSize(), reference_data.size()) 
    << "Incorrect result data size";
  ASSERT_TRUE(CompareBytesInLenValAndContainer(output, reference_data))
      << "Processed data doesn't match reference output";
}

// TEST_F(GuetzliSapiTest, WriteDataToFd) {
//   sapi::v::Fd fd(open(".", O_TMPFILE | O_RDWR, S_IRUSR | S_IWUSR));
// }

} // namespace tests
} // namespace sandbox
} // namespace guetzli