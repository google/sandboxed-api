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

#ifndef CONTRIB_URIPARSER_UTILS_UTILS_ZIP_H_
#define CONTRIB_URIPARSER_UTILS_UTILS_ZIP_H_

#include <vector>

#include "absl/container/btree_map.h"
#include "absl/log/die_if_null.h"
#include "contrib/uriparser/sandboxed.h"
#include "sandboxed_api/util/status_macros.h"

class UriParser {
 public:
  UriParser(UriparserSandbox* sandbox, const std::string& uri)
      : sandbox_(ABSL_DIE_IF_NULL(sandbox)),
        api_(sandbox_),
        c_uri_(uri.c_str()) {
    status_ = ParseUri();
  }

  ~UriParser();

  absl::Status GetStatus();
  absl::Status NormalizeSyntax();
  absl::Status NormalizeSyntax(int norm_mask);

  absl::StatusOr<std::string> GetUri();
  absl::StatusOr<std::string> GetUriWithBase(const std::string& base);
  absl::StatusOr<std::string> GetUriWithoutBase(const std::string& base,
                                                bool domain_root_mode);
  absl::StatusOr<std::string> GetScheme();
  absl::StatusOr<std::string> GetUserInfo();
  absl::StatusOr<std::string> GetHostText();
  absl::StatusOr<std::string> GetHostIP();
  absl::StatusOr<std::string> GetPortText();
  absl::StatusOr<std::string> GetQuery();
  absl::StatusOr<std::string> GetFragment();
  absl::StatusOr<std::string> GetUriEscaped(bool space_to_plus,
                                            bool normalize_breaks);
  absl::StatusOr<std::vector<std::string>> GetPath();
  absl::StatusOr<absl::btree_map<std::string, std::string>> GetQueryElements();

 protected:
  absl::StatusOr<std::string> FetchUriText(UriTextRangeA* ptr);
  absl::StatusOr<std::string> GetUri(sapi::v::Struct<UriUriA>* uri);
  absl::Status ParseUri();
  sapi::v::Struct<UriUriA> uri_;

 private:
  UriparserSandbox* sandbox_;
  UriparserApi api_;
  sapi::v::ConstCStr c_uri_;  // We have to keep a orginal string in sandbox
  absl::Status status_;
};

#endif  // CONTRIB_URIARSER_UTILS_UTILS_ZIP_H_
