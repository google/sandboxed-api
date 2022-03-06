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

#include "utils_miniz.h"

#include <cstddef>
#include <vector>
#pragma GCC diagnostic error "-Wmissing-prototypes"
#pragma GCC diagnostic error "-Wuninitialized"

namespace sapi {
namespace util {
std::streamsize GetStreamSize(std::ifstream& stream) {
  stream.seekg(0, std::ios_base::end);
  std::streamsize ssize = stream.tellg();
  stream.seekg(0, std::ios_base::beg);

  return ssize;
}

absl::StatusOr<std::vector<uint8_t>> ReadFile(std::ifstream& in_stream) {
  std::streamsize ssize = GetStreamSize(in_stream);
  std::vector<uint8_t> inbuf(ssize);
  in_stream.read(reinterpret_cast<char*>(inbuf.data()), ssize);
  if (in_stream.gcount() != ssize) {
    return absl::UnavailableError("Unable to read file");
  } else {
    return inbuf;
  }
}

absl::StatusOr<std::vector<uint8_t>> CompressInMemory(miniz_sapi::MinizApi& api,
                                                      uint8_t* ptr_in,
                                                      size_t len, int level) {
  sapi::v::Array<uint8_t> inbuf(ptr_in, len);
  sapi::v::IntBase<size_t> outsize(0);
  SAPI_ASSIGN_OR_RETURN(
      void* ptr, api.tdefl_compress_mem_to_heap(
                     inbuf.PtrBefore(), inbuf.GetSize(), outsize.PtrAfter(),
                     TDEFL_WRITE_ZLIB_HEADER));
  if (!ptr) return absl::UnavailableError("Unable to compress file");
  size_t const size = outsize.GetValue();
  std::vector<uint8_t> out(size);
  sapi::v::Array<uint8_t> outbuf(out.data(), size);
  outbuf.SetRemote(ptr);
  SAPI_RETURN_IF_ERROR(api.GetSandbox()->TransferFromSandboxee(&outbuf));
  sapi::v::RemotePtr p{ptr};
  auto s = api.mz_free(&p).status();
  if (!s.ok()) return s;
  return out;
}

absl::StatusOr<std::vector<uint8_t>> DecompressInMemory(
    miniz_sapi::MinizApi& api, uint8_t* ptr, size_t len) {
  sapi::v::Array<uint8_t> inbuf(ptr, len);
  sapi::v::IntBase<size_t> outsize(0);

  // FIXME: this is vulnerable to a trivial DoS (memory exhaustion) by
  // means of a corrupt input file.  Yuck.
  SAPI_ASSIGN_OR_RETURN(
      void* outptr,
      api.tinfl_decompress_mem_to_heap(
          inbuf.PtrBefore(), inbuf.GetSize(), outsize.PtrAfter(),
          TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_COMPUTE_ADLER32));
  if (!outptr) {
    return absl::UnavailableError("Unable to decompress file");
  }
  size_t const size = outsize.GetValue();
  std::vector<uint8_t> out(size);
  sapi::v::Array<uint8_t> outbuf(out.data(), out.size());
  outbuf.SetRemote(outptr);
  SAPI_RETURN_IF_ERROR(api.GetSandbox()->TransferFromSandboxee(&outbuf));
  sapi::v::RemotePtr p{outptr};
  SAPI_RETURN_IF_ERROR(api.mz_free(&p).status());
  return absl::StatusOr<std::vector<uint8_t>>{std::move(out)};
}
}  // namespace util
}  // namespace sapi
