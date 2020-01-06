// Copyright 2020 Google LLC. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "sandboxed_api/sandbox2/util/path.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Pair;
using testing::StrEq;

namespace sandbox2 {
namespace {

TEST(PathTest, ArgumentTypes) {
  // JoinPath must be able to accept arguments that are compatible with
  // absl::string_view. So test a few of them here.
  const char char_array[] = "a";
  const char* char_ptr = "b";
  std::string string_type = "c";
  absl::string_view sp_type = "d";

  EXPECT_THAT(file::JoinPath(char_array, char_ptr, string_type, sp_type),
              StrEq("a/b/c/d"));
}

TEST(PathTest, JoinPath) {
  EXPECT_THAT(file::JoinPath("/foo", "bar"), StrEq("/foo/bar"));
  EXPECT_THAT(file::JoinPath("foo", "bar"), StrEq("foo/bar"));
  EXPECT_THAT(file::JoinPath("foo", "/bar"), StrEq("foo/bar"));
  EXPECT_THAT(file::JoinPath("/foo", "/bar"), StrEq("/foo/bar"));

  EXPECT_THAT(file::JoinPath("", "/bar"), StrEq("/bar"));
  EXPECT_THAT(file::JoinPath("", "bar"), StrEq("bar"));
  EXPECT_THAT(file::JoinPath("/foo", ""), StrEq("/foo"));

  EXPECT_THAT(file::JoinPath("/foo/bar/baz/", "/blah/blink/biz"),
              StrEq("/foo/bar/baz/blah/blink/biz"));

  EXPECT_THAT(file::JoinPath("/foo", "bar", "baz"), StrEq("/foo/bar/baz"));
  EXPECT_THAT(file::JoinPath("foo", "bar", "baz"), StrEq("foo/bar/baz"));
  EXPECT_THAT(file::JoinPath("/foo", "bar", "baz", "blah"),
              StrEq("/foo/bar/baz/blah"));
  EXPECT_THAT(file::JoinPath("/foo", "bar", "/baz", "blah"),
              StrEq("/foo/bar/baz/blah"));
  EXPECT_THAT(file::JoinPath("/foo", "/bar/", "/baz", "blah"),
              StrEq("/foo/bar/baz/blah"));
  EXPECT_THAT(file::JoinPath("/foo", "/bar/", "baz", "blah"),
              StrEq("/foo/bar/baz/blah"));

  EXPECT_THAT(file::JoinPath("/", "a"), StrEq("/a"));
  EXPECT_THAT(file::JoinPath(), StrEq(""));
}

TEST(PathTest, SplitPath) {
  // We cannot write the type directly within the EXPECT, because the ',' breaks
  // the macro.
  EXPECT_THAT(file::SplitPath("/hello/"), Pair("/hello", ""));
  EXPECT_THAT(file::SplitPath("/hello"), Pair("/", "hello"));
  EXPECT_THAT(file::SplitPath("hello/world"), Pair("hello", "world"));
  EXPECT_THAT(file::SplitPath("hello/"), Pair("hello", ""));
  EXPECT_THAT(file::SplitPath("world"), Pair("", "world"));
  EXPECT_THAT(file::SplitPath("/"), Pair("/", ""));
  EXPECT_THAT(file::SplitPath(""), Pair("", ""));
}

TEST(PathTest, CleanPath) {
  EXPECT_THAT(file::CleanPath(""), StrEq("."));
  EXPECT_THAT(file::CleanPath("x"), StrEq("x"));
  EXPECT_THAT(file::CleanPath("/a/b/c/d"), StrEq("/a/b/c/d"));
  EXPECT_THAT(file::CleanPath("/a/b/c/d/"), StrEq("/a/b/c/d"));
  EXPECT_THAT(file::CleanPath("/a//b"), StrEq("/a/b"));
  EXPECT_THAT(file::CleanPath("//a//b/"), StrEq("/a/b"));
  EXPECT_THAT(file::CleanPath("/.."), StrEq("/"));
  EXPECT_THAT(file::CleanPath("/././././"), StrEq("/"));
  EXPECT_THAT(file::CleanPath("/a/b/.."), StrEq("/a"));
  EXPECT_THAT(file::CleanPath("/a/b/../../.."), StrEq("/"));
  EXPECT_THAT(file::CleanPath("//a//b/..////../..//"), StrEq("/"));
  EXPECT_THAT(file::CleanPath("//a//../x//"), StrEq("/x"));
  EXPECT_THAT(file::CleanPath("../../a/b/../c"), StrEq("../../a/c"));
  EXPECT_THAT(file::CleanPath("../../a/b/../c/../.."), StrEq("../.."));
  EXPECT_THAT(file::CleanPath("foo/../../../bar"), StrEq("../../bar"));
}

}  // namespace
}  // namespace sandbox2
