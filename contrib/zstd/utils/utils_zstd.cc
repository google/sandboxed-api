// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Google LLC
//                Mariusz Zaborski <oshogbo@invisiblethingslab.com>

#include <fstream>
#include <iostream>
#include <string>

#include "../sandboxed.h"

std::streamsize getStreamSize(std::ifstream& stream) {
  stream.seekg(0, std::ios_base::end);
  std::streamsize ssize = stream.tellg();
  stream.seekg(0, std::ios_base::beg);

  return ssize;
}

absl::Status CompressInMemory(ZstdApi& api, std::ifstream& inFile,
                              std::ofstream& outFile, int level) {
  std::streamsize ssize = getStreamSize(inFile);
  sapi::v::Array<uint8_t> inBuf(ssize);
  inFile.read((char*)inBuf.GetData(), ssize);
  if (inFile.gcount() != ssize) {
    return absl::UnavailableError("Unable to read file");
  }

  SAPI_ASSIGN_OR_RETURN(size_t size, api.ZSTD_compressBound(inBuf.GetSize()));
  sapi::v::Array<uint8_t> outBuf(size);

  SAPI_ASSIGN_OR_RETURN(
      size_t outSize,
      api.ZSTD_compress(outBuf.PtrAfter(), size, inBuf.PtrBefore(),
                        inBuf.GetSize(), level));
  SAPI_ASSIGN_OR_RETURN(int iserr, api.ZSTD_isError(outSize))
  if (iserr) {
    return absl::UnavailableError("Unable to compress file");
  }

  outFile.write((char*)outBuf.GetData(), outSize);
  if (!outFile.good()) {
    return absl::UnavailableError("Unable to write file");
  }

  return absl::OkStatus();
}

absl::Status DecompressInMemory(ZstdApi& api, std::ifstream& inFile,
                                std::ofstream& outFile) {
  int iserr;
  std::streamsize ssize = getStreamSize(inFile);
  sapi::v::Array<uint8_t> inBuf(ssize);
  inFile.read((char*)inBuf.GetData(), ssize);
  if (inFile.gcount() != ssize) {
    return absl::UnavailableError("Unable to read file");
  }

  SAPI_ASSIGN_OR_RETURN(size_t size, api.ZSTD_getFrameContentSize(
                                         inBuf.PtrBefore(), inBuf.GetSize()));
  SAPI_ASSIGN_OR_RETURN(iserr, api.ZSTD_isError(size));
  if (iserr) {
    return absl::UnavailableError("Unable to decompress file");
  }
  sapi::v::Array<uint8_t> outBuf(size);

  SAPI_ASSIGN_OR_RETURN(
      size_t deSize, api.ZSTD_decompress(outBuf.PtrAfter(), size,
                                         inBuf.PtrBefore(), inBuf.GetSize()));
  SAPI_ASSIGN_OR_RETURN(iserr, api.ZSTD_isError(deSize));
  if (iserr) {
    return absl::UnavailableError("Unable to decompress file");
  }

  outFile.write((char*)outBuf.GetData(), outBuf.GetSize());
  if (!outFile.good()) {
    return absl::UnavailableError("Unable to write file");
  }

  return absl::OkStatus();
}

