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

#include "wrapper_zstd.h"

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>

#include "zstd.h"

static constexpr size_t kFileMaxSize = 1024 * 1024 * 1024;  // 1GB

off_t FDGetSize(int fd) {
  off_t size = lseek(fd, 0, SEEK_END);
  if (size < 0) {
    return -1;
  }
  if (lseek(fd, 0, SEEK_SET) < 0) {
    return -1;
  }

  return (size);
}

int ZSTD_compress_fd(int fdin, int fdout, int level) {
  off_t sizein = FDGetSize(fdin);
  if (sizein <= 0) {
    return -1;
  }

  size_t sizeout = ZSTD_compressBound(sizein);

  std::unique_ptr<int8_t[]> bufin = std::make_unique<int8_t[]>(sizein);
  std::unique_ptr<int8_t[]> bufout = std::make_unique<int8_t[]>(sizeout);

  if (read(fdin, bufin.get(), sizein) != sizein) {
    return -1;
  }

  int retsize =
      ZSTD_compress(bufout.get(), sizeout, bufin.get(), sizein, level);
  if (ZSTD_isError(retsize)) {
    return -1;
  }

  if (write(fdout, bufout.get(), retsize) != retsize) {
    return -1;
  }

  return 0;
}

int ZSTD_compressStream_fd(ZSTD_CCtx* cctx, int fdin, int fdout) {
  size_t sizein = ZSTD_CStreamInSize();
  size_t sizeout = ZSTD_CStreamOutSize();

  std::unique_ptr<int8_t[]> bufin = std::make_unique<int8_t[]>(sizein);
  std::unique_ptr<int8_t[]> bufout = std::make_unique<int8_t[]>(sizeout);

  ssize_t size;
  while ((size = read(fdin, bufin.get(), sizein)) > 0) {
    ZSTD_inBuffer_s struct_in;
    struct_in.src = bufin.get();
    struct_in.pos = 0;
    struct_in.size = size;

    ZSTD_EndDirective mode = ZSTD_e_continue;
    if (size < sizein) {
      mode = ZSTD_e_end;
    }

    bool isdone = false;
    while (!isdone) {
      ZSTD_outBuffer_s struct_out;
      struct_out.dst = bufout.get();
      struct_out.pos = 0;
      struct_out.size = sizeout;

      size_t remaining =
          ZSTD_compressStream2(cctx, &struct_out, &struct_in, mode);
      if (ZSTD_isError(remaining)) {
        return -1;
      }
      if (write(fdout, bufout.get(), struct_out.pos) != struct_out.pos) {
        return -1;
      }

      if (mode == ZSTD_e_continue) {
        isdone = (struct_in.pos == size);
      } else {
        isdone = (remaining == 0);
      }
    }
  }

  if (size != 0) {
    return -1;
  }

  return 0;
}

int ZSTD_decompress_fd(int fdin, int fdout) {
  off_t sizein = FDGetSize(fdin);
  if (sizein <= 0) {
    return -1;
  }
  std::unique_ptr<int8_t[]> bufin = std::make_unique<int8_t[]>(sizein);
  if (read(fdin, bufin.get(), sizein) != sizein) {
    return -1;
  }

  size_t sizeout = ZSTD_getFrameContentSize(bufin.get(), sizein);
  if (ZSTD_isError(sizeout) || sizeout > kFileMaxSize) {
    return -1;
  }

  std::unique_ptr<int8_t[]> bufout = std::make_unique<int8_t[]>(sizeout);

  size_t desize = ZSTD_decompress(bufout.get(), sizeout, bufin.get(), sizein);
  if (ZSTD_isError(desize) || desize != sizeout) {
    return -1;
  }

  if (write(fdout, bufout.get(), sizeout) != sizeout) {
    return -1;
  }

  return 0;
}

int ZSTD_decompressStream_fd(ZSTD_DCtx* dctx, int fdin, int fdout) {
  size_t sizein = ZSTD_CStreamInSize();
  size_t sizeout = ZSTD_CStreamOutSize();

  std::unique_ptr<int8_t[]> bufin = std::make_unique<int8_t[]>(sizein);
  std::unique_ptr<int8_t[]> bufout = std::make_unique<int8_t[]>(sizeout);

  ssize_t size;
  while ((size = read(fdin, bufin.get(), sizein)) > 0) {
    ZSTD_inBuffer_s struct_in;
    struct_in.src = bufin.get();
    struct_in.pos = 0;
    struct_in.size = size;

    while (struct_in.pos < size) {
      ZSTD_outBuffer_s struct_out;
      struct_out.dst = bufout.get();
      struct_out.pos = 0;
      struct_out.size = sizeout;

      size_t ret = ZSTD_decompressStream(dctx, &struct_out, &struct_in);
      if (ZSTD_isError(ret)) {
        return -1;
      }
      if (write(fdout, bufout.get(), struct_out.pos) != struct_out.pos) {
        return -1;
      }
    }
  }

  if (size != 0) {
    return -1;
  }

  return 0;
}
