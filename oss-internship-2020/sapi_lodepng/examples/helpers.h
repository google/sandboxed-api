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

#ifndef SAPI_LODEPNG_HELPERS_H
#define SAPI_LODEPNG_HELPERS_H

#include <glog/logging.h>

#include <cstdint>

#include "sandboxed_api/sandbox2/util/fileops.h"
#include "sandboxed_api/sandbox2/util/temp_file.h"

constexpr uint32_t kWidth = 512, kHeight = 512, kImgLen = kWidth * kHeight * 4;

// Returns a vector that contains values used for testing.
// This part of code is taken from
// https://github.com/lvandeve/lodepng/blob/master/examples/example_encode.c#L96-L104.
// The generated image contains square fractals.
std::vector<uint8_t> GenerateValues();

// Creates a temporary directory in the current working directory and returns
// the path.
std::string CreateTempDirAtCWD();

#endif  // SAPI_LODEPNG_HELPERS_H
