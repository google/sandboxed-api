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

#include "contrib/c-blosc/utils/utils_blosc.h"

#include <fstream>
#include <iostream>
#include <string>

#include "contrib/c-blosc/sandboxed.h"

constexpr size_t kFileMaxSize = 1024 * 1024 * 1024;  // 1GB

std::streamsize GetStreamSize(std::ifstream& stream) {
  stream.seekg(0, std::ios_base::end);
  std::streamsize ssize = stream.tellg();
  stream.seekg(0, std::ios_base::beg);

  return ssize;
}

absl::Status Compress(CbloscApi& api, std::ifstream& in_stream,
                      std::ofstream& out_stream, int clevel,
                      std::string& compressor, int nthreads) {
  std::streamsize ssize = GetStreamSize(in_stream);
  sapi::v::Array<uint8_t> inbuf(ssize);
  sapi::v::Array<uint8_t> outbuf(ssize);

  in_stream.read(reinterpret_cast<char*>(inbuf.GetData()), ssize);
  if (in_stream.gcount() != ssize) {
    return absl::UnavailableError("Unable to read file");
  }

  int ret;
  SAPI_ASSIGN_OR_RETURN(
      ret, api.blosc_set_compressor(
               sapi::v::ConstCStr(compressor.c_str()).PtrBefore()));
  if (ret < 0) {
    return absl::UnavailableError("Unable to set compressor");
  }

  SAPI_ASSIGN_OR_RETURN(ret, api.blosc_set_nthreads(nthreads));
  if (ret < 0) {
    return absl::UnavailableError("Unable to set nthreads");
  }

  SAPI_ASSIGN_OR_RETURN(
      ssize_t outsize, api.blosc_compress(clevel, 1, sizeof(uint8_t),
                                          inbuf.GetSize(), inbuf.PtrBefore(),
                                          outbuf.PtrAfter(), outbuf.GetSize()));
  if (outsize <= 0) {
    return absl::UnavailableError("Unable to compress file.");
  }

  out_stream.write(reinterpret_cast<char*>(outbuf.GetData()), outsize);
  if (!out_stream.good()) {
    return absl::UnavailableError("Unable to write file");
  }

  return absl::OkStatus();
}

absl::Status Decompress(CbloscApi& api, std::ifstream& in_stream,
                        std::ofstream& out_stream, int nthreads) {
  std::streamsize ssize = GetStreamSize(in_stream);
  sapi::v::Array<uint8_t> inbuf(ssize);

  in_stream.read(reinterpret_cast<char*>(inbuf.GetData()), ssize);
  if (in_stream.gcount() != ssize) {
    return absl::UnavailableError("Unable to read file");
  }

  int ret;
  SAPI_ASSIGN_OR_RETURN(ret, api.blosc_set_nthreads(nthreads));
  if (ret < 0) {
    return absl::UnavailableError("Unable to set nthreads");
  }

  // To not transfer memory twice (for blosc_cbuffer_sizes and decopmress),
  // tranfer memory before using it.
  SAPI_RETURN_IF_ERROR(api.GetSandbox()->Allocate(&inbuf, true));
  SAPI_RETURN_IF_ERROR(api.GetSandbox()->TransferToSandboxee(&inbuf));

  sapi::v::IntBase<size_t> nbytes;
  sapi::v::IntBase<size_t> cbytes;
  sapi::v::IntBase<size_t> blocksize;
  SAPI_RETURN_IF_ERROR(
      api.blosc_cbuffer_sizes(inbuf.PtrNone(), nbytes.PtrAfter(),
                              cbytes.PtrAfter(), blocksize.PtrAfter()));
  if (nbytes.GetValue() == 0) {
    return absl::UnavailableError("Unable to get size");
  }
  if (nbytes.GetValue() > kFileMaxSize) {
    return absl::UnavailableError("The file is to large");
  }

  sapi::v::Array<uint8_t> outbuf(nbytes.GetValue());
  SAPI_ASSIGN_OR_RETURN(ssize_t outsize,
                        api.blosc_decompress(inbuf.PtrNone(), outbuf.PtrAfter(),
                                             outbuf.GetSize()));
  if (outsize <= 0) {
    return absl::UnavailableError("Unable to decompress file");
  }

  out_stream.write(reinterpret_cast<char*>(outbuf.GetData()), outsize);
  if (!out_stream.good()) {
    return absl::UnavailableError("Unable to write file");
  }

  return absl::OkStatus();
}
