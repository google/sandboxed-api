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

#include <fcntl.h>

#include <fstream>

#include "contrib/brotli/sandboxed.h"
#include "contrib/brotli/utils/utils_brotli.h"
#include "contrib/brotli/utils/utils_brotli_dec.h"
#include "contrib/brotli/utils/utils_brotli_enc.h"
#include "sandboxed_api/util/path.h"
#include "sandboxed_api/util/status_matchers.h"

namespace {

using ::sapi::IsOk;

class BrotliBase : public testing::Test {
 protected:
  std::string GetTestFilePath(const std::string& filename) {
    return sapi::file::JoinPath(test_dir_, filename);
  }

  void SetUp() override;

  std::unique_ptr<BrotliSandbox> sandbox_;
  std::unique_ptr<BrotliEncoder> enc_;
  std::unique_ptr<BrotliDecoder> dec_;
  const char* test_dir_;
};

class BrotliMultiFile : public BrotliBase,
                        public testing::WithParamInterface<std::string> {};

void BrotliBase::SetUp() {
  sandbox_ = std::make_unique<BrotliSapiSandbox>();
  ASSERT_THAT(sandbox_.get()->Init(), IsOk());

  enc_ = std::make_unique<BrotliEncoder>(sandbox_.get());
  ASSERT_TRUE(enc_.get()->IsInit());

  dec_ = std::make_unique<BrotliDecoder>(sandbox_.get());
  ASSERT_TRUE(dec_.get()->IsInit());

  test_dir_ = getenv("TEST_FILES_DIR");
  ASSERT_NE(test_dir_, nullptr);
}

TEST_F(BrotliBase, SetParamEnc) {
  ASSERT_THAT(enc_.get()->SetParameter(BROTLI_PARAM_QUALITY, 5), IsOk());
}

TEST_F(BrotliBase, SetParamDec) {
  ASSERT_THAT(dec_.get()->SetParameter(
                  BROTLI_DECODER_PARAM_DISABLE_RING_BUFFER_REALLOCATION, 100),
              IsOk());
}

TEST_F(BrotliBase, Compress) {
  SAPI_ASSERT_OK_AND_ASSIGN(std::vector<uint8_t> bufin,
                            ReadFile(GetTestFilePath("text")));

  ASSERT_THAT(enc_.get()->Compress(bufin), IsOk());

  SAPI_ASSERT_OK_AND_ASSIGN(std::vector<uint8_t> bufout,
                            enc_.get()->TakeOutput());
  ASSERT_LT(bufout.size(), bufin.size());
}

TEST_F(BrotliBase, CompressDecompress) {
  SAPI_ASSERT_OK_AND_ASSIGN(std::vector<uint8_t> bufin,
                            ReadFile(GetTestFilePath("text")));

  ASSERT_THAT(enc_.get()->Compress(bufin), IsOk());

  SAPI_ASSERT_OK_AND_ASSIGN(std::vector<uint8_t> bufcomp,
                            enc_.get()->TakeOutput());
  ASSERT_LT(bufcomp.size(), bufin.size());

  SAPI_ASSERT_OK_AND_ASSIGN(BrotliDecoderResult result,
                            dec_.get()->Decompress(bufcomp));
  ASSERT_THAT(result, BROTLI_DECODER_RESULT_SUCCESS);

  SAPI_ASSERT_OK_AND_ASSIGN(std::vector<uint8_t> bufout,
                            dec_.get()->TakeOutput());
  ASSERT_EQ(bufin, bufout);
}

TEST_F(BrotliBase, CompressStreamDecompress) {
  SAPI_ASSERT_OK_AND_ASSIGN(std::vector<uint8_t> buforig,
                            ReadFile(GetTestFilePath("text")));

  for (auto it = buforig.begin(); it != buforig.end();) {
    int nsize = std::min<size_t>(512, buforig.end() - it);
    BrotliEncoderOperation op = BROTLI_OPERATION_PROCESS;
    if (it + nsize == buforig.end()) {
      op = BROTLI_OPERATION_FINISH;
    }
    std::vector<uint8_t> bufin(it, it + nsize);

    ASSERT_THAT(enc_.get()->Compress(bufin, op), IsOk());
    it += nsize;
  }

  bool empty = false;
  std::vector<uint8_t> bufcomp;
  while (!empty) {
    SAPI_ASSERT_OK_AND_ASSIGN(std::vector<uint8_t> takebuf,
                              enc_.get()->TakeOutput());
    empty = takebuf.empty();
    if (!empty) {
      bufcomp.insert(bufcomp.end(), takebuf.begin(), takebuf.end());
    }
  }

  SAPI_ASSERT_OK_AND_ASSIGN(BrotliDecoderResult result,
                            dec_.get()->Decompress(bufcomp));
  ASSERT_THAT(result, BROTLI_DECODER_RESULT_SUCCESS);

  SAPI_ASSERT_OK_AND_ASSIGN(std::vector<uint8_t> bufout,
                            dec_.get()->TakeOutput());
  ASSERT_EQ(buforig, bufout);
}

TEST_P(BrotliMultiFile, Decompress) {
  SAPI_ASSERT_OK_AND_ASSIGN(std::vector<uint8_t> buforig,
                            ReadFile(GetTestFilePath("text")));
  SAPI_ASSERT_OK_AND_ASSIGN(std::vector<uint8_t> bufin,
                            ReadFile(GetTestFilePath(GetParam())));

  SAPI_ASSERT_OK_AND_ASSIGN(BrotliDecoderResult result,
                            dec_.get()->Decompress(bufin));
  ASSERT_THAT(result, BROTLI_DECODER_RESULT_SUCCESS);

  SAPI_ASSERT_OK_AND_ASSIGN(std::vector<uint8_t> bufout,
                            dec_.get()->TakeOutput());
  ASSERT_EQ(buforig, bufout);
}

TEST_P(BrotliMultiFile, DecompressCharStream) {
  SAPI_ASSERT_OK_AND_ASSIGN(std::vector<uint8_t> buforig,
                            ReadFile(GetTestFilePath("text")));
  SAPI_ASSERT_OK_AND_ASSIGN(std::vector<uint8_t> bufcomp,
                            ReadFile(GetTestFilePath(GetParam())));

  BrotliDecoderResult result;
  std::vector<uint8_t> bufout;

  for (auto it = bufcomp.begin(); it != bufcomp.end(); ++it) {
    std::vector<uint8_t> tmp(it, it + 1);

    SAPI_ASSERT_OK_AND_ASSIGN(result, dec_.get()->Decompress(tmp));
    if (result == BROTLI_DECODER_RESULT_SUCCESS) {
      SAPI_ASSERT_OK_AND_ASSIGN(std::vector<uint8_t> bufuncomptmp,
                                dec_.get()->TakeOutput());
      bufout.insert(bufout.end(), bufuncomptmp.begin(), bufuncomptmp.end());
    }
  }
  ASSERT_THAT(result, BROTLI_DECODER_RESULT_SUCCESS);

  ASSERT_EQ(buforig, bufout);
}

TEST_P(BrotliMultiFile, DecompressChunksStream) {
  SAPI_ASSERT_OK_AND_ASSIGN(std::vector<uint8_t> buforig,
                            ReadFile(GetTestFilePath("text")));
  SAPI_ASSERT_OK_AND_ASSIGN(std::vector<uint8_t> bufcomp,
                            ReadFile(GetTestFilePath(GetParam())));

  std::vector<size_t> chunks = {128, 256, 13, 37, 99, 10, 42};

  BrotliDecoderResult result;
  std::vector<uint8_t> bufout;
  std::vector<uint8_t>::iterator it = bufcomp.begin();

  for (int i = 0; it != bufcomp.end(); ++i) {
    size_t nsize =
        std::min<size_t>(bufcomp.end() - it, chunks[i % chunks.size()]);
    std::vector<uint8_t> tmp(it, it + nsize);
    it += nsize;

    SAPI_ASSERT_OK_AND_ASSIGN(result, dec_.get()->Decompress(tmp));
    if (result == BROTLI_DECODER_RESULT_SUCCESS) {
      SAPI_ASSERT_OK_AND_ASSIGN(std::vector<uint8_t> bufuncomptmp,
                                dec_.get()->TakeOutput());
      bufout.insert(bufout.end(), bufuncomptmp.begin(), bufuncomptmp.end());
    }
  }

  ASSERT_THAT(result, BROTLI_DECODER_RESULT_SUCCESS);
  ASSERT_EQ(buforig, bufout);
}

INSTANTIATE_TEST_SUITE_P(BrotliBase, BrotliMultiFile,
                         testing::Values("text.full.brotli",
                                         "text.chunk.brotli"));
}  // namespace
