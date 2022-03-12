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

#include "contrib/libspng/utils/utils_libspng.h"

#include <fstream>
#include <iostream>
#include <string>

#include "absl/cleanup/cleanup.h"

absl::Status LibSPng::InitStruct(int flags) {
  spng_ctx* pngctx;

  SAPI_ASSIGN_OR_RETURN(pngctx, api_.spng_ctx_new(flags));

  context_.SetRemote(pngctx);

  return absl::OkStatus();
}

LibSPng::~LibSPng() {
  Close();
}

void LibSPng::Close() {
  if (context_.GetRemote() != nullptr) {
    api_.spng_ctx_free(context_.PtrNone()).IgnoreError();
  }
  if (bufptr_.GetRemote() != nullptr) {
    sandbox_->Free(&bufptr_).IgnoreError();
  }
  if (pfile_.GetRemote() != nullptr) {
    api_.sapi_fclose(pfile_.PtrNone()).IgnoreError();
  }

  context_.SetRemote(nullptr);
  bufptr_.SetRemote(nullptr);
  pfile_.SetRemote(nullptr);
}

bool LibSPng::IsInit() { return status_ == absl::OkStatus(); }

absl::Status LibSPng::CheckInit() {
  if (!IsInit()) {
    return absl::UnavailableError("Library not initialized");
  }

  return absl::OkStatus();
}

absl::Status LibSPng::CheckTransfered() {
  if (bufptr_.GetRemote() != nullptr) {
    return absl::UnavailableError("Unable to transfer data twice");
  }

  return absl::OkStatus();
}

absl::Status LibSPng::SetBuffer(std::vector<uint8_t>& buf) {
  SAPI_RETURN_IF_ERROR(CheckInit());
  // We don't need to handle double decode as libspng
  // don't support that with single context.
  SAPI_RETURN_IF_ERROR(CheckTransfered());

  sapi::v::Array<uint8_t> sbuf(buf.data(), buf.size());

  SAPI_ASSIGN_OR_RETURN(
      int ret, api_.spng_set_png_buffer(context_.PtrNone(), sbuf.PtrBefore(),
                                        sbuf.GetSize()));
  if (ret != 0) {
    return RetError("Unable to set buffer", ret);
  }

  // We can free buffer only when class will be destroyed.
  bufptr_.SetRemote(sbuf.GetRemote());
  sbuf.SetRemote(nullptr);

  return absl::OkStatus();
}

absl::StatusOr<size_t> LibSPng::GetDecodeSize(enum spng_format fmt) {
  SAPI_RETURN_IF_ERROR(CheckInit());

  sapi::v::IntBase<size_t> out_size;

  SAPI_ASSIGN_OR_RETURN(int ret,
                        api_.spng_decoded_image_size(context_.PtrNone(), fmt,
                                                     out_size.PtrAfter()));
  if (ret != 0) {
    return RetError("Unable to decode size", ret);
  }

  return out_size.GetValue();
}

absl::StatusOr<std::vector<uint8_t>> LibSPng::Decode(
    enum spng_format fmt, enum spng_decode_flags flags) {
  SAPI_RETURN_IF_ERROR(CheckInit());

  decode_eof_ = false;
  if ((flags & SPNG_DECODE_PROGRESSIVE) != 0) {
    return DecodeProgressive(fmt, flags);
  }

  return DecodeStandard(fmt, flags);
}

absl::StatusOr<std::vector<uint8_t>> LibSPng::DecodeProgressive(
    enum spng_format fmt, enum spng_decode_flags flags) {
  SAPI_RETURN_IF_ERROR(CheckInit());

  SAPI_ASSIGN_OR_RETURN(
      int ret,
      api_.spng_decode_image(context_.PtrNone(), &null_ptr_, 0, fmt, flags));
  if (ret != 0) {
    return RetError("Unable to decode image", ret);
  }

  return std::vector<uint8_t>(0);
}

absl::StatusOr<std::vector<uint8_t>> LibSPng::DecodeStandard(
    enum spng_format fmt, enum spng_decode_flags flags) {
  SAPI_RETURN_IF_ERROR(CheckInit());
  SAPI_ASSIGN_OR_RETURN(size_t out_size, GetDecodeSize(fmt));
  if (out_size > kMaxBuf) {
    return absl::UnavailableError("Decoded image to large");
  }

  std::vector<uint8_t> out_buf(out_size);
  sapi::v::Array<uint8_t> sapi_buf(out_buf.data(), out_buf.size());

  SAPI_ASSIGN_OR_RETURN(
      int ret, api_.spng_decode_image(context_.PtrNone(), sapi_buf.PtrAfter(),
                                      sapi_buf.GetSize(), fmt, flags));
  if (ret != 0) {
    return RetError("Unable to decode image", ret);
  }

  return out_buf;
}

