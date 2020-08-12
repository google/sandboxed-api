#include "gtest/gtest.h"
#include "guetzli_transaction.h"
#include "sandboxed_api/sandbox2/util/fileops.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fstream>
#include <sstream>
#include <memory>

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

} // namespace

TEST(GuetzliTransactionTest, TestTransactionJpg) {
  sandbox2::file_util::fileops::FDCloser in_fd_closer(
    open(GetPathToInputFile(IN_JPG_FILENAME).c_str(), O_RDONLY));
  ASSERT_TRUE(in_fd_closer.get() != -1) << "Error opening input jpg file";
  sandbox2::file_util::fileops::FDCloser out_fd_closer(
    open(".", O_TMPFILE | O_RDWR, S_IRUSR | S_IWUSR));
  ASSERT_TRUE(out_fd_closer.get() != -1) << "Error creating temp output file";
  TransactionParams params = {
    in_fd_closer.get(),
    out_fd_closer.get(),
    0,
    DEFAULT_QUALITY_TARGET,
    DEFAULT_MEMLIMIT_MB
  };
  {
    GuetzliTransaction transaction(std::move(params));
    auto result = transaction.Run();

    ASSERT_TRUE(result.ok()) << result.ToString();
  }
  ASSERT_TRUE(fcntl(out_fd_closer.get(), F_GETFD) != -1 || errno != EBADF)
    << "Local output fd closed";
  auto reference_data = ReadFromFile(GetPathToInputFile(JPG_REFERENCE_FILENAME));
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
    open(GetPathToInputFile(IN_PNG_FILENAME).c_str(), O_RDONLY));
  ASSERT_TRUE(in_fd_closer.get() != -1) << "Error opening input png file";
  sandbox2::file_util::fileops::FDCloser out_fd_closer(
    open(".", O_TMPFILE | O_RDWR, S_IRUSR | S_IWUSR));
  ASSERT_TRUE(out_fd_closer.get() != -1) << "Error creating temp output file";
  TransactionParams params = {
    in_fd_closer.get(),
    out_fd_closer.get(),
    0,
    DEFAULT_QUALITY_TARGET,
    DEFAULT_MEMLIMIT_MB
  };
  {
    GuetzliTransaction transaction(std::move(params));
    auto result = transaction.Run();

    ASSERT_TRUE(result.ok()) << result.ToString();
  }
  ASSERT_TRUE(fcntl(out_fd_closer.get(), F_GETFD) != -1 || errno != EBADF)
    << "Local output fd closed";
  auto reference_data = ReadFromFile(GetPathToInputFile(PNG_REFERENCE_FILENAME));
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

} // namespace tests
} // namespace sandbox
} // namespace guetzli