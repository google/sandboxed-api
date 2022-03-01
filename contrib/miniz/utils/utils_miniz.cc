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

#include <cstddef>
#include <vector>
#include "utils_miniz.h"

std::streamsize GetStreamSize(std::ifstream& stream) {
  stream.seekg(0, std::ios_base::end);
  std::streamsize ssize = stream.tellg();
  stream.seekg(0, std::ios_base::beg);

  return ssize;
}

absl::Status CompressInMemory(miniz_sapi::MinizApi& api,
                              std::ifstream& in_stream,
                              std::ofstream& out_stream, int level) {
  std::streamsize ssize = GetStreamSize(in_stream);
  sapi::v::Array<uint8_t> inbuf(ssize);
  sapi::v::IntBase<size_t> outsize(0);
  in_stream.read(reinterpret_cast<char*>(inbuf.GetData()), ssize);
  if (in_stream.gcount() != ssize) {
    return absl::UnavailableError("Unable to read file");
  }

  SAPI_ASSIGN_OR_RETURN(void* ptr, api.tdefl_compress_mem_to_heap(
                                       inbuf.PtrBefore(), inbuf.GetSize(),
                                       outsize.PtrAfter(), TDEFL_WRITE_ZLIB_HEADER));
  if (!ptr) return absl::UnavailableError("Unable to compress file");
  size_t const size = outsize.GetValue();
  sapi::v::Array<uint8_t> outbuf(size);
  outbuf.SetRemote(ptr);
  SAPI_RETURN_IF_ERROR(api.GetSandbox()->TransferFromSandboxee(&outbuf));
  sapi::v::RemotePtr p{ptr};
  SAPI_RETURN_IF_ERROR(api.mz_free(&p));
  out_stream.write(reinterpret_cast<char*>(outbuf.GetLocal()), size);
  if (!out_stream.good()) {
    return absl::UnavailableError("Unable to write file");
  }

  return absl::OkStatus();
}

absl::Status DecompressInMemory(miniz_sapi::MinizApi& api, std::ifstream& in_stream,
                                std::ofstream& out_stream) {
  int iserr;
  std::streamsize ssize = GetStreamSize(in_stream);
  sapi::v::Array<uint8_t> inbuf(ssize);
  in_stream.read(reinterpret_cast<char*>(inbuf.GetData()), ssize);
  if (in_stream.gcount() != ssize) {
    return absl::UnavailableError("Unable to read file");
  }
  sapi::v::IntBase<size_t> outsize(0);

  // FIXME: this is vulnerable to a trivial DoS (memory exhaustion) by
  // means of a corrupt input file.  Yuck.
  SAPI_ASSIGN_OR_RETURN(
      void* ptr,
      api.tinfl_decompress_mem_to_heap(
          inbuf.PtrBefore(), inbuf.GetSize(), outsize.PtrAfter(),
          TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_COMPUTE_ADLER32));
  if (iserr) {
    return absl::UnavailableError("Unable to decompress file");
  }
  sapi::v::Array<uint8_t> outbuf(outsize.GetValue());
  SAPI_RETURN_IF_ERROR(api.GetSandbox()->TransferFromSandboxee(&outbuf));
  sapi::v::RemotePtr p{ptr};
  SAPI_RETURN_IF_ERROR(api.mz_free(&p));
  return absl::OkStatus();
}
