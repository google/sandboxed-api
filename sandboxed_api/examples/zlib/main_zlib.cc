// Copyright 2020 Google LLC. All Rights Reserved.
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

#include <linux/audit.h>
#include <sys/syscall.h>

#include <glog/logging.h>
#include "absl/base/macros.h"
#include "sandboxed_api/util/flag.h"
#include "sandboxed_api/examples/zlib/zlib-sapi.sapi.h"
#include "sandboxed_api/examples/zlib/zlib-sapi_embed.h"
#include "sandboxed_api/vars.h"

// Need to define these manually, as zlib.h cannot be directly included. The
// interface generator makes all functions available that were requested in
// sapi_library(), but does not know which macro constants are needed by the
// sandboxee.
#define Z_NO_FLUSH 0
#define Z_FINISH 4
#define Z_OK 0
#define Z_DEFAULT_COMPRESSION (-1)
#define Z_ERRNO (-1)
#define Z_STREAM_ERROR (-2)
#define Z_STREAM_END 1

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);

  sapi::Sandbox sandbox(sapi::zlib::zlib_sapi_embed_create());
  sapi::zlib::ZlibApi api(&sandbox);
  if (!sandbox.Init().ok()) {
    LOG(FATAL) << "Couldn't initialize the Sandboxed API Lib";
  }

  int ret, flush;
  unsigned have;
  sapi::v::Struct<sapi::zlib::z_stream> strm;

  constexpr int kChunk = 16384;
  unsigned char in_[kChunk] = {0};
  unsigned char out_[kChunk] = {0};
  sapi::v::Array<unsigned char> input(in_, kChunk);
  sapi::v::Array<unsigned char> output(out_, kChunk);
  if (!sandbox.Allocate(&input).ok() || !sandbox.Allocate(&output).ok()) {
    LOG(FATAL) << "Allocate memory failed";
  }

  constexpr char kZlibVersion[] = "1.2.11";
  sapi::v::Array<const char> version(kZlibVersion,
                                     ABSL_ARRAYSIZE(kZlibVersion));

  // Allocate deflate state.
  *strm.mutable_data() = sapi::zlib::z_stream{};

  ret = api.deflateInit_(strm.PtrBoth(), Z_DEFAULT_COMPRESSION,
                         version.PtrBefore(), sizeof(sapi::zlib::z_stream))
            .ValueOrDie();
  if (ret != Z_OK) {
    return ret;
  }

  LOG(INFO) << "Starting decompression";

  // Compress until end of file.
  do {
    strm.mutable_data()->avail_in = fread(input.GetLocal(), 1, kChunk, stdin);
    if (!sandbox.TransferToSandboxee(&input).ok()) {
      LOG(FATAL) << "TransferToSandboxee failed";
    }
    if (ferror(stdin)) {
      LOG(INFO) << "Error reading from stdin";
      (void)api.deflateEnd(strm.PtrBoth()).ValueOrDie();
      return Z_ERRNO;
    }
    flush = feof(stdin) ? Z_FINISH : Z_NO_FLUSH;
    strm.mutable_data()->next_in =
        reinterpret_cast<unsigned char*>(input.GetRemote());

    // Run deflate() on input until output buffer not full, finish compression
    // if all of source has been read in.
    do {
      strm.mutable_data()->avail_out = kChunk;
      strm.mutable_data()->next_out =
          reinterpret_cast<unsigned char*>(output.GetRemote());

      ret = api.deflate(strm.PtrBoth(), flush)
                .ValueOrDie();  // no bad return value.

      assert(ret != Z_STREAM_ERROR);  // state not clobbered.
      have = kChunk - strm.data().avail_out;

      if (!sandbox.TransferFromSandboxee(&output).ok()) {
        LOG(FATAL) << "TransferFromSandboxee failed";
      }
      if (fwrite(output.GetLocal(), 1, have, stdout) != have ||
          ferror(stdout)) {
        // Not really necessary as strm did not change from last transfer.
        (void)api.deflateEnd(strm.PtrBoth()).ValueOrDie();
        return Z_ERRNO;
      }
    } while (strm.data().avail_out == 0);
    assert(strm.data().avail_in == 0);  // all input will be used.

    // done when last data in file processed.
  } while (flush != Z_FINISH);
  assert(ret == Z_STREAM_END);  // stream will be complete.

  // clean up and return.
  (void)api.deflateEnd(strm.PtrBoth()).ValueOrDie();

  return 0;
}
