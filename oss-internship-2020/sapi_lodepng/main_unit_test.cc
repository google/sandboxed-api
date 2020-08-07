// Copyright 2020 Google LLC
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

#include <glog/logging.h>

#include <filesystem>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "lodepng_sapi.sapi.h"
#include "sandbox.h"
#include "sandboxed_api/util/flag.h"

// defining the flag does not work as intended (always has the default value)
// ignore for now
ABSL_FLAG(string, images_path, std::filesystem::current_path().string(),
          "path to the folder containing test images");

namespace {

TEST(addition, basic) {
  EXPECT_EQ(2, 1 + 1);
//   std::cout << "flag=" << std::string(absl::GetFlag(FLAGS_images_path))
//             << std::endl;
}

}  // namespace