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

#include <fstream>
#include <iostream>
#include <string>

#include "contrib/zstd/sandboxed.h"

static const size_t kFileMaxSize = 1024 * 1024 * 1024;  // 1GB

std::streamsize GetStreamSize(std::ifstream& stream) {
  stream.seekg(0, std::ios_base::end);
  std::streamsize ssize = stream.tellg();
  stream.seekg(0, std::ios_base::beg);

  return ssize;
}

absl::Status CompressInMemory(ZstdApi& api, std::ifstream& in_stream,
                              std::ofstream& out_stream, int level) {
  std::streamsize ssize = GetStreamSize(in_stream);
  sapi::v::Array<uint8_t> inbuf(ssize);
  in_stream.read(reinterpret_cast<char*>(inbuf.GetData()), ssize);
  if (in_stream.gcount() != ssize) {
    return absl::UnavailableError("Unable to read file");
  }

  SAPI_ASSIGN_OR_RETURN(size_t size, api.ZSTD_compressBound(inbuf.GetSize()));
  sapi::v::Array<uint8_t> outbuf(size);

  SAPI_ASSIGN_OR_RETURN(
      size_t outsize,
      api.ZSTD_compress(outbuf.PtrAfter(), size, inbuf.PtrBefore(),
                        inbuf.GetSize(), level));
  SAPI_ASSIGN_OR_RETURN(int iserr, api.ZSTD_isError(outsize))
  if (iserr) {
    return absl::UnavailableError("Unable to compress file");
  }

  out_stream.write(reinterpret_cast<char*>(outbuf.GetData()), outsize);
  if (!out_stream.good()) {
    return absl::UnavailableError("Unable to write file");
  }

  return absl::OkStatus();
}

absl::Status DecompressInMemory(ZstdApi& api, std::ifstream& in_stream,
                                std::ofstream& out_stream) {
  int iserr;
  std::streamsize ssize = GetStreamSize(in_stream);
  sapi::v::Array<uint8_t> inbuf(ssize);
  in_stream.read(reinterpret_cast<char*>(inbuf.GetData()), ssize);
  if (in_stream.gcount() != ssize) {
    return absl::UnavailableError("Unable to read file");
  }

  SAPI_ASSIGN_OR_RETURN(size_t size, api.ZSTD_getFrameContentSize(
                                         inbuf.PtrBefore(), inbuf.GetSize()));
  if (size > kFileMaxSize) {
    return absl::UnavailableError("File to large");
  }
  SAPI_ASSIGN_OR_RETURN(iserr, api.ZSTD_isError(size));
  if (iserr) {
    return absl::UnavailableError("Unable to decompress file");
  }
  sapi::v::Array<uint8_t> outbuf(size);

  SAPI_ASSIGN_OR_RETURN(
      size_t desize, api.ZSTD_decompress(outbuf.PtrAfter(), size,
                                         inbuf.PtrBefore(), inbuf.GetSize()));
  SAPI_ASSIGN_OR_RETURN(iserr, api.ZSTD_isError(desize));
  if (iserr) {
    return absl::UnavailableError("Unable to decompress file");
  }

  out_stream.write(reinterpret_cast<char*>(outbuf.GetData()), desize);
  if (!out_stream.good()) {
    return absl::UnavailableError("Unable to write file");
  }

  return absl::OkStatus();
}

