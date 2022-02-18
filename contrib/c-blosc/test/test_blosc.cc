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

#include "contrib/c-blosc/sandboxed.h"
#include "contrib/c-blosc/utils/utils_blosc.h"
#include "sandboxed_api/util/path.h"
#include "sandboxed_api/util/status_matchers.h"
#include "sandboxed_api/util/temp_file.h"

namespace {

using ::sapi::IsOk;

constexpr size_t kDefaultBlockSize = 19059;

bool CompareFiles(const std::string& name1, const std::string& name2) {
  std::ifstream f1(name1, std::ios::binary);
  if (!f1.is_open()) {
    return false;
  }

  std::ifstream f2(name2, std::ios::binary);
  if (!f2.is_open()) {
    return false;
  }

  while (!f1.eof() && !f2.eof()) {
    char buf1[128];
    char buf2[128];

    f1.read(buf1, sizeof(buf1));
    f2.read(buf2, sizeof(buf2));

    if (f1.gcount() != f2.gcount()) {
      return false;
    }
    if (memcmp(&buf1, &buf2, f2.gcount()) != 0) {
      return false;
    }
  }

  return f1.eof() == f2.eof();
}

std::string GetTestFilePath(const std::string& filename) {
  return sapi::file::JoinPath(getenv("TEST_FILES_DIR"), filename);
}

std::string GetTemporaryFile(const std::string& filename) {
  absl::StatusOr<std::string> tmp_file =
      sapi::CreateNamedTempFileAndClose(filename);
  if (!tmp_file.ok()) {
    return "";
  }

  return sapi::file::JoinPath(sapi::file_util::fileops::GetCWD(), *tmp_file);
}

std::streamsize GetStreamSize(std::ifstream& stream) {
  stream.seekg(0, std::ios_base::end);
  std::streamsize ssize = stream.tellg();
  stream.seekg(0, std::ios_base::beg);

  return ssize;
}

class TestText : public testing::TestWithParam<std::string> {};

TEST(SandboxTest, CheckInit) {
  CbloscSapiSandbox sandbox;
  ASSERT_THAT(sandbox.Init(), IsOk()) << "Couldn't initialize Sandboxed API";
  CbloscApi api = CbloscApi(&sandbox);

  ASSERT_THAT(api.blosc_init(), IsOk());
}

TEST(SandboxTest, CheckDestroy) {
  CbloscSapiSandbox sandbox;
  ASSERT_THAT(sandbox.Init(), IsOk()) << "Couldn't initialize Sandboxed API";
  CbloscApi api = CbloscApi(&sandbox);

  ASSERT_THAT(api.blosc_init(), IsOk());
  ASSERT_THAT(api.blosc_destroy(), IsOk());
}

TEST(SandboxTest, CheckGetNThreads) {
  CbloscSapiSandbox sandbox;
  ASSERT_THAT(sandbox.Init(), IsOk()) << "Couldn't initialize Sandboxed API";
  CbloscApi api = CbloscApi(&sandbox);

  SAPI_ASSERT_OK_AND_ASSIGN(int nthreads, api.blosc_get_nthreads());

  ASSERT_GT(nthreads, 0);
}

TEST(SandboxTest, CheckSetNThreads) {
  CbloscSapiSandbox sandbox;
  ASSERT_THAT(sandbox.Init(), IsOk()) << "Couldn't initialize Sandboxed API";
  CbloscApi api = CbloscApi(&sandbox);

  int nthreads;
  SAPI_ASSERT_OK_AND_ASSIGN(nthreads, api.blosc_get_nthreads());
  ASSERT_NE(nthreads, 3);
  ASSERT_THAT(api.blosc_set_nthreads(3), IsOk());
  SAPI_ASSERT_OK_AND_ASSIGN(nthreads, api.blosc_get_nthreads());
  ASSERT_EQ(nthreads, 3);
}

TEST(SandboxTest, CheckGetBlocksize) {
  CbloscSapiSandbox sandbox;
  ASSERT_THAT(sandbox.Init(), IsOk()) << "Couldn't initialize Sandboxed API";
  CbloscApi api = CbloscApi(&sandbox);

  SAPI_ASSERT_OK_AND_ASSIGN(size_t blocksize, api.blosc_get_blocksize());
  ASSERT_NE(blocksize, kDefaultBlockSize);
}

TEST(SandboxTest, CheckSetBlocksize) {
  CbloscSapiSandbox sandbox;
  ASSERT_THAT(sandbox.Init(), IsOk()) << "Couldn't initialize Sandboxed API";
  CbloscApi api = CbloscApi(&sandbox);

  size_t blocksize;
  SAPI_ASSERT_OK_AND_ASSIGN(blocksize, api.blosc_get_blocksize());
  ASSERT_NE(blocksize, 1337);
  ASSERT_THAT(api.blosc_set_blocksize(1337), IsOk());
  SAPI_ASSERT_OK_AND_ASSIGN(blocksize, api.blosc_get_blocksize());
  ASSERT_EQ(blocksize, 1337);
}

TEST_P(TestText, CheckSizes) {
  absl::Status status;
  CbloscSapiSandbox sandbox;
  ASSERT_THAT(sandbox.Init(), IsOk()) << "Couldn't initialize Sandboxed API";
  CbloscApi api = CbloscApi(&sandbox);

  std::string compressor(GetParam());

  std::string origfile_s = GetTestFilePath("text");
  std::string infile_s = GetTestFilePath(absl::StrCat("text.", compressor));

  std::ifstream origfile(origfile_s, std::ios::binary);
  ASSERT_TRUE(origfile.is_open());
  ssize_t origsize = GetStreamSize(origfile);

  std::ifstream infile(infile_s, std::ios::binary);
  ASSERT_TRUE(infile.is_open());

  std::streamsize insize = GetStreamSize(infile);
  sapi::v::Array<uint8_t> inbuf(insize);
  infile.read(reinterpret_cast<char*>(inbuf.GetData()), insize);

  sapi::v::IntBase<size_t> nbytes;
  sapi::v::IntBase<size_t> cbytes;
  sapi::v::IntBase<size_t> blocksize;

  ASSERT_THAT(api.blosc_cbuffer_sizes(inbuf.PtrBefore(), nbytes.PtrAfter(),
                                      cbytes.PtrAfter(), blocksize.PtrAfter()),
              IsOk());

  ASSERT_EQ(nbytes.GetValue(), origsize);
  ASSERT_EQ(cbytes.GetValue(), insize);
  ASSERT_EQ(blocksize.GetValue(), kDefaultBlockSize);
}

TEST_P(TestText, CheckValidate) {
  absl::Status status;
  CbloscSapiSandbox sandbox;
  ASSERT_THAT(sandbox.Init(), IsOk()) << "Couldn't initialize Sandboxed API";
  CbloscApi api = CbloscApi(&sandbox);

  std::string compressor(GetParam());

  std::string origfile_s = GetTestFilePath("text");
  std::string infile_s = GetTestFilePath(absl::StrCat("text.", compressor));

  std::ifstream origfile(origfile_s, std::ios::binary);
  ASSERT_TRUE(origfile.is_open());
  ssize_t origsize = GetStreamSize(origfile);

  std::ifstream infile(infile_s, std::ios::binary);
  ASSERT_TRUE(infile.is_open());

  std::streamsize insize = GetStreamSize(infile);
  sapi::v::Array<uint8_t> inbuf(insize);
  infile.read(reinterpret_cast<char*>(inbuf.GetData()), insize);

  sapi::v::IntBase<size_t> nbytes;

  SAPI_ASSERT_OK_AND_ASSIGN(
      int ret, api.blosc_cbuffer_validate(inbuf.PtrBefore(), inbuf.GetSize(),
                                          nbytes.PtrAfter()));

  ASSERT_GE(ret, 0);
  ASSERT_EQ(nbytes.GetValue(), origsize);
}

TEST_P(TestText, SetCompress) {
  CbloscSapiSandbox sandbox;
  ASSERT_THAT(sandbox.Init(), IsOk()) << "Couldn't initialize Sandboxed API";
  CbloscApi api = CbloscApi(&sandbox);

  std::string compressor(GetParam());
  SAPI_ASSERT_OK_AND_ASSIGN(
      int ret, api.blosc_set_compressor(
                   sapi::v::ConstCStr(compressor.c_str()).PtrBefore()));
  ASSERT_GE(ret, 0);

  SAPI_ASSERT_OK_AND_ASSIGN(char* c_compressor_ret, api.blosc_get_compressor());
  SAPI_ASSERT_OK_AND_ASSIGN(
      std::string compressor_ret,
      api.GetSandbox()->GetCString(sapi::v::RemotePtr(c_compressor_ret)));

  EXPECT_EQ(compressor_ret, compressor);
}

TEST_P(TestText, Compress) {
  CbloscSapiSandbox sandbox;
  ASSERT_THAT(sandbox.Init(), IsOk()) << "Couldn't initialize Sandboxed API";
  CbloscApi api = CbloscApi(&sandbox);

  std::string compressor(GetParam());

  std::string infile_s = GetTestFilePath("text");
  std::string outfile_s = GetTemporaryFile(absl::StrCat("out", compressor));
  ASSERT_FALSE(outfile_s.empty());

  std::ifstream infile(infile_s, std::ios::binary);
  ASSERT_TRUE(infile.is_open());

  std::ofstream outfile(outfile_s, std::ios::binary);
  ASSERT_TRUE(outfile.is_open());

  absl::Status status = Compress(api, infile, outfile, 5, compressor, 5);
  ASSERT_THAT(status, IsOk()) << "Unable to compress file";

  ASSERT_LT(outfile.tellp(), infile.tellg());
}

TEST_P(TestText, Decompress) {
  absl::Status status;
  CbloscSapiSandbox sandbox;
  ASSERT_THAT(sandbox.Init(), IsOk()) << "Couldn't initialize Sandboxed API";
  CbloscApi api = CbloscApi(&sandbox);

  std::string compressor(GetParam());

  std::string origfile_s = GetTestFilePath("text");
  std::string infile_s = GetTestFilePath(absl::StrCat("text.", compressor));
  std::string outfile_s = GetTemporaryFile(absl::StrCat("middle", compressor));
  ASSERT_FALSE(outfile_s.empty());

  std::ifstream infile(infile_s, std::ios::binary);
  ASSERT_TRUE(infile.is_open());

  std::ofstream outfile(outfile_s, std::ios::binary);
  ASSERT_TRUE(outfile.is_open());

  status = Decompress(api, infile, outfile, 5);
  ASSERT_THAT(status, IsOk()) << "Unable to compress file";

  ASSERT_GT(outfile.tellp(), infile.tellg());

  ASSERT_TRUE(CompareFiles(origfile_s, outfile_s));
}

TEST_P(TestText, CompressDecompress) {
  absl::Status status;
  CbloscSapiSandbox sandbox;
  ASSERT_THAT(sandbox.Init(), IsOk()) << "Couldn't initialize Sandboxed API";
  CbloscApi api = CbloscApi(&sandbox);

  std::string compressor(GetParam());

  std::string infile_s = GetTestFilePath("text");
  std::string middlefile_s =
      GetTemporaryFile(absl::StrCat("middle", compressor));
  ASSERT_FALSE(middlefile_s.empty());

  std::ifstream infile(infile_s, std::ios::binary);
  ASSERT_TRUE(infile.is_open());

  std::ofstream outmiddlefile(middlefile_s, std::ios::binary);
  ASSERT_TRUE(outmiddlefile.is_open());

  status = Compress(api, infile, outmiddlefile, 5, compressor, 5);
  ASSERT_THAT(status, IsOk()) << "Unable to compress file";

  ASSERT_LT(outmiddlefile.tellp(), infile.tellg());

  std::string outfile_s = GetTemporaryFile(absl::StrCat("out", compressor));
  ASSERT_FALSE(outfile_s.empty());

  std::ifstream inmiddlefile(middlefile_s, std::ios::binary);
  ASSERT_TRUE(inmiddlefile.is_open());

  std::ofstream outfile(outfile_s, std::ios::binary);
  ASSERT_TRUE(outfile.is_open());

  status = Decompress(api, inmiddlefile, outfile, 5);
  ASSERT_THAT(status, IsOk()) << "Unable to compress file";

  ASSERT_GT(outfile.tellp(), inmiddlefile.tellg());

  ASSERT_TRUE(CompareFiles(infile_s, outfile_s));
}

INSTANTIATE_TEST_SUITE_P(SandboxTest, TestText,
                         testing::Values("blosclz", "lz4", "lz4hc", "zlib",
                                         "zstd"));

}  // namespace
