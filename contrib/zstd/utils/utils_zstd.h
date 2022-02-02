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

#ifndef CONTRIB_ZSTD_UTILS_UTILS_ZSTD_H_
#define CONTRIB_ZSTD_UTILS_UTILS_ZSTD_H_

#include <fstream>
#include <string>

#include "contrib/zstd/sandboxed.h"

absl::Status CompressInMemory(ZstdApi& api, std::ifstream& in_stream,
                              std::ofstream& out_stream, int level);
absl::Status DecompressInMemory(ZstdApi& api, std::ifstream& in_stream,
                                std::ofstream& out_stream);
absl::Status CompressInMemoryFD(ZstdApi& api, sapi::v::Fd& infd,
                                sapi::v::Fd& outfd, int level);
absl::Status DecompressInMemoryFD(ZstdApi& api, sapi::v::Fd& infd,
                                  sapi::v::Fd& outfd);

absl::Status CompressStream(ZstdApi& api, std::ifstream& in_stream,
                            std::ofstream& outstreeam, int level);
absl::Status DecompressStream(ZstdApi& api, std::ifstream& in_stream,
                              std::ofstream& out_stream);
absl::Status CompressStreamFD(ZstdApi& api, sapi::v::Fd& infd,
                              sapi::v::Fd& outfd, int level);
absl::Status DecompressStreamFD(ZstdApi& api, sapi::v::Fd& infd,
                                sapi::v::Fd& outfd);

#endif  // CONTRIB_ZSTD_UTILS_UTILS_ZSTD_H_
