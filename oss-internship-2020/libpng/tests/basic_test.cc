// Copyright 2020 Google LLC
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

#include "../sandboxed.h"  // NOLINT(build/include)
#include "helper.h"        // NOLINT(build/include)
#include "libpng.h"        // NOLINT(build/include)
#include "gtest/gtest.h"
#include "sandboxed_api/util/fileops.h"
#include "sandboxed_api/util/path.h"
#include "sandboxed_api/util/status_matchers.h"
#include "sandboxed_api/util/temp_file.h"

namespace {

using ::sapi::IsOk;
using ::testing::Eq;
using ::testing::IsTrue;

TEST(SandboxTest, ReadWrite) {
  std::string infile = GetFilePath("pngtest.png");

  absl::StatusOr<std::string> status_or_path =
      sapi::CreateNamedTempFileAndClose("output.png");
  ASSERT_THAT(status_or_path, IsOk()) << "Could not create temp output file";

  std::string outfile = sapi::file::JoinPath(sapi::file_util::fileops::GetCWD(),
                                             status_or_path.value());

  LibPNGSapiSandbox sandbox;
  sandbox.AddFile(infile);
  sandbox.AddFile(outfile);

  ASSERT_THAT(sandbox.Init(), IsOk()) << "Couldn't initialize Sandboxed API";

  LibPNGApi api(&sandbox);

  sapi::v::Struct<png_image> image;
  sapi::v::ConstCStr infile_var(infile.c_str());
  sapi::v::ConstCStr outfile_var(outfile.c_str());

  image.mutable_data()->version = PNG_IMAGE_VERSION;

  absl::StatusOr<int> status_or_int = api.png_image_begin_read_from_file(
      image.PtrBoth(), infile_var.PtrBefore());
  ASSERT_THAT(status_or_int, IsOk())
      << "fatal error when invoking png_image_begin_read_from_file";
  ASSERT_THAT(status_or_int.value(), IsTrue())
      << "png_image_begin_read_from_file failed: "
      << image.mutable_data()->message;

  image.mutable_data()->format = PNG_FORMAT_RGBA;
  ASSERT_THAT(image.mutable_data()->version, Eq(PNG_IMAGE_VERSION))
      << "image version changed";

  sapi::v::Array<uint8_t> buffer(PNG_IMAGE_SIZE(*image.mutable_data()));
  sapi::v::NullPtr null = sapi::v::NullPtr();
  status_or_int = api.png_image_finish_read(image.PtrBoth(), &null,
                                            buffer.PtrBoth(), 0, &null);
  ASSERT_THAT(status_or_int, IsOk())
      << "fatal error when invoking png_image_finish_read";
  ASSERT_THAT(status_or_int.value(), IsTrue())
      << "png_image_finish_read failed: " << image.mutable_data()->message;
  ASSERT_THAT(image.mutable_data()->version, Eq(PNG_IMAGE_VERSION))
      << "image version changed";
  ASSERT_THAT(image.mutable_data()->format, Eq(PNG_FORMAT_RGBA))
      << "image format changed";

  status_or_int = api.png_image_write_to_file(
      image.PtrBoth(), outfile_var.PtrBefore(), 0, buffer.PtrBoth(), 0, &null);
  ASSERT_THAT(status_or_int, IsOk())
      << "fatal error when invoking png_image_write_to_file";
  ASSERT_THAT(status_or_int.value(), IsTrue())
      << "png_image_finish_read failed: " << image.mutable_data()->message;
  ASSERT_THAT(image.mutable_data()->version, Eq(PNG_IMAGE_VERSION))
      << "image version changed";
  ASSERT_THAT(image.mutable_data()->format, Eq(PNG_FORMAT_RGBA))
      << "image format changed";
}

}  // namespace