absl::StatusOr<struct spng_row_info> LibSPng::GetRowInfo() {
  SAPI_RETURN_IF_ERROR(CheckInit());
  sapi::v::Struct<struct spng_row_info> sapi_row_info;

  SAPI_ASSIGN_OR_RETURN(
      int ret,
      api_.spng_get_row_info(context_.PtrNone(), sapi_row_info.PtrAfter()));
  if (ret == SPNG_EOI) {
    decode_eof_ = true;  // It's returns error and data...
  } else if (ret != 0) {
    return RetError("Unable to get row info", ret);
  }

  return *(sapi_row_info.mutable_data());
}

absl::StatusOr<std::vector<uint8_t>> LibSPng::DecodeRow(size_t row_size) {
  SAPI_RETURN_IF_ERROR(CheckInit());
  if (row_size > kMaxBuf) {
    return absl::UnavailableError("Row is to large");
  }

  std::vector<uint8_t> buf_out(row_size);
  sapi::v::Array<uint8_t> sapi_buf(buf_out.data(), buf_out.size());

  SAPI_ASSIGN_OR_RETURN(
      int ret, api_.spng_decode_row(context_.PtrNone(), sapi_buf.PtrAfter(),
                                    sapi_buf.GetSize()));
  if (ret == SPNG_EOI) {
    decode_eof_ = true;  // It's returns error and data...
  } else if (ret != 0) {
    return RetError("Unable to get decode row", ret);
  }

  return buf_out;
}

bool LibSPng::DecodeEOF() { return decode_eof_; }

absl::StatusOr<struct spng_ihdr> LibSPng::GetIHdr() {
  SAPI_RETURN_IF_ERROR(CheckInit());

  sapi::v::Struct<struct spng_ihdr> ihdr;

  SAPI_ASSIGN_OR_RETURN(
      int ret, api_.spng_get_ihdr(context_.PtrNone(), ihdr.PtrAfter()));
  if (ret != 0) {
    return RetError("Unable to get ihdr", ret);
  }

  return *(ihdr.mutable_data());
}

absl::Status LibSPng::SetIHdr(struct spng_ihdr ihdr) {
  SAPI_RETURN_IF_ERROR(CheckInit());

  sapi::v::Struct<struct spng_ihdr> sapi_ihdr;
  memcpy(sapi_ihdr.mutable_data(), &ihdr, sizeof(ihdr));

  SAPI_ASSIGN_OR_RETURN(
      int ret, api_.spng_set_ihdr(context_.PtrNone(), sapi_ihdr.PtrBefore()));
  if (ret != 0) {
    return RetError("Unable to set ihdr", ret);
  }

  return absl::OkStatus();
}

absl::StatusOr<std::pair<uint32_t, uint32_t>> LibSPng::GetImageSize() {
  SAPI_RETURN_IF_ERROR(CheckInit());

  SAPI_ASSIGN_OR_RETURN(struct spng_ihdr ihdr, GetIHdr());

  return std::make_pair(ihdr.width, ihdr.height);
}

absl::StatusOr<uint8_t> LibSPng::GetImageBitDepth() {
  SAPI_RETURN_IF_ERROR(CheckInit());

  SAPI_ASSIGN_OR_RETURN(struct spng_ihdr ihdr, GetIHdr());

  return ihdr.bit_depth;
}

absl::Status LibSPng::SetOption(enum spng_option option, int value) {
  SAPI_RETURN_IF_ERROR(CheckInit());

  SAPI_ASSIGN_OR_RETURN(
      int ret, api_.spng_set_option(context_.PtrNone(), option, value));
  if (ret != 0) {
    return RetError("Unable to set option", ret);
  }

  return absl::OkStatus();
}

absl::StatusOr<int> LibSPng::GetOption(enum spng_option option) {
  SAPI_RETURN_IF_ERROR(CheckInit());

  sapi::v::Int value;

  SAPI_ASSIGN_OR_RETURN(
      int ret,
      api_.spng_get_option(context_.PtrNone(), option, value.PtrAfter()));
  if (ret != 0) {
    return RetError("Unable to get option", ret);
  }

  return value.GetValue();
}

absl::Status LibSPng::Encode(std::vector<uint8_t>& buf, int fmt, int flags) {
  SAPI_RETURN_IF_ERROR(CheckInit());

  if ((flags & SPNG_ENCODE_PROGRESSIVE) != 0) {
    return EncodeProgressive(fmt, flags);
  }

  return EncodeStandard(buf, fmt, flags);
}

