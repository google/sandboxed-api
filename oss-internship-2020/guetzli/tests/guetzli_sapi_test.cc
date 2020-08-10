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

namespace guetzli {
namespace sandbox {
namespace tests {

namespace {

constexpr const char* IN_PNG_FILENAME = "bees.png";
constexpr const char* IN_JPG_FILENAME = "landscape.jpg";

constexpr int IN_PNG_FILE_SIZE = 177'424;
constexpr int IN_JPG_FILE_SIZE = 14'418;

constexpr int DEFAULT_QUALITY_TARGET = 95;

constexpr const char* RELATIVE_PATH_TO_TESTDATA =
  "/guetzli/guetzli-sandboxed/tests/testdata/";

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

TEST_F(GuetzliSapiTest, ReadDataFromFd) {
  std::string input_file_path = GetPathToInputFile(IN_PNG_FILENAME);
  int fd = open(input_file_path.c_str(), O_RDONLY);
  ASSERT_TRUE(fd != -1) << "Error opening input file";
  sapi::v::Fd remote_fd(fd);
  auto send_fd_status = sandbox_->TransferToSandboxee(&remote_fd);
  ASSERT_TRUE(send_fd_status.ok()) << "Error sending fd to sandboxee";
  ASSERT_TRUE(remote_fd.GetRemoteFd() != -1) << "Error opening remote fd";
  sapi::v::LenVal data(0);
  auto read_status = 
    api_->ReadDataFromFd(remote_fd.GetRemoteFd(), data.PtrBoth());
  ASSERT_TRUE(read_status.value_or(false)) << "Error reading data from fd";
  ASSERT_EQ(data.GetDataSize(), IN_PNG_FILE_SIZE) << "Wrong size of file";
}

// TEST_F(GuetzliSapiTest, WriteDataToFd) {

// }

TEST_F(GuetzliSapiTest, ReadPng) {
  std::string data = ReadFromFile(GetPathToInputFile(IN_PNG_FILENAME));
  ASSERT_EQ(data.size(), IN_PNG_FILE_SIZE) << "Error reading input file";
  sapi::v::LenVal in_data(data.data(), data.size());
  sapi::v::Int xsize, ysize;
  sapi::v::LenVal rgb_out(0);

  auto status = api_->ReadPng(in_data.PtrBefore(), xsize.PtrBoth(), 
    ysize.PtrBoth(), rgb_out.PtrBoth());
  ASSERT_TRUE(status.value_or(false)) << "Error processing png data";
  ASSERT_EQ(xsize.GetValue(), 444) << "Error parsing width";
  ASSERT_EQ(ysize.GetValue(), 258) << "Error parsing height";
}

TEST_F(GuetzliSapiTest, ReadJpeg) {
  std::string data = ReadFromFile(GetPathToInputFile(IN_JPG_FILENAME));
  ASSERT_EQ(data.size(), IN_JPG_FILE_SIZE) << "Error reading input file";
  sapi::v::LenVal in_data(data.data(), data.size());
  sapi::v::Int xsize, ysize;

  auto status = api_->ReadJpegData(in_data.PtrBefore(), 0, 
    xsize.PtrBoth(), ysize.PtrBoth());
  ASSERT_TRUE(status.value_or(false)) << "Error processing jpeg data";
  ASSERT_EQ(xsize.GetValue(), 180) << "Error parsing width";
  ASSERT_EQ(ysize.GetValue(), 180) << "Error parsing height";
}

// This test can take up to few minutes depending on your hardware
TEST_F(GuetzliSapiTest, ProcessRGB) {
  std::string data = ReadFromFile(GetPathToInputFile(IN_PNG_FILENAME));
  ASSERT_EQ(data.size(), IN_PNG_FILE_SIZE) << "Error reading input file";
  sapi::v::LenVal in_data(data.data(), data.size());
  sapi::v::Int xsize, ysize;
  sapi::v::LenVal rgb_out(0);

  auto status = api_->ReadPng(in_data.PtrBefore(), xsize.PtrBoth(), 
    ysize.PtrBoth(), rgb_out.PtrBoth());
  ASSERT_TRUE(status.value_or(false)) << "Error processing png data";
  ASSERT_EQ(xsize.GetValue(), 444) << "Error parsing width";
  ASSERT_EQ(ysize.GetValue(), 258) << "Error parsing height";
  auto quality = 
    api_->ButteraugliScoreQuality(static_cast<double>(DEFAULT_QUALITY_TARGET));
  ASSERT_TRUE(quality.ok()) << "Error calculating butteraugli quality";
  sapi::v::Struct<Params> params;
  sapi::v::LenVal out_data(0);
  params.mutable_data()->butteraugli_target = quality.value();

  status = api_->ProcessRGBData(params.PtrBefore(), 0, rgb_out.PtrBefore(), 
    xsize.GetValue(), ysize.GetValue(), out_data.PtrBoth());
  ASSERT_TRUE(status.value_or(false)) << "Error processing png file";
  ASSERT_EQ(out_data.GetDataSize(), 38'625);
  //ADD COMPARSION WITH REFERENCE OUTPUT
}

// This test can take up to few minutes depending on your hardware
TEST_F(GuetzliSapiTest, ProcessJpeg) {
  std::string data = ReadFromFile(GetPathToInputFile(IN_JPG_FILENAME));
  ASSERT_EQ(data.size(), IN_JPG_FILE_SIZE) << "Error reading input file";
  sapi::v::LenVal in_data(data.data(), data.size());
  sapi::v::Int xsize, ysize;

  auto status = api_->ReadJpegData(in_data.PtrBefore(), 0, 
    xsize.PtrBoth(), ysize.PtrBoth());
  ASSERT_TRUE(status.value_or(false)) << "Error processing jpeg data";
  ASSERT_EQ(xsize.GetValue(), 180) << "Error parsing width";
  ASSERT_EQ(ysize.GetValue(), 180) << "Error parsing height";

  auto quality = 
    api_->ButteraugliScoreQuality(static_cast<double>(DEFAULT_QUALITY_TARGET));
  ASSERT_TRUE(quality.ok()) << "Error calculating butteraugli quality";
  sapi::v::Struct<Params> params;
  params.mutable_data()->butteraugli_target = quality.value();
  sapi::v::LenVal out_data(0);

  status = api_->ProcessJPEGString(params.PtrBefore(), 0, in_data.PtrBefore(), 
    out_data.PtrBoth());
  ASSERT_TRUE(status.value_or(false)) << "Error processing jpeg file";
  ASSERT_EQ(out_data.GetDataSize(), 10'816);
  //ADD COMPARSION WITH REFERENCE OUTPUT
}

} // namespace tests
} // namespace sandbox
} // namespace guetzli