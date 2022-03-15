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

#include "contrib/uriparser/sandboxed.h"
#include "contrib/uriparser/utils/utils_uriparser.h"
#include "sandboxed_api/util/path.h"
#include "sandboxed_api/util/status_matchers.h"
#include "sandboxed_api/util/temp_file.h"

namespace {

using ::sapi::IsOk;

const struct TestVariant {
  std::string test;
  std::string uri;
  std::string uriescaped;
  std::string scheme;
  std::string userinfo;
  std::string hosttext;
  std::string hostip;
  std::string porttext;
  std::string query;
  std::string fragment;
  std::string normalized;
  std::string add_base_example;
  std::string remove_base_example;
  std::vector<std::string> path_elements;
  std::map<std::string, std::string> query_elements;
} TestData[] = {
    {
        .test = "http://www.example.com/",
        .uri = "http://www.example.com/",
        .uriescaped = "http%3A%2F%2Fwww.example.com%2F",
        .scheme = "http",
        .userinfo = "",
        .hosttext = "www.example.com",
        .hostip = "",
        .porttext = "",
        .query = "",
        .fragment = "",
        .normalized = "http://www.example.com/",
        .add_base_example = "http://www.example.com/",
        .remove_base_example = "./",
    },
    {
        .test = "https://github.com/google/sandboxed-api/",
        .uri = "https://github.com/google/sandboxed-api/",
        .uriescaped = "https%3A%2F%2Fgithub.com%2Fgoogle%2Fsandboxed-api%2F",
        .scheme = "https",
        .userinfo = "",
        .hosttext = "github.com",
        .hostip = "",
        .porttext = "",
        .query = "",
        .fragment = "",
        .normalized = "https://github.com/google/sandboxed-api/",
        .add_base_example = "https://github.com/google/sandboxed-api/",
        .remove_base_example = "https://github.com/google/sandboxed-api/",
        .path_elements = {"google", "sandboxed-api"},
    },
    {
        .test = "mailto:test@example.com",
        .uri = "mailto:test@example.com",
        .uriescaped = "mailto%3Atest%40example.com",
        .scheme = "mailto",
        .userinfo = "",
        .hosttext = "",
        .hostip = "",
        .porttext = "",
        .query = "",
        .fragment = "",
        .normalized = "mailto:test@example.com",
        .add_base_example = "mailto:test@example.com",
        .remove_base_example = "mailto:test@example.com",
        .path_elements = {"test@example.com"},
    },
    {
        .test = "file:///bin/bash",
        .uri = "file:///bin/bash",
        .uriescaped = "file%3A%2F%2F%2Fbin%2Fbash",
        .scheme = "file",
        .userinfo = "",
        .hosttext = "",
        .hostip = "",
        .porttext = "",
        .query = "",
        .fragment = "",
        .normalized = "file:///bin/bash",
        .add_base_example = "file:///bin/bash",
        .remove_base_example = "file:///bin/bash",
        .path_elements =
            {
                "bin",
                "bash",
            },
    },
    {
        .test = "http://www.example.com/name%20with%20spaces/",
        .uri = "http://www.example.com/name%20with%20spaces/",
        .uriescaped =
            "http%3A%2F%2Fwww.example.com%2Fname%2520with%2520spaces%2F",
        .scheme = "http",
        .userinfo = "",
        .hosttext = "www.example.com",
        .hostip = "",
        .porttext = "",
        .query = "",
        .fragment = "",
        .normalized = "http://www.example.com/name%20with%20spaces/",
        .add_base_example = "http://www.example.com/name%20with%20spaces/",
        .remove_base_example = "name%20with%20spaces/",
        .path_elements =
            {
                "name%20with%20spaces",
            },
    },
    {
        .test = "http://abcdefg@localhost/",
        .uri = "http://abcdefg@localhost/",
        .uriescaped = "http%3A%2F%2Fabcdefg%40localhost%2F",
        .scheme = "http",
        .userinfo = "abcdefg",
        .hosttext = "localhost",
        .hostip = "",
        .porttext = "",
        .query = "",
        .fragment = "",
        .normalized = "http://abcdefg@localhost/",
        .add_base_example = "http://abcdefg@localhost/",
        .remove_base_example = "//abcdefg@localhost/",
    },
    {
        .test = "https://localhost:123/",
        .uri = "https://localhost:123/",
        .uriescaped = "https%3A%2F%2Flocalhost%3A123%2F",
        .scheme = "https",
        .userinfo = "",
        .hosttext = "localhost",
        .hostip = "",
        .porttext = "123",
        .query = "",
        .fragment = "",
        .normalized = "https://localhost:123/",
        .add_base_example = "https://localhost:123/",
        .remove_base_example = "https://localhost:123/",
    },
    {
        .test = "http://[::1]/",
        .uri = "http://[0000:0000:0000:0000:0000:0000:0000:0001]/",
        .uriescaped = "http%3A%2F%2F%5B0000%3A0000%3A0000%3A0000%3A0000%3A0000%"
                      "3A0000%3A0001%5D%2F",
        .scheme = "http",
        .userinfo = "",
        .hosttext = "::1",
        .hostip = "::1",
        .porttext = "",
        .query = "",
        .fragment = "",
        .normalized = "http://[0000:0000:0000:0000:0000:0000:0000:0001]/",
        .add_base_example = "http://[0000:0000:0000:0000:0000:0000:0000:0001]/",
        .remove_base_example = "//[0000:0000:0000:0000:0000:0000:0000:0001]/",
    },
    {
        .test = "http://a/b/c/d;p?q",
        .uri = "http://a/b/c/d;p?q",
        .uriescaped = "http%3A%2F%2Fa%2Fb%2Fc%2Fd%3Bp%3Fq",
        .scheme = "http",
        .userinfo = "",
        .hosttext = "a",
        .hostip = "",
        .porttext = "",
        .query = "q",
        .fragment = "",
        .normalized = "http://a/b/c/d;p?q",
        .add_base_example = "http://a/b/c/d;p?q",
        .remove_base_example = "//a/b/c/d;p?q",
        .path_elements = {"b", "c", "d;p"},
        .query_elements = {{"q", ""}},
    },
    {.test = "http://a/b/c/../d;p?q",
     .uri = "http://a/b/c/../d;p?q",
     .uriescaped = "http%3A%2F%2Fa%2Fb%2Fc%2F..%2Fd%3Bp%3Fq",
     .scheme = "http",
     .userinfo = "",
     .hosttext = "a",
     .hostip = "",
     .porttext = "",
     .query = "q",
     .fragment = "",
     .normalized = "http://a/b/d;p?q",
     .add_base_example = "http://a/b/d;p?q",
     .remove_base_example = "//a/b/c/../d;p?q",
     .path_elements = {"b", "c", "..", "d;p"},
     .query_elements = {{"q", ""}}},
    {
        .test = "http://example.com/abc/def/",
        .uri = "http://example.com/abc/def/",
        .uriescaped = "http%3A%2F%2Fexample.com%2Fabc%2Fdef%2F",
        .scheme = "http",
        .userinfo = "",
        .hosttext = "example.com",
        .hostip = "",
        .porttext = "",
        .query = "",
        .fragment = "",
        .normalized = "http://example.com/abc/def/",
        .add_base_example = "http://example.com/abc/def/",
        .remove_base_example = "//example.com/abc/def/",
        .path_elements =
            {
                "abc",
                "def",
            },
    },
    {.test = "http://example.com/?abc",
     .uri = "http://example.com/?abc",
     .uriescaped = "http%3A%2F%2Fexample.com%2F%3Fabc",
     .scheme = "http",
     .userinfo = "",
     .hosttext = "example.com",
     .hostip = "",
     .porttext = "",
     .query = "abc",
     .fragment = "",
     .normalized = "http://example.com/?abc",
     .add_base_example = "http://example.com/?abc",
     .remove_base_example = "//example.com/?abc",
     .query_elements = {{"abc", ""}}},
    {
        .test = "http://[vA.123456]/",
        .uri = "http://[vA.123456]/",
        .uriescaped = "http%3A%2F%2F%5BvA.123456%5D%2F",
        .scheme = "http",
        .userinfo = "",
        .hosttext = "vA.123456",
        .hostip = "",
        .porttext = "",
        .query = "",
        .fragment = "",
        .normalized = "http://[va.123456]/",
        .add_base_example = "http://[vA.123456]/",
        .remove_base_example = "//[vA.123456]/",
    },
    {
        .test = "http://8.8.8.8/",
        .uri = "http://8.8.8.8/",
        .uriescaped = "http%3A%2F%2F8.8.8.8%2F",
        .scheme = "http",
        .userinfo = "",
        .hosttext = "8.8.8.8",
        .hostip = "8.8.8.8",
        .porttext = "",
        .query = "",
        .fragment = "",
        .normalized = "http://8.8.8.8/",
        .add_base_example = "http://8.8.8.8/",
        .remove_base_example = "//8.8.8.8/",
    },
    {.test = "http://www.example.com/?abc",
     .uri = "http://www.example.com/?abc",
     .uriescaped = "http%3A%2F%2Fwww.example.com%2F%3Fabc",
     .scheme = "http",
     .userinfo = "",
     .hosttext = "www.example.com",
     .hostip = "",
     .porttext = "",
     .query = "abc",
     .fragment = "",
     .normalized = "http://www.example.com/?abc",
     .add_base_example = "http://www.example.com/?abc",
     .remove_base_example = "./?abc",
     .query_elements = {{"abc", ""}}},
    {.test = "https://google.com?q=asd&x=y&zxc=asd",
     .uri = "https://google.com?q=asd&x=y&zxc=asd",
     .uriescaped = "https%3A%2F%2Fgoogle.com%3Fq%3Dasd%26x%3Dy%26zxc%3Dasd",
     .scheme = "https",
     .userinfo = "",
     .hosttext = "google.com",
     .hostip = "",
     .porttext = "",
     .query = "q=asd&x=y&zxc=asd",
     .fragment = "",
     .normalized = "https://google.com?q=asd&x=y&zxc=asd",
     .add_base_example = "https://google.com?q=asd&x=y&zxc=asd",
     .remove_base_example = "https://google.com?q=asd&x=y&zxc=asd",
     .query_elements = {{"q", "asd"}, {"x", "y"}, {"zxc", "asd"}}},
    {.test = "https://google.com?q=asd#newplace",
     .uri = "https://google.com?q=asd#newplace",
     .uriescaped = "https%3A%2F%2Fgoogle.com%3Fq%3Dasd%23newplace",
     .scheme = "https",
     .userinfo = "",
     .hosttext = "google.com",
     .hostip = "",
     .porttext = "",
     .query = "q=asd",
     .fragment = "newplace",
     .normalized = "https://google.com?q=asd#newplace",
     .add_base_example = "https://google.com?q=asd#newplace",
     .remove_base_example = "https://google.com?q=asd#newplace",
     .query_elements = {{"q", "asd"}}},
};

class UriParserBase : public testing::Test {
 protected:
  void SetUp() override;
  std::unique_ptr<UriparserSapiSandbox> sandbox_;
};

class UriParserTestData : public UriParserBase,
                          public testing::WithParamInterface<TestVariant> {};

void UriParserBase::SetUp() {
  sandbox_ = std::make_unique<UriparserSapiSandbox>();
  ASSERT_THAT(sandbox_->Init(), IsOk());
}

TEST_P(UriParserTestData, TestUri) {
  const TestVariant& tv = GetParam();

  UriParser uri(sandbox_.get(), tv.test);
  ASSERT_THAT(uri.GetStatus(), IsOk());

  SAPI_ASSERT_OK_AND_ASSIGN(std::string ret, uri.GetUri());
  ASSERT_EQ(ret, tv.uri);
}

TEST_P(UriParserTestData, TestUriEscaped) {
  const TestVariant& tv = GetParam();

  UriParser uri(sandbox_.get(), tv.test);
  ASSERT_THAT(uri.GetStatus(), IsOk());

  SAPI_ASSERT_OK_AND_ASSIGN(std::string ret, uri.GetUriEscaped(true, true));
  ASSERT_EQ(ret, tv.uriescaped);
}

TEST_P(UriParserTestData, TestScheme) {
  const TestVariant& tv = GetParam();

  UriParser uri(sandbox_.get(), tv.test);
  ASSERT_THAT(uri.GetStatus(), IsOk());

  SAPI_ASSERT_OK_AND_ASSIGN(std::string ret, uri.GetScheme());
  ASSERT_EQ(ret, tv.scheme);
}

TEST_P(UriParserTestData, TestUserInfo) {
  const TestVariant& tv = GetParam();

  UriParser uri(sandbox_.get(), tv.test);
  ASSERT_THAT(uri.GetStatus(), IsOk());

  SAPI_ASSERT_OK_AND_ASSIGN(std::string ret, uri.GetUserInfo());
  ASSERT_EQ(ret, tv.userinfo);
}

TEST_P(UriParserTestData, TestHostText) {
  const TestVariant& tv = GetParam();

  UriParser uri(sandbox_.get(), tv.test);
  ASSERT_THAT(uri.GetStatus(), IsOk());

  SAPI_ASSERT_OK_AND_ASSIGN(std::string ret, uri.GetHostText());
  ASSERT_EQ(ret, tv.hosttext);
}

TEST_P(UriParserTestData, TestHostIP) {
  const TestVariant& tv = GetParam();

  UriParser uri(sandbox_.get(), tv.test);
  ASSERT_THAT(uri.GetStatus(), IsOk());

  SAPI_ASSERT_OK_AND_ASSIGN(std::string ret, uri.GetHostIP());
  ASSERT_EQ(ret, tv.hostip);
}

TEST_P(UriParserTestData, TestPortText) {
  const TestVariant& tv = GetParam();

  UriParser uri(sandbox_.get(), tv.test);
  ASSERT_THAT(uri.GetStatus(), IsOk());

  SAPI_ASSERT_OK_AND_ASSIGN(std::string ret, uri.GetPortText());
  ASSERT_EQ(ret, tv.porttext);
}

TEST_P(UriParserTestData, TestQuery) {
  const TestVariant& tv = GetParam();

  UriParser uri(sandbox_.get(), tv.test);
  ASSERT_THAT(uri.GetStatus(), IsOk());

  SAPI_ASSERT_OK_AND_ASSIGN(std::string ret, uri.GetQuery());
  ASSERT_EQ(ret, tv.query);
}

TEST_P(UriParserTestData, TestFragment) {
  const TestVariant& tv = GetParam();

  UriParser uri(sandbox_.get(), tv.test);
  ASSERT_THAT(uri.GetStatus(), IsOk());

  SAPI_ASSERT_OK_AND_ASSIGN(std::string ret, uri.GetFragment());
  ASSERT_EQ(ret, tv.fragment);
}

TEST_P(UriParserTestData, TestNormalize) {
  const TestVariant& tv = GetParam();

  UriParser uri(sandbox_.get(), tv.test);
  ASSERT_THAT(uri.GetStatus(), IsOk());

  ASSERT_THAT(uri.NormalizeSyntax(), IsOk());
  SAPI_ASSERT_OK_AND_ASSIGN(std::string ret, uri.GetUri());
  ASSERT_EQ(ret, tv.normalized);
}

TEST_P(UriParserTestData, TestMultiple) {
  const TestVariant& tv = GetParam();

  UriParser uri(sandbox_.get(), tv.test);
  ASSERT_THAT(uri.GetStatus(), IsOk());

  std::string ret;
  SAPI_ASSERT_OK_AND_ASSIGN(ret, uri.GetQuery());
  ASSERT_EQ(ret, tv.query);

  SAPI_ASSERT_OK_AND_ASSIGN(ret, uri.GetHostIP());
  ASSERT_EQ(ret, tv.hostip);

  ASSERT_THAT(uri.NormalizeSyntax(), IsOk());
  SAPI_ASSERT_OK_AND_ASSIGN(ret, uri.GetUri());
  ASSERT_EQ(ret, tv.normalized);
}

TEST_P(UriParserTestData, TestAddBaseExample) {
  const TestVariant& tv = GetParam();

  UriParser uri(sandbox_.get(), tv.test);
  ASSERT_THAT(uri.GetStatus(), IsOk());

  SAPI_ASSERT_OK_AND_ASSIGN(std::string ret,
                            uri.GetUriWithBase("http://www.example.com"));
  ASSERT_EQ(ret, tv.add_base_example);
}

TEST_P(UriParserTestData, TestRemoveBaseExample) {
  const TestVariant& tv = GetParam();

  UriParser uri(sandbox_.get(), tv.test);
  ASSERT_THAT(uri.GetStatus(), IsOk());

  SAPI_ASSERT_OK_AND_ASSIGN(
      std::string ret, uri.GetUriWithoutBase("http://www.example.com", false));
  ASSERT_EQ(ret, tv.remove_base_example);
}

TEST_P(UriParserTestData, TestPath) {
  const TestVariant& tv = GetParam();

  UriParser uri(sandbox_.get(), tv.test);
  ASSERT_THAT(uri.GetStatus(), IsOk());

  SAPI_ASSERT_OK_AND_ASSIGN(std::vector<std::string> ret, uri.GetPath());
  ASSERT_EQ(ret.size(), tv.path_elements.size());
  for (int i = 0; i < ret.size(); ++i) {
    ASSERT_EQ(ret[i], tv.path_elements[i]);
  }
}

TEST_P(UriParserTestData, TestQueryElements) {
  const TestVariant& tv = GetParam();

  UriParser uri(sandbox_.get(), tv.test);
  ASSERT_THAT(uri.GetStatus(), IsOk());

  SAPI_ASSERT_OK_AND_ASSIGN(auto ret, uri.GetQueryElements());
  ASSERT_EQ(ret.size(), tv.query_elements.size());
  for (auto orig : tv.query_elements) {
    ASSERT_NE(ret.find(orig.first), ret.end());
    ASSERT_EQ(ret[orig.first], orig.second);
  }
}

INSTANTIATE_TEST_SUITE_P(UriParserBase, UriParserTestData,
                         testing::ValuesIn(TestData));

}  // namespace