absl::Status LibSPng::EncodeProgressive(int fmt, int flags) {
  SAPI_RETURN_IF_ERROR(CheckInit());

  SAPI_ASSIGN_OR_RETURN(
      int ret, api_.spng_encode_image(context_.PtrNone(), &null_ptr_,
                                      0, fmt, flags));
  if (ret != 0) {
    return RetError("Unable to encode image progressive", ret);
  }

  return absl::OkStatus();
}

absl::Status LibSPng::EncodeStandard(std::vector<uint8_t>& buf, int fmt, int flags) {
  SAPI_RETURN_IF_ERROR(CheckInit());

  sapi::v::Array<uint8_t> sapi_buf(buf.data(), buf.size());

  SAPI_ASSIGN_OR_RETURN(
      int ret, api_.spng_encode_image(context_.PtrNone(), sapi_buf.PtrBefore(),
                                      sapi_buf.GetSize(), fmt, flags));
  if (ret != 0) {
    return RetError("Unable to encode image", ret);
  }

  return absl::OkStatus();
}

absl::Status LibSPng::EncodeRow(std::vector<uint8_t>& buf) {
  SAPI_RETURN_IF_ERROR(CheckInit());

  sapi::v::Array<uint8_t> sapi_buf(buf.data(), buf.size());

  SAPI_ASSIGN_OR_RETURN(
    int ret, api_.spng_encode_row(context_.PtrNone(), sapi_buf.PtrBefore(),
                                  sapi_buf.GetSize()));
  if (ret != 0 && ret != SPNG_EOI) {
    return RetError("Unable to encode row", ret);
  }

  return absl::OkStatus();
}

absl::StatusOr<std::vector<uint8_t>> LibSPng::GetPngBuffer() {
  SAPI_RETURN_IF_ERROR(CheckInit());

  sapi::v::Int length, error;

  SAPI_ASSIGN_OR_RETURN(
      void* r_ptr_buffer,
      api_.spng_get_png_buffer(context_.PtrNone(), length.PtrAfter(),
                               error.PtrAfter()));
  if (r_ptr_buffer == nullptr) {
    return RetError("Unable to encode image", error.GetValue());
  }
  absl::Cleanup r_buffer_cleanup = [this, r_ptr_buffer] {
    sapi::v::RemotePtr rptr(r_ptr_buffer);
    sandbox_->Free(&rptr).IgnoreError();
  };

  if (length.GetValue() > kMaxBuf) {
    return absl::UnavailableError("Buffer to large");
  }

  std::vector<uint8_t> buf_out(length.GetValue());
  sapi::v::Array<uint8_t> sapi_buf(buf_out.data(), buf_out.size());
  sapi_buf.SetRemote(r_ptr_buffer);

  SAPI_RETURN_IF_ERROR(sandbox_->TransferFromSandboxee(&sapi_buf));
  return buf_out;
}

absl::Status LibSPng::RetError(const std::string& str, int ret) {
  return absl::UnavailableError(absl::StrCat(str, ": ", GetError(ret)));
}

absl::Status LibSPng::SetFd(int fd, const std::string& mode) {
  SAPI_RETURN_IF_ERROR(CheckInit());
  sapi::v::Fd infd(fd);
  sapi::v::ConstCStr sapi_mode(mode.c_str());

  SAPI_RETURN_IF_ERROR(sandbox_->TransferToSandboxee(&infd));

  SAPI_ASSIGN_OR_RETURN(void *pfile,
    api_.sapi_fdopen(infd.GetRemoteFd(), sapi_mode.PtrBefore()));
  if (pfile == NULL) {
    return absl::UnavailableError("Unable to fdopen");
  }

  // Don't close automaticlly remote fd
  infd.SetRemoteFd(-1);

  sapi::v::RemotePtr sapi_pfile(pfile);
  absl::StatusOr<int> status = api_.spng_set_png_file(context_.PtrNone(),
                                                      &sapi_pfile);
  if (!status.ok()) {
    api_.sapi_fclose(&sapi_pfile).IgnoreError();
    return status.status();
  }
  if (*status != 0) {
    api_.sapi_fclose(&sapi_pfile).IgnoreError();
    return RetError("Unable to set file", *status);
  }

  pfile_.SetRemote(pfile);

  return absl::OkStatus();
}

std::string LibSPng::GetError(int err) {
  absl::StatusOr<char*> ptr_val = api_.spng_strerror(err);
  if (!ptr_val.ok()) {
    return "Unable to get error details";
  }

  sapi::v::RemotePtr remote(*ptr_val);
  absl::StatusOr<std::string> str = sandbox_->GetCString(remote);
  if (!str.ok()) {
    return "Unable to fetch error details";
  }

  return *str;
}
