// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Google LLC
//                Mariusz Zaborski <oshogbo@invisiblethingslab.com>

#ifndef UTILS_ZSTD_H_
#define UTILS_ZSTD_H_

absl::Status CompressInMemory(ZstdApi& api, std::ifstream& inFile,
                              std::ofstream& outFile, int level);
absl::Status DecompressInMemory(ZstdApi& api, std::ifstream& inFile,
                                std::ofstream& outFile);

absl::Status CompressStream(ZstdApi& api, std::ifstream& inFile,
                            std::ofstream& outFile, int level);
absl::Status DecompressStream(ZstdApi& api, std::ifstream& inFile,
                              std::ofstream& outFile);

#endif  // UTILS_ZSTD_H_
