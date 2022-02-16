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

#include "contrib/zopfli/wrapper/wrapper_zopfli.h"

#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <memory>

int ZopfliCompressFD(const ZopfliOptions* options, ZopfliFormat output_type,
                     int infd, int outfd) {
  off_t insize = lseek(infd, 0, SEEK_END);
  if (insize < 0) {
    return -1;
  }
  if (lseek(infd, 0, SEEK_SET) < 0) {
    return -1;
  }

  auto inbuf = std::make_unique<uint8_t[]>(insize);
  if (read(infd, inbuf.get(), insize) != insize) {
    return -1;
  }

  size_t outsize = 0;
  uint8_t* outbuf = nullptr;
  ZopfliCompress(options, output_type, inbuf.get(), insize, &outbuf, &outsize);
  size_t retsize = write(outfd, outbuf, outsize);
  free(outbuf);

  if (outsize != retsize) {
    return -1;
  }

  return 0;
}
