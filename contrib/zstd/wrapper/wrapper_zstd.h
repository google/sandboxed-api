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

#ifndef CONTRIB_ZSTD_WRAPPER_WRAPPER_ZSTD_H_
#define CONTRIB_ZSTD_WRAPPER_WRAPPER_ZSTD_H_

#include "zstd.h"

extern "C" {
int ZSTD_compress_fd(int fdin, int fdout, int level);
int ZSTD_compressStream_fd(ZSTD_CCtx* cctx, int fdin, int fdout);

int ZSTD_decompress_fd(int fdin, int fdout);
int ZSTD_decompressStream_fd(ZSTD_DCtx* dctx, int fdin, int fdout);
};

#endif  // CONTRIB_ZSTD_WRAPPER_WRAPPER_ZSTD_H_
