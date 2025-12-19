// Copyright 2025 Google LLC
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

#include "sandboxed_api/testcases/replaced_library.h"

#include <limits>
#include <string>

#include "gtest/gtest.h"

namespace {

TEST(Test, Add) {
  EXPECT_EQ(mylib_add(1, 2), 3);
  EXPECT_EQ(mylib_add(-1, -2), -3);
  EXPECT_EQ(mylib_add(std::numeric_limits<int>::min() + 1, -1),
            std::numeric_limits<int>::min());
}

TEST(Test, Copy) {
  EXPECT_EQ(mylib_copy(""), "");
  EXPECT_EQ(mylib_copy("hello"), "hello");
  std::string src = "hello";
  std::string dst = "there";
  mylib_copy({src.data(), src.size() - 1}, dst);
  EXPECT_EQ(dst, "hell");

  char src_buf[5] = {'h', 'e', 'l', 'l', 'o'};
  char dst_buf[5] = {'t', 'h', 'e', 'r', 'e'};
  mylib_copy_raw(src_buf + 1, dst_buf + 1, 3);
  EXPECT_EQ(std::string(dst_buf, sizeof(dst_buf)), "telle");
}

}  // namespace
