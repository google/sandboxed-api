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

#include "contrib/libzip/sandboxed.h"
#include "contrib/libzip/utils/utils_zip.h"
#include "sandboxed_api/util/path.h"
#include "sandboxed_api/util/status_matchers.h"
#include "sandboxed_api/util/temp_file.h"

namespace {

using ::sapi::IsOk;

class ZipBase : public testing::Test {
 protected:
  std::string GetTestFilePath(const std::string& filename);
  std::string GetTemporaryFile(const std::string& filename);
  std::streamsize GetStreamSize(std::ifstream& stream);

  absl::StatusOr<std::vector<uint8_t>> ReadFile(const std::string& filename);

  void SetUp() override;

  const char* test_files_dir_;
  std::string test_path_zip_;

  std::unique_ptr<ZipSapiSandbox> sandbox_;
};

class ZipMultiFiles
    : public ZipBase,
      public testing::WithParamInterface<std::pair<uint64_t, std::string>> {};

void ZipBase::SetUp() {
  test_files_dir_ = getenv("TEST_FILES_DIR");
  ASSERT_NE(test_files_dir_, nullptr);

  test_path_zip_ = GetTestFilePath("zip.zip");

  sandbox_ = std::make_unique<ZipSapiSandbox>();
  ASSERT_THAT(sandbox_->Init(), IsOk());
}

absl::StatusOr<std::vector<uint8_t>> ZipBase::ReadFile(
    const std::string& filename) {
  std::ifstream file(filename, std::ios::binary);
  if (!file.is_open()) {
    return absl::UnavailableError("Unable to open file");
  }

  std::streamsize size = GetStreamSize(file);
  std::vector<uint8_t> buf(size);

  file.read(reinterpret_cast<char*>(buf.data()), size);

  if (file.gcount() != size) {
    return absl::UnavailableError("Unable to read data");
  }

  return buf;
}

std::string ZipBase::GetTestFilePath(const std::string& filename) {
  return sapi::file::JoinPath(test_files_dir_, filename);
}

std::string ZipBase::GetTemporaryFile(const std::string& filename) {
  absl::StatusOr<std::string> tmp_file =
      sapi::CreateNamedTempFileAndClose(filename);
  if (!tmp_file.ok()) {
    return "";
  }

  return sapi::file::JoinPath(sapi::file_util::fileops::GetCWD(), *tmp_file);
}

std::streamsize ZipBase::GetStreamSize(std::ifstream& stream) {
  stream.seekg(0, std::ios_base::end);
  std::streamsize ssize = stream.tellg();
  stream.seekg(0, std::ios_base::beg);

  return ssize;
}

TEST_F(ZipBase, CheckInit) {
  LibZip zip(sandbox_.get(), test_path_zip_, 0);
  ASSERT_THAT(zip.IsOpen(), true);
}

TEST_F(ZipBase, CheckFileCount) {
  LibZip zip(sandbox_.get(), test_path_zip_, 0);
  ASSERT_THAT(zip.IsOpen(), true);

  SAPI_ASSERT_OK_AND_ASSIGN(uint64_t count, zip.GetNumberEntries());
  ASSERT_EQ(count, 2);
}

TEST_F(ZipBase, AddFileBuf) {
  LibZip zip(sandbox_.get(), test_path_zip_, 0);
  ASSERT_THAT(zip.IsOpen(), true);

  SAPI_ASSERT_OK_AND_ASSIGN(auto newdata,
                            ReadFile(GetTestFilePath("notinzip")));

  SAPI_ASSERT_OK_AND_ASSIGN(uint64_t index, zip.AddFile("test", newdata));
  SAPI_ASSERT_OK_AND_ASSIGN(uint64_t count, zip.GetNumberEntries());
  ASSERT_EQ(count, 3);

  SAPI_ASSERT_OK_AND_ASSIGN(std::string newname, zip.GetName(index));
  ASSERT_EQ(newname, "test");
}

TEST_F(ZipBase, AddFileFd) {
  LibZip zip(sandbox_.get(), test_path_zip_, 0);
  ASSERT_THAT(zip.IsOpen(), true);

  int fd = open(GetTestFilePath("notinzip").c_str(), O_RDONLY);
  ASSERT_GE(fd, 0);

  SAPI_ASSERT_OK_AND_ASSIGN(uint64_t index, zip.AddFile("test", fd));
  SAPI_ASSERT_OK_AND_ASSIGN(uint64_t count, zip.GetNumberEntries());
  ASSERT_EQ(count, 3);

  SAPI_ASSERT_OK_AND_ASSIGN(std::string newname, zip.GetName(index));
  ASSERT_EQ(newname, "test");
}

TEST_F(ZipMultiFiles, AddFileBufInplaceStore) {
  std::string new_path_zip = GetTemporaryFile("newzip.zip");

  ASSERT_TRUE(
      sapi::file_util::fileops::CopyFile(test_path_zip_, new_path_zip, 0644));
  LibZip zip(sandbox_.get(), new_path_zip, 0);
  ASSERT_THAT(zip.IsOpen(), true);

  SAPI_ASSERT_OK_AND_ASSIGN(auto newdata,
                            ReadFile(GetTestFilePath("notinzip")));
  SAPI_ASSERT_OK_AND_ASSIGN(uint64_t index, zip.AddFile("test", newdata));

  ASSERT_THAT(zip.Finish(), IsOk());
  ASSERT_THAT(zip.Save(), IsOk());

  LibZip newzip(sandbox_.get(), new_path_zip, 0);
  ASSERT_THAT(newzip.IsOpen(), true);

  SAPI_ASSERT_OK_AND_ASSIGN(auto nameddata, newzip.ReadFile("test"));
  ASSERT_EQ(newdata, newdata);

  SAPI_ASSERT_OK_AND_ASSIGN(auto indexeddata, newzip.ReadFile(index));
  ASSERT_EQ(indexeddata, newdata);
}

TEST_P(ZipMultiFiles, CheckFileNames) {
  LibZip zip(sandbox_.get(), test_path_zip_, 0);
  ASSERT_THAT(zip.IsOpen(), true);

  uint64_t index = GetParam().first;
  std::string origname = GetParam().second;

  SAPI_ASSERT_OK_AND_ASSIGN(std::string name, zip.GetName(index));
  ASSERT_EQ(name, origname);
}

TEST_P(ZipMultiFiles, DeleteFile) {
  LibZip zip(sandbox_.get(), test_path_zip_, 0);
  ASSERT_THAT(zip.IsOpen(), true);

  uint64_t index = GetParam().first;
  std::string origname = GetParam().second;

  SAPI_ASSERT_OK_AND_ASSIGN(uint64_t count, zip.GetNumberEntries());
  ASSERT_THAT(zip.DeleteFile(index), IsOk());

  for (uint64_t i = 0; i < count; i++) {
    absl::StatusOr<std::string> name = zip.GetName(i);
    if (i == index) {
      ASSERT_FALSE(name.ok());
    } else {
      ASSERT_THAT(name, IsOk());
      ASSERT_NE(*name, origname);
    }
  }
}

TEST_P(ZipMultiFiles, ReadFileName) {
  LibZip zip(sandbox_.get(), test_path_zip_, 0);
  ASSERT_THAT(zip.IsOpen(), true);

  std::string name = GetParam().second;

  SAPI_ASSERT_OK_AND_ASSIGN(auto zipdata, zip.ReadFile(name));
  SAPI_ASSERT_OK_AND_ASSIGN(auto origdata, ReadFile(GetTestFilePath(name)));

  ASSERT_EQ(zipdata, origdata);
}

TEST_P(ZipMultiFiles, ReadFileIndex) {
  LibZip zip(sandbox_.get(), test_path_zip_, 0);
  ASSERT_THAT(zip.IsOpen(), true);

  uint64_t index = GetParam().first;
  std::string name = GetParam().second;

  SAPI_ASSERT_OK_AND_ASSIGN(auto zipdata, zip.ReadFile(index));
  SAPI_ASSERT_OK_AND_ASSIGN(auto origdata, ReadFile(GetTestFilePath(name)));

  ASSERT_EQ(zipdata, origdata);
}

TEST_P(ZipMultiFiles, AddFileBufNewStore) {
  LibZip zip(sandbox_.get(), test_path_zip_, 0);
  ASSERT_THAT(zip.IsOpen(), true);

  SAPI_ASSERT_OK_AND_ASSIGN(auto newdata,
                            ReadFile(GetTestFilePath("notinzip")));
  SAPI_ASSERT_OK_AND_ASSIGN(uint64_t index, zip.AddFile("test", newdata));

  std::string new_zip_file_name = GetTemporaryFile("newzip.zip");
  int newfdzip = open(new_zip_file_name.c_str(), O_WRONLY);
  ASSERT_GE(newfdzip, 0);
  ASSERT_THAT(zip.Finish(), IsOk());
  ASSERT_THAT(zip.Save(newfdzip), IsOk());
  close(newfdzip);

  LibZip newzip(sandbox_.get(), new_zip_file_name, 0);
  ASSERT_THAT(newzip.IsOpen(), true);

  SAPI_ASSERT_OK_AND_ASSIGN(auto nameddata, newzip.ReadFile("test"));
  ASSERT_EQ(newdata, newdata);

  SAPI_ASSERT_OK_AND_ASSIGN(auto indexeddata, newzip.ReadFile(index));
  ASSERT_EQ(indexeddata, newdata);

  // We also check if non other data was corrupted
  uint64_t oldindex = GetParam().first;
  std::string name = GetParam().second;

  SAPI_ASSERT_OK_AND_ASSIGN(auto zipdata, newzip.ReadFile(oldindex));
  SAPI_ASSERT_OK_AND_ASSIGN(auto origdata, ReadFile(GetTestFilePath(name)));

  ASSERT_EQ(zipdata, origdata);
}

TEST_P(ZipMultiFiles, AddFileFdStore) {
  LibZip zip(sandbox_.get(), test_path_zip_, 0);
  ASSERT_THAT(zip.IsOpen(), true);

  SAPI_ASSERT_OK_AND_ASSIGN(auto newdata,
                            ReadFile(GetTestFilePath("notinzip")));
  int fd = open(GetTestFilePath("notinzip").c_str(), O_RDONLY);
  ASSERT_GE(fd, 0);
  SAPI_ASSERT_OK_AND_ASSIGN(uint64_t index, zip.AddFile("test", fd));

  std::string new_zip_file_name = GetTemporaryFile("newzip.zip");
  int newfdzip = open(new_zip_file_name.c_str(), O_WRONLY);
  ASSERT_GE(newfdzip, 0);
  ASSERT_THAT(zip.Finish(), IsOk());
  ASSERT_THAT(zip.Save(newfdzip), IsOk());
  close(newfdzip);

  LibZip newzip(sandbox_.get(), new_zip_file_name, 0);
  ASSERT_THAT(newzip.IsOpen(), true);

  SAPI_ASSERT_OK_AND_ASSIGN(auto nameddata, newzip.ReadFile("test"));
  ASSERT_EQ(nameddata, newdata);

  SAPI_ASSERT_OK_AND_ASSIGN(auto indexeddata, newzip.ReadFile(index));
  ASSERT_EQ(indexeddata, newdata);

  // We also check if non other data was corrupted
  uint64_t oldindex = GetParam().first;
  std::string name = GetParam().second;

  SAPI_ASSERT_OK_AND_ASSIGN(auto zipdata, newzip.ReadFile(oldindex));
  SAPI_ASSERT_OK_AND_ASSIGN(auto origdata, ReadFile(GetTestFilePath(name)));

  ASSERT_EQ(zipdata, origdata);
}

TEST_P(ZipMultiFiles, ReplaceFileBufStore) {
  LibZip zip(sandbox_.get(), test_path_zip_, 0);
  ASSERT_THAT(zip.IsOpen(), true);

  uint64_t index = GetParam().first;
  std::string name = GetParam().second;

  SAPI_ASSERT_OK_AND_ASSIGN(auto newdata,
                            ReadFile(GetTestFilePath("notinzip")));
  SAPI_ASSERT_OK_AND_ASSIGN(auto zipdata, zip.ReadFile(index));
  ASSERT_NE(zipdata, newdata);

  ASSERT_THAT(zip.ReplaceFile(index, newdata), IsOk());

  std::string new_zip_file_name = GetTemporaryFile("newzip.zip");
  int newfdzip = open(new_zip_file_name.c_str(), O_WRONLY);
  ASSERT_GE(newfdzip, 0);
  ASSERT_THAT(zip.Finish(), IsOk());
  ASSERT_THAT(zip.Save(newfdzip), IsOk());
  close(newfdzip);

  LibZip newzip(sandbox_.get(), new_zip_file_name, 0);
  ASSERT_THAT(newzip.IsOpen(), true);

  SAPI_ASSERT_OK_AND_ASSIGN(auto nameddata, newzip.ReadFile(name));
  ASSERT_EQ(nameddata, newdata);

  SAPI_ASSERT_OK_AND_ASSIGN(auto indexeddata, newzip.ReadFile(index));
  ASSERT_EQ(indexeddata, newdata);
}

TEST_P(ZipMultiFiles, ReplaceFileFdStore) {
  LibZip zip(sandbox_.get(), test_path_zip_, 0);
  ASSERT_THAT(zip.IsOpen(), true);

  uint64_t index = GetParam().first;
  std::string name = GetParam().second;

  SAPI_ASSERT_OK_AND_ASSIGN(auto newdata,
                            ReadFile(GetTestFilePath("notinzip")));
  SAPI_ASSERT_OK_AND_ASSIGN(auto zipdata, zip.ReadFile(index));
  ASSERT_NE(zipdata, newdata);

  int fd = open(GetTestFilePath("notinzip").c_str(), O_RDONLY);
  ASSERT_GE(fd, 0);

  ASSERT_THAT(zip.ReplaceFile(index, fd), IsOk());

  std::string new_zip_file_name = GetTemporaryFile("newzip.zip");
  int newfdzip = open(new_zip_file_name.c_str(), O_WRONLY);
  ASSERT_GE(newfdzip, 0);
  ASSERT_THAT(zip.Finish(), IsOk());
  ASSERT_THAT(zip.Save(newfdzip), IsOk());
  close(newfdzip);

  LibZip newzip(sandbox_.get(), new_zip_file_name, 0);
  ASSERT_THAT(newzip.IsOpen(), true);

  SAPI_ASSERT_OK_AND_ASSIGN(auto nameddata, newzip.ReadFile(name));
  ASSERT_EQ(nameddata, newdata);

  SAPI_ASSERT_OK_AND_ASSIGN(auto indexeddata, newzip.ReadFile(index));
  ASSERT_EQ(indexeddata, newdata);
}

TEST_P(ZipMultiFiles, DeleteFileStore) {
  LibZip zip(sandbox_.get(), test_path_zip_, 0);
  ASSERT_THAT(zip.IsOpen(), true);

  uint64_t index = GetParam().first;
  std::string origname = GetParam().second;

  SAPI_ASSERT_OK_AND_ASSIGN(uint64_t oldcount, zip.GetNumberEntries());

  ASSERT_THAT(zip.DeleteFile(index), IsOk());

  std::string new_zip_file_name = GetTemporaryFile("newzip.zip");
  int newfdzip = open(new_zip_file_name.c_str(), O_WRONLY);
  ASSERT_GE(newfdzip, 0);
  ASSERT_THAT(zip.Finish(), IsOk());
  ASSERT_THAT(zip.Save(newfdzip), IsOk());
  close(newfdzip);

  LibZip newzip(sandbox_.get(), new_zip_file_name, 0);
  ASSERT_THAT(newzip.IsOpen(), true);

  SAPI_ASSERT_OK_AND_ASSIGN(uint64_t newcount, newzip.GetNumberEntries());
  ASSERT_LT(newcount, oldcount);

  for (uint64_t i = 0; i < newcount; i++) {
    absl::StatusOr<std::string> name = newzip.GetName(i);
    ASSERT_THAT(name, IsOk());
    ASSERT_NE(*name, origname);
  }
}

INSTANTIATE_TEST_SUITE_P(ZipBase, ZipMultiFiles,
                         testing::Values(std::make_pair(0, "binary"),
                                         std::make_pair(1, "text")));
}  // namespace
