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

#include "contrib/brotli/utils/utils_brotli_dec.h"

#include <fstream>
#include <string>

#include "contrib/brotli/sandboxed.h"
#include "contrib/brotli/utils/utils_brotli.h"

absl::Status BrotliDecoder::InitStructs() {
  SAPI_ASSIGN_OR_RETURN(
      BrotliDecoderState * state,
      api_.BrotliDecoderCreateInstance(&null_ptr_, &null_ptr_, &null_ptr_));

  state_.SetRemote(state);

  return absl::OkStatus();
}

BrotliDecoder::~BrotliDecoder() {
  if (state_.GetRemote() != nullptr) {
    api_.BrotliDecoderDestroyInstance(state_.PtrNone()).IgnoreError();
  }
}

bool BrotliDecoder::IsInit() {
  if (state_.GetRemote() == nullptr) {
    return false;
  }

  return true;
}

absl::Status BrotliDecoder::CheckIsInit() {
  if (!IsInit()) {
    return absl::UnavailableError("The decoder is not initialized");
  }

  return absl::OkStatus();
}

absl::Status BrotliDecoder::SetParameter(enum BrotliDecoderParameter param,
                                         uint32_t value) {
  SAPI_RETURN_IF_ERROR(CheckIsInit());

  SAPI_ASSIGN_OR_RETURN(
      int ret, api_.BrotliDecoderSetParameter(state_.PtrNone(), param, value));
  if (!ret) {
    return absl::UnavailableError("Unable to set parameter");
  }

  return absl::OkStatus();
}

absl::StatusOr<BrotliDecoderResult> BrotliDecoder::Decompress(
    std::vector<uint8_t>& buf_in) {
  SAPI_RETURN_IF_ERROR(CheckIsInit());

  sapi::v::Array<uint8_t> sapi_buf_in(buf_in.data(), buf_in.size());
  sapi::v::IntBase<size_t> sapi_size_in(buf_in.size());

  // BrotliDecoderCompress requires a pointer to a pointer,
  // as function moves to pointer to indicate how much data
  // was compressed.
  // In this case we compress whole buffer so we don't use it
  // but we still have to allocate buffer remotely and gets
  // a pointer.
  SAPI_RETURN_IF_ERROR(sandbox_->Allocate(&sapi_buf_in));
  SAPI_RETURN_IF_ERROR(sandbox_->TransferToSandboxee(&sapi_buf_in));
  sapi::v::GenericPtr sapi_opaque_buf_in(sapi_buf_in.GetRemote());

  sapi::v::IntBase<size_t> sapi_avilable_out(0);

  SAPI_ASSIGN_OR_RETURN(
      BrotliDecoderResult ret,
      api_.BrotliDecoderDecompressStream(
          state_.PtrNone(), sapi_size_in.PtrBefore(),
          sapi_opaque_buf_in.PtrBefore(), sapi_avilable_out.PtrBefore(),
          &null_ptr_, &null_ptr_));

  // Ignore output error, as we didn't provide any buffer.
  if (ret == BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT) {
    ret = BROTLI_DECODER_RESULT_SUCCESS;
  }

  return ret;
}

absl::StatusOr<std::vector<uint8_t>> BrotliDecoder::TakeOutput() {
  SAPI_RETURN_IF_ERROR(CheckIsInit());

  sapi::v::IntBase<size_t> sapi_size_out(0);

  SAPI_ASSIGN_OR_RETURN(
      uint8_t * sapi_out_buf_ptr,
      api_.BrotliDecoderTakeOutput(state_.PtrNone(), sapi_size_out.PtrAfter()));
  if (sapi_out_buf_ptr == nullptr || sapi_size_out.GetValue() == 0) {
    return std::vector<uint8_t>(0);
  }
  if (sapi_size_out.GetValue() > kFileMaxSize) {
    return absl::UnavailableError("Output to large");
  }

  std::vector<uint8_t> buf_out(sapi_size_out.GetValue());
  sapi::v::Array<uint8_t> sapi_buf_out(buf_out.data(), buf_out.size());
  sapi_buf_out.SetRemote(sapi_out_buf_ptr);

  SAPI_RETURN_IF_ERROR(sandbox_->TransferFromSandboxee(&sapi_buf_out));

  return buf_out;
}