absl::Status CompressStream(ZstdApi& api, std::ifstream& inFile,
                            std::ofstream& outFile, int level) {
  int iserr;

  /* Create necessary buffers. */
  SAPI_ASSIGN_OR_RETURN(size_t bufInSize, api.ZSTD_CStreamInSize());
  SAPI_ASSIGN_OR_RETURN(size_t bufOutSize, api.ZSTD_CStreamOutSize());
  sapi::v::Array<uint8_t> inBuf(bufInSize);
  sapi::v::Array<uint8_t> outBuf(bufOutSize);

  if (!api.GetSandbox()->Allocate(&inBuf).ok() ||
      !api.GetSandbox()->Allocate(&outBuf).ok()) {
    return absl::UnavailableError("Unable to allocate buffors");
  }

  /* Create Zstd context. */
  SAPI_ASSIGN_OR_RETURN(ZSTD_DCtx * dctx, api.ZSTD_createDCtx());
  sapi::v::RemotePtr rdctx(dctx);

  SAPI_ASSIGN_OR_RETURN(iserr, api.ZSTD_CCtx_setParameter(
                                   &rdctx, ZSTD_c_compressionLevel, level));
  if (!iserr) {
    return absl::UnavailableError("Unable to set parameter");
  }
  SAPI_ASSIGN_OR_RETURN(
      iserr, api.ZSTD_CCtx_setParameter(&rdctx, ZSTD_c_checksumFlag, 1));
  if (!iserr) {
    return absl::UnavailableError("Unable to set parameter");
  }

  /* Compress. */
  while (inFile) {
    inFile.read((char*)inBuf.GetData(), bufInSize);

    if (!api.GetSandbox()->TransferToSandboxee(&inBuf).ok()) {
      return absl::UnavailableError("Unable to transfer data");
    }

    sapi::v::Struct<ZSTD_inBuffer_s> structIn;
    structIn.mutable_data()->src = static_cast<uint8_t*>(inBuf.GetRemote());
    structIn.mutable_data()->pos = 0;
    structIn.mutable_data()->size = inFile.gcount();

    ZSTD_EndDirective mode = ZSTD_e_continue;
    if (inFile.gcount() < bufInSize) {
      mode = ZSTD_e_end;
    }

    bool isDone = false;
    while (!isDone) {
      sapi::v::Struct<ZSTD_outBuffer_s> structOut;
      structOut.mutable_data()->dst = static_cast<uint8_t*>(outBuf.GetRemote());
      structOut.mutable_data()->pos = 0;
      structOut.mutable_data()->size = outBuf.GetSize();

      SAPI_ASSIGN_OR_RETURN(size_t remaining, api.ZSTD_compressStream2(
                                                  &rdctx, structOut.PtrBoth(),
                                                  structIn.PtrBoth(), mode));
      SAPI_ASSIGN_OR_RETURN(int iserr, api.ZSTD_isError(remaining))
      if (iserr) {
        return absl::UnavailableError("Unable to decompress file");
      }

      if (!api.GetSandbox()->TransferFromSandboxee(&outBuf).ok()) {
        return absl::UnavailableError("Unable to transfer data from");
      }
      outFile.write((char*)outBuf.GetData(), structOut.mutable_data()->pos);
      if (!outFile.good()) {
        return absl::UnavailableError("Unable to write file");
      }

      if (mode == ZSTD_e_continue) {
        isDone = (structIn.mutable_data()->pos == inFile.gcount());
      } else {
        isDone = (remaining == 0);
      }
    }
  }

  api.ZSTD_freeDCtx(&rdctx).IgnoreError();

  return absl::OkStatus();
}

absl::Status DecompressStream(ZstdApi& api, std::ifstream& inFile,
                              std::ofstream& outFile) {
  /* Create necessary buffers. */
  SAPI_ASSIGN_OR_RETURN(size_t bufInSize, api.ZSTD_CStreamInSize());
  SAPI_ASSIGN_OR_RETURN(size_t bufOutSize, api.ZSTD_CStreamOutSize());
  sapi::v::Array<uint8_t> inBuf(bufInSize);
  sapi::v::Array<uint8_t> outBuf(bufOutSize);

  if (!api.GetSandbox()->Allocate(&inBuf).ok() ||
      !api.GetSandbox()->Allocate(&outBuf).ok()) {
    return absl::UnavailableError("Unable to allocate buffors");
  }

  /* Create Zstd context. */
  SAPI_ASSIGN_OR_RETURN(ZSTD_DCtx * dctx, api.ZSTD_createDCtx());
  sapi::v::RemotePtr rdctx(dctx);

  /* Decompress. */
  while (inFile) {
    inFile.read((char*)inBuf.GetData(), bufInSize);

    if (!api.GetSandbox()->TransferToSandboxee(&inBuf).ok()) {
      return absl::UnavailableError("Unable to transfer data");
    }

    sapi::v::Struct<ZSTD_inBuffer_s> structIn;
    *structIn.mutable_data() = {static_cast<uint8_t*>(inBuf.GetRemote()),
                                (size_t)inFile.gcount(), 0};

    bool isDone = false;
    while (structIn.mutable_data()->pos < inFile.gcount()) {
      sapi::v::Struct<ZSTD_outBuffer_s> structOut;
      *structOut.mutable_data() = {static_cast<uint8_t*>(outBuf.GetRemote()),
                                   (size_t)outBuf.GetSize(), 0};

      SAPI_ASSIGN_OR_RETURN(
          size_t ret, api.ZSTD_decompressStream(&rdctx, structOut.PtrBoth(),
                                                structIn.PtrBoth()));
      SAPI_ASSIGN_OR_RETURN(int iserr, api.ZSTD_isError(ret))
      if (iserr) {
        return absl::UnavailableError("Unable to decompress file");
      }

      if (!api.GetSandbox()->TransferFromSandboxee(&outBuf).ok()) {
        return absl::UnavailableError("Unable to transfer data from");
      }

      outFile.write((char*)outBuf.GetData(), structOut.mutable_data()->pos);
      if (!outFile.good()) {
        return absl::UnavailableError("Unable to write file");
      }
    }
  }

  api.ZSTD_freeDCtx(&rdctx).IgnoreError();

  return absl::OkStatus();
}
