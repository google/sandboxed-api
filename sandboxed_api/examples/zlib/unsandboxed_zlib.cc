// Copyright 2025 Google LLC
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

#include <cassert>
#include <cstdio>
#include <cstdlib>

#include "absl/base/log_severity.h"
#include "absl/flags/parse.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "zlib.h"

int main(int argc, char* argv[]) {
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();

  int ret;
  int flush;
  unsigned have;
  z_stream strm = {};

  constexpr int kChunk = 16384;
  unsigned char in[kChunk] = {0};
  unsigned char out[kChunk] = {0};

  const char* kZlibVersion = "1.2.11";
  // Allocate deflate state.

  ret = deflateInit_(&strm, Z_DEFAULT_COMPRESSION, kZlibVersion, sizeof(strm));

  if (ret != Z_OK) {
    return ret;
  }

  LOG(INFO) << "Starting compression";

  // Compress until end of file.
  do {
    strm.avail_in = fread(in, 1, kChunk, stdin);
    if (ferror(stdin)) {
      LOG(INFO) << "Error reading from stdin";
      deflateEnd(&strm);
      return Z_ERRNO;
    }
    flush = feof(stdin) ? Z_FINISH : Z_NO_FLUSH;
    strm.next_in = in;

    // Run deflate() on input until output buffer not full, finish compression
    // if all of source has been read in.
    do {
      strm.avail_out = kChunk;
      strm.next_out = out;

      ret = deflate(&strm, flush);    // no bad return value.
      assert(ret != Z_STREAM_ERROR);  // state not clobbered.
      have = kChunk - strm.avail_out;

      if (fwrite(out, 1, have, stdout) != have || ferror(stdout)) {
        // Not really necessary as strm did not change from last transfer.
        deflateEnd(&strm);
        return Z_ERRNO;
      }
    } while (strm.avail_out == 0);
    assert(strm.avail_in == 0);  // all input will be used.

    // done when last data in file processed.
  } while (flush != Z_FINISH);
  assert(ret == Z_STREAM_END);  // stream will be complete.

  // clean up and return.
  deflateEnd(&strm);

  return EXIT_SUCCESS;
}
