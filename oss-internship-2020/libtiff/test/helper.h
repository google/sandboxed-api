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

#include <string>

#include "../sandboxed.h"
#include "gtest/gtest.h"
#include "sandboxed_api/sandbox2/util/fileops.h"
#include "sandboxed_api/sandbox2/util/path.h"
#include "sandboxed_api/sandbox2/util/temp_file.h"
#include "sandboxed_api/util/status_matchers.h"

using ::sapi::IsOk;
using ::testing::Eq;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Ne;
using ::testing::NotNull;

std::string GetFilePath(const std::string& filename);
