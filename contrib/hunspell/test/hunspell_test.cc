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

#include "../sandboxed.h"
#include "sandboxed_api/util/path.h"
#include "sandboxed_api/util/status_matchers.h"
#include "sandboxed_api/util/temp_file.h"

namespace {

using ::sapi::IsOk;

class HunspellTest : public ::testing::Test {
 protected:
  static constexpr absl::string_view kEncoding = "UTF-8";
  static constexpr absl::string_view kAffixFileName = "utf8.aff";
  static constexpr absl::string_view kDictionaryFileName = "utf8.dic";

  static constexpr absl::string_view kGoodFileName = "utf8.good";
  static constexpr absl::string_view kWrongFileName = "utf8.wrong";

  static constexpr absl::string_view kSuggestion = "fo";
  static constexpr absl::string_view kRandomWord = "random_word123";

  void SetUp() override {
    test_files_dir_ = getenv("TEST_FILES_DIR");
    ASSERT_NE(test_files_dir_, nullptr);

    std::string s_afn = GetTestFilePath(kAffixFileName);
    std::string s_dfn = GetTestFilePath(kDictionaryFileName);
    sapi::v::ConstCStr c_afn(s_afn.c_str());
    sapi::v::ConstCStr c_dfn(s_dfn.c_str());

    sandbox_ = std::make_unique<HunspellSapiSandbox>(s_afn, s_dfn);
    ASSERT_THAT(sandbox_->Init(), IsOk());

    api_ = std::make_unique<HunspellApi>(sandbox_.get());

    SAPI_ASSERT_OK_AND_ASSIGN(
        Hunhandle * hunspell,
        api_->Hunspell_create(c_afn.PtrBefore(), c_dfn.PtrBefore()));
    hunspellrp_ = std::make_unique<sapi::v::RemotePtr>(hunspell);
  }

  void TearDown() override {
    absl::Status status = api_->Hunspell_destroy(&(*hunspellrp_));
    ASSERT_THAT(status, IsOk());
  }

  std::string GetTestFilePath(const absl::string_view& filename) {
    return sapi::file::JoinPath(test_files_dir_, filename);
  }

  std::unique_ptr<HunspellSapiSandbox> sandbox_;
  std::unique_ptr<HunspellApi> api_;
  std::unique_ptr<sapi::v::RemotePtr> hunspellrp_;

 private:
  const char* test_files_dir_;
};

TEST_F(HunspellTest, CheckEncoding) {
  SAPI_ASSERT_OK_AND_ASSIGN(char* ret,
                            api_->Hunspell_get_dic_encoding(&(*hunspellrp_)));
  SAPI_ASSERT_OK_AND_ASSIGN(
      std::string encoding,
      api_->GetSandbox()->GetCString(sapi::v::RemotePtr(ret)));
  EXPECT_EQ(encoding, kEncoding);
}

TEST_F(HunspellTest, CheckGoodSpell) {
  SAPI_ASSERT_OK_AND_ASSIGN(char* ret,
                            api_->Hunspell_get_dic_encoding(&(*hunspellrp_)));
  std::ifstream wtclst(GetTestFilePath(kGoodFileName), std::ios_base::in);
  ASSERT_TRUE(wtclst.is_open());

  std::string buf;
  while (std::getline(wtclst, buf)) {
    sapi::v::ConstCStr cbuf(buf.c_str());
    SAPI_ASSERT_OK_AND_ASSIGN(
        int result, api_->Hunspell_spell(&(*hunspellrp_), cbuf.PtrBefore()));
    ASSERT_EQ(result, 1);
  }
}

TEST_F(HunspellTest, CheckWrongSpell) {
  SAPI_ASSERT_OK_AND_ASSIGN(char* ret,
                            api_->Hunspell_get_dic_encoding(&(*hunspellrp_)));
  std::ifstream wtclst(GetTestFilePath(kWrongFileName), std::ios_base::in);
  ASSERT_TRUE(wtclst.is_open());

  std::string buf;
  while (std::getline(wtclst, buf)) {
    sapi::v::ConstCStr cbuf(buf.c_str());
    SAPI_ASSERT_OK_AND_ASSIGN(
        int result, api_->Hunspell_spell(&(*hunspellrp_), cbuf.PtrBefore()));
    ASSERT_EQ(result, 0);
  }
}

TEST_F(HunspellTest, CheckAddToDict) {
  sapi::v::ConstCStr cbuf(kRandomWord.data());

  int result;
  SAPI_ASSERT_OK_AND_ASSIGN(
      result, api_->Hunspell_spell(&(*hunspellrp_), cbuf.PtrBefore()));
  ASSERT_EQ(result, 0);

  SAPI_ASSERT_OK_AND_ASSIGN(
      result, api_->Hunspell_add(&(*hunspellrp_), cbuf.PtrBefore()));
  ASSERT_EQ(result, 0);

  SAPI_ASSERT_OK_AND_ASSIGN(
      result, api_->Hunspell_spell(&(*hunspellrp_), cbuf.PtrBefore()));
  ASSERT_EQ(result, 1);

  SAPI_ASSERT_OK_AND_ASSIGN(
      result, api_->Hunspell_remove(&(*hunspellrp_), cbuf.PtrBefore()));
  ASSERT_EQ(result, 0);

  SAPI_ASSERT_OK_AND_ASSIGN(
      result, api_->Hunspell_spell(&(*hunspellrp_), cbuf.PtrBefore()));
  ASSERT_EQ(result, 0);
}

TEST_F(HunspellTest, CheckSuggestion) {
  sapi::v::ConstCStr cbuf(kSuggestion.data());

  SAPI_ASSERT_OK_AND_ASSIGN(
      int result, api_->Hunspell_spell(&(*hunspellrp_), cbuf.PtrBefore()));
  ASSERT_EQ(result, 0);

  sapi::v::GenericPtr outptr;
  SAPI_ASSERT_OK_AND_ASSIGN(
      int nlist, api_->Hunspell_suggest(&(*hunspellrp_), outptr.PtrAfter(),
                                        cbuf.PtrBefore()));
  ASSERT_GT(nlist, 0);
}

}  // namespace
