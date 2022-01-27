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

#ifndef CONTRIB_ZOPFLI_UTILS_UTILS_ZOPFLI_H_
#define CONTRIB_ZOPFLI_UTILS_UTILS_ZOPFLI_H_

absl::Status CompressStream(ZopfliApi& api, std::ifstream& instream,
                            std::ofstream& outstream, ZopfliFormat format);

#endif  // CONTRIB_ZOPFLI_UTILS_UTILS_ZOPFLI_H_