absl::Status CompressStream(ZstdApi& api, std::ifstream& in_stream,
                            std::ofstream& out_stream, int level) {
  int iserr;

  // Create necessary buffers.
  SAPI_ASSIGN_OR_RETURN(size_t inbuf_size, api.ZSTD_CStreamInSize());
  SAPI_ASSIGN_OR_RETURN(size_t outbuf_size, api.ZSTD_CStreamOutSize());
  sapi::v::Array<uint8_t> inbuf(inbuf_size);
  sapi::v::Array<uint8_t> outbuf(outbuf_size);

  if (!api.GetSandbox()->Allocate(&inbuf).ok() ||
      !api.GetSandbox()->Allocate(&outbuf).ok()) {
    return absl::UnavailableError("Unable to allocate buffors");
  }

  // Create Zstd context.
  SAPI_ASSIGN_OR_RETURN(ZSTD_CCtx* cctx, api.ZSTD_createCCtx());
  sapi::v::RemotePtr rcctx(cctx);

  SAPI_ASSIGN_OR_RETURN(iserr, api.ZSTD_CCtx_setParameter(
                                   &rcctx, ZSTD_c_compressionLevel, level));
  SAPI_ASSIGN_OR_RETURN(iserr, api.ZSTD_isError(iserr))
  if (iserr) {
    return absl::UnavailableError("Unable to set parameter");
  }
  SAPI_ASSIGN_OR_RETURN(
      iserr, api.ZSTD_CCtx_setParameter(&rcctx, ZSTD_c_checksumFlag, 1));
  SAPI_ASSIGN_OR_RETURN(iserr, api.ZSTD_isError(iserr))
  if (iserr) {
    return absl::UnavailableError("Unable to set parameter");
  }

  // Compress.
  while (in_stream) {
    in_stream.read(reinterpret_cast<char*>(inbuf.GetData()), inbuf_size);

    if (!api.GetSandbox()->TransferToSandboxee(&inbuf).ok()) {
      return absl::UnavailableError("Unable to transfer data");
    }

    sapi::v::Struct<ZSTD_inBuffer_s> struct_in;
    struct_in.mutable_data()->src = static_cast<uint8_t*>(inbuf.GetRemote());
    struct_in.mutable_data()->pos = 0;
    struct_in.mutable_data()->size = in_stream.gcount();

    ZSTD_EndDirective mode = ZSTD_e_continue;
    if (in_stream.gcount() < inbuf_size) {
      mode = ZSTD_e_end;
    }

    bool isdone = false;
    while (!isdone) {
      sapi::v::Struct<ZSTD_outBuffer_s> struct_out;
      struct_out.mutable_data()->dst =
          static_cast<uint8_t*>(outbuf.GetRemote());
      struct_out.mutable_data()->pos = 0;
      struct_out.mutable_data()->size = outbuf.GetSize();

      SAPI_ASSIGN_OR_RETURN(size_t remaining, api.ZSTD_compressStream2(
                                                  &rcctx, struct_out.PtrBoth(),
                                                  struct_in.PtrBoth(), mode));
      SAPI_ASSIGN_OR_RETURN(int iserr, api.ZSTD_isError(remaining))
      if (iserr) {
        return absl::UnavailableError("Unable to decompress file");
      }

      if (!api.GetSandbox()->TransferFromSandboxee(&outbuf).ok()) {
        return absl::UnavailableError("Unable to transfer data from");
      }
      out_stream.write(reinterpret_cast<char*>(outbuf.GetData()),
                       struct_out.mutable_data()->pos);
      if (!out_stream.good()) {
        return absl::UnavailableError("Unable to write file");
      }

      if (mode == ZSTD_e_continue) {
        isdone = (struct_in.mutable_data()->pos == in_stream.gcount());
      } else {
        isdone = (remaining == 0);
      }
    }
  }

  api.ZSTD_freeDCtx(&rcctx).IgnoreError();

  return absl::OkStatus();
}

absl::Status DecompressStream(ZstdApi& api, std::ifstream& in_stream,
                              std::ofstream& out_stream) {
  // Create necessary buffers.
  SAPI_ASSIGN_OR_RETURN(size_t inbuf_size, api.ZSTD_CStreamInSize());
  SAPI_ASSIGN_OR_RETURN(size_t outbuf_size, api.ZSTD_CStreamOutSize());
  sapi::v::Array<uint8_t> inbuf(inbuf_size);
  sapi::v::Array<uint8_t> outbuf(outbuf_size);

  if (!api.GetSandbox()->Allocate(&inbuf).ok() ||
      !api.GetSandbox()->Allocate(&outbuf).ok()) {
    return absl::UnavailableError("Unable to allocate buffors");
  }

  // Create Zstd context.
  SAPI_ASSIGN_OR_RETURN(ZSTD_DCtx* dctx, api.ZSTD_createDCtx());
  sapi::v::RemotePtr rdctx(dctx);

  // Decompress.
  while (in_stream) {
    in_stream.read(reinterpret_cast<char*>(inbuf.GetData()), inbuf_size);

    if (!api.GetSandbox()->TransferToSandboxee(&inbuf).ok()) {
      return absl::UnavailableError("Unable to transfer data");
    }

    sapi::v::Struct<ZSTD_inBuffer_s> struct_in;
    *struct_in.mutable_data() = {static_cast<uint8_t*>(inbuf.GetRemote()),
                                 (size_t)in_stream.gcount(), 0};

    bool isdone = false;
    while (struct_in.mutable_data()->pos < in_stream.gcount()) {
      sapi::v::Struct<ZSTD_outBuffer_s> struct_out;
      *struct_out.mutable_data() = {static_cast<uint8_t*>(outbuf.GetRemote()),
                                    (size_t)outbuf.GetSize(), 0};

      SAPI_ASSIGN_OR_RETURN(
          size_t ret, api.ZSTD_decompressStream(&rdctx, struct_out.PtrBoth(),
                                                struct_in.PtrBoth()));
      SAPI_ASSIGN_OR_RETURN(int iserr, api.ZSTD_isError(ret))
      if (iserr) {
        return absl::UnavailableError("Unable to decompress file");
      }

      if (!api.GetSandbox()->TransferFromSandboxee(&outbuf).ok()) {
        return absl::UnavailableError("Unable to transfer data from");
      }

      out_stream.write(reinterpret_cast<char*>(outbuf.GetData()),
                       struct_out.mutable_data()->pos);
      if (!out_stream.good()) {
        return absl::UnavailableError("Unable to write file");
      }
    }
  }

  api.ZSTD_freeDCtx(&rdctx).IgnoreError();

  return absl::OkStatus();
}

