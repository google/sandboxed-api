// Copyright 2022 Google LLC
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

#define CAT_X(A, B, C) A ## B ## C
#define CAT(A, B, C) CAT_X(A, B, C)

TEST(SandboxTest, CAT(Check, NAME, Text)) {
  ZopfliSapiSandbox sandbox;
  ASSERT_THAT(sandbox.Init(), IsOk()) << "Couldn't initialize Sandboxed API";
  ZopfliApi api = ZopfliApi(&sandbox);

  std::string infile_s = GetTestFilePath("text");
  std::string outfile_s = GetTemporaryFile("text.out");
  ASSERT_FALSE(outfile_s.empty());

  std::ifstream infile(infile_s, std::ios::binary);
  ASSERT_TRUE(infile.is_open());
  
  std::ofstream outfile(outfile_s, std::ios::binary);
  ASSERT_TRUE(outfile.is_open());

  absl::Status status = CompressStream(api, infile, outfile, METHOD);
  ASSERT_THAT(status, IsOk()) << "Unable to compress file";

  ASSERT_LT(outfile.tellp(), infile.tellg());
}

TEST(SandboxTest, CAT(Check, NAME, Binary)) {
  ZopfliSapiSandbox sandbox;
  ASSERT_THAT(sandbox.Init(), IsOk()) << "Couldn't initialize Sandboxed API";
  ZopfliApi api = ZopfliApi(&sandbox);

  std::string infile_s = GetTestFilePath("binary");
  std::string outfile_s = GetTemporaryFile("binary.out");
  ASSERT_FALSE(outfile_s.empty());

  std::ifstream infile(infile_s, std::ios::binary);
  ASSERT_TRUE(infile.is_open());
  
  std::ofstream outfile(outfile_s, std::ios::binary);
  ASSERT_TRUE(outfile.is_open());

  absl::Status status = CompressStream(api, infile, outfile, METHOD);
  ASSERT_THAT(status, IsOk()) << "Unable to compress file";

  ASSERT_LT(outfile.tellp(), infile.tellg());
}

#undef CAT_X
#undef CAT
