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

#include "contrib/zopfli/utils/utils_zopfli.h"

#include <unistd.h>

#include <fstream>

absl::Status Compress(ZopfliApi& api, std::ifstream& instream,
                      std::ofstream& outstream, ZopfliFormat format) {
  // Get size of Stream
  instream.seekg(0, std::ios_base::end);
  std::streamsize ssize = instream.tellg();
  instream.seekg(0, std::ios_base::beg);

  // Read data
  sapi::v::Array<uint8_t> inbuf(ssize);
  instream.read(reinterpret_cast<char*>(inbuf.GetData()), ssize);
  if (instream.gcount() != ssize) {
    return absl::UnavailableError("Unable to read file");
  }

  // Compress
  sapi::v::Struct<ZopfliOptions> options;
  SAPI_RETURN_IF_ERROR(api.ZopfliInitOptions(options.PtrAfter()));

  sapi::v::GenericPtr outptr;
  sapi::v::IntBase<size_t> outsize(0);

  SAPI_RETURN_IF_ERROR(
      api.ZopfliCompress(options.PtrBefore(), format, inbuf.PtrBefore(), ssize,
                         outptr.PtrAfter(), outsize.PtrBoth()));

  // Get and save data
  sapi::v::Array<int8_t> outbuf(outsize.GetValue());
  outbuf.SetRemote(reinterpret_cast<void*>(outptr.GetValue()));
  SAPI_RETURN_IF_ERROR(api.GetSandbox()->TransferFromSandboxee(&outbuf));

  outstream.write(reinterpret_cast<char*>(outbuf.GetData()), outbuf.GetSize());
  if (!outstream.good()) {
    return absl::UnavailableError("Unable to write file");
  }
  return absl::OkStatus();
}

absl::Status CompressFD(ZopfliApi& api, sapi::v::Fd& infd, sapi::v::Fd& outfd,
                        ZopfliFormat format) {
  SAPI_RETURN_IF_ERROR(api.GetSandbox()->TransferToSandboxee(&infd));
  SAPI_RETURN_IF_ERROR(api.GetSandbox()->TransferToSandboxee(&outfd));

  sapi::v::Struct<ZopfliOptions> options;
  SAPI_RETURN_IF_ERROR(api.ZopfliInitOptions(options.PtrAfter()));

  SAPI_ASSIGN_OR_RETURN(
      int ret, api.ZopfliCompressFD(options.PtrBefore(), format,
                                    infd.GetRemoteFd(), outfd.GetRemoteFd()));

  infd.CloseRemoteFd(api.GetSandbox()->rpc_channel()).IgnoreError();
  outfd.CloseRemoteFd(api.GetSandbox()->rpc_channel()).IgnoreError();

  if (ret == -1) {
    return absl::UnavailableError("Unable to compress file");
  }

  return absl::OkStatus();
}