absl::Status CompressInMemoryFD(ZstdApi& api, sapi::v::Fd& infd,
                                sapi::v::Fd& outfd, int level) {
  SAPI_RETURN_IF_ERROR(api.GetSandbox()->TransferToSandboxee(&infd));
  SAPI_RETURN_IF_ERROR(api.GetSandbox()->TransferToSandboxee(&outfd));

  SAPI_ASSIGN_OR_RETURN(
      int iserr,
      api.ZSTD_compress_fd(infd.GetRemoteFd(), outfd.GetRemoteFd(), 0));
  SAPI_ASSIGN_OR_RETURN(iserr, api.ZSTD_isError(iserr))
  if (iserr) {
    return absl::UnavailableError("Unable to compress file");
  }

  infd.CloseRemoteFd(api.GetSandbox()->rpc_channel()).IgnoreError();
  outfd.CloseRemoteFd(api.GetSandbox()->rpc_channel()).IgnoreError();

  return absl::OkStatus();
}

absl::Status DecompressInMemoryFD(ZstdApi& api, sapi::v::Fd& infd,
                                  sapi::v::Fd& outfd) {
  SAPI_RETURN_IF_ERROR(api.GetSandbox()->TransferToSandboxee(&infd));
  SAPI_RETURN_IF_ERROR(api.GetSandbox()->TransferToSandboxee(&outfd));

  SAPI_ASSIGN_OR_RETURN(int iserr, api.ZSTD_decompress_fd(infd.GetRemoteFd(),
                                                          outfd.GetRemoteFd()));
  SAPI_ASSIGN_OR_RETURN(iserr, api.ZSTD_isError(iserr))
  if (iserr) {
    return absl::UnavailableError("Unable to compress file");
  }

  infd.CloseRemoteFd(api.GetSandbox()->rpc_channel()).IgnoreError();
  outfd.CloseRemoteFd(api.GetSandbox()->rpc_channel()).IgnoreError();

  return absl::OkStatus();
}

absl::Status CompressStreamFD(ZstdApi& api, sapi::v::Fd& infd,
                              sapi::v::Fd& outfd, int level) {
  SAPI_ASSIGN_OR_RETURN(ZSTD_CCtx* cctx, api.ZSTD_createCCtx());
  sapi::v::RemotePtr rcctx(cctx);

  int iserr;
  SAPI_ASSIGN_OR_RETURN(iserr, api.ZSTD_CCtx_setParameter(
                                   &rcctx, ZSTD_c_compressionLevel, level));
  SAPI_ASSIGN_OR_RETURN(iserr, api.ZSTD_isError(iserr));
  if (iserr) {
    return absl::UnavailableError("Unable to set parameter l");
  }
  SAPI_ASSIGN_OR_RETURN(
      iserr, api.ZSTD_CCtx_setParameter(&rcctx, ZSTD_c_checksumFlag, 1));
  SAPI_ASSIGN_OR_RETURN(iserr, api.ZSTD_isError(iserr));
  if (iserr) {
    return absl::UnavailableError("Unable to set parameter c");
  }

  SAPI_RETURN_IF_ERROR(api.GetSandbox()->TransferToSandboxee(&infd));
  SAPI_RETURN_IF_ERROR(api.GetSandbox()->TransferToSandboxee(&outfd));

  SAPI_ASSIGN_OR_RETURN(iserr,
                        api.ZSTD_compressStream_fd(&rcctx, infd.GetRemoteFd(),
                                                   outfd.GetRemoteFd()));
  if (iserr) {
    return absl::UnavailableError("Unable to compress");
  }

  infd.CloseRemoteFd(api.GetSandbox()->rpc_channel()).IgnoreError();
  outfd.CloseRemoteFd(api.GetSandbox()->rpc_channel()).IgnoreError();

  return absl::OkStatus();
}

absl::Status DecompressStreamFD(ZstdApi& api, sapi::v::Fd& infd,
                                sapi::v::Fd& outfd) {
  SAPI_ASSIGN_OR_RETURN(ZSTD_DCtx* dctx, api.ZSTD_createDCtx());
  sapi::v::RemotePtr rdctx(dctx);

  SAPI_RETURN_IF_ERROR(api.GetSandbox()->TransferToSandboxee(&infd));
  SAPI_RETURN_IF_ERROR(api.GetSandbox()->TransferToSandboxee(&outfd));

  SAPI_ASSIGN_OR_RETURN(int iserr,
                        api.ZSTD_decompressStream_fd(&rdctx, infd.GetRemoteFd(),
                                                     outfd.GetRemoteFd()));
  if (iserr) {
    return absl::UnavailableError("Unable to decompress");
  }
  infd.CloseRemoteFd(api.GetSandbox()->rpc_channel()).IgnoreError();
  outfd.CloseRemoteFd(api.GetSandbox()->rpc_channel()).IgnoreError();

  return absl::OkStatus();
}
