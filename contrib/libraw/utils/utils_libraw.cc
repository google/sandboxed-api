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

#include "contrib/libraw/utils/utils_libraw.h"

#include "absl/strings/str_cat.h"
#include "contrib/libraw/sandboxed.h"

absl::Status LibRaw::InitLibRaw() {
  SAPI_ASSIGN_OR_RETURN(libraw_data_t * lr_data, api_.libraw_init(0));

  sapi_libraw_data_t_.SetRemote(lr_data);
  SAPI_RETURN_IF_ERROR(sandbox_->TransferFromSandboxee(&sapi_libraw_data_t_));

  return absl::OkStatus();
}

LibRaw::~LibRaw() {
  if (sapi_libraw_data_t_.GetRemote() != nullptr) {
    api_.libraw_close(sapi_libraw_data_t_.PtrNone()).IgnoreError();
  }
}

absl::Status LibRaw::CheckIsInit() { return init_status_; }

bool LibRaw::IsInit() { return CheckIsInit().ok(); }

libraw_data_t LibRaw::GetImgData() { return sapi_libraw_data_t_.data(); }

absl::Status LibRaw::OpenFile() {
  SAPI_RETURN_IF_ERROR(CheckIsInit());

  sapi::v::CStr file_name(file_name_.c_str());

  SAPI_ASSIGN_OR_RETURN(int error_code,
                        api_.libraw_open_file(sapi_libraw_data_t_.PtrAfter(),
                                              file_name.PtrBefore()));

  if (error_code != LIBRAW_SUCCESS) {
    return absl::UnavailableError(
        absl::string_view(std::to_string(error_code)));
  }

  return absl::OkStatus();
}

absl::Status LibRaw::Unpack() {
  SAPI_RETURN_IF_ERROR(CheckIsInit());

  SAPI_ASSIGN_OR_RETURN(int error_code,
                        api_.libraw_unpack(sapi_libraw_data_t_.PtrAfter()));
  if (error_code != LIBRAW_SUCCESS) {
    return absl::UnavailableError(
        absl::string_view(std::to_string(error_code)));
  }

  return absl::OkStatus();
}

absl::Status LibRaw::SubtractBlack() {
  SAPI_RETURN_IF_ERROR(CheckIsInit());

  return api_.libraw_subtract_black(sapi_libraw_data_t_.PtrAfter());
}

absl::StatusOr<std::vector<char*>> LibRaw::GetCameraList() {
  SAPI_RETURN_IF_ERROR(CheckIsInit());

  int size;
  SAPI_ASSIGN_OR_RETURN(size, api_.libraw_cameraCount());

  std::vector<char*> buf(size);
  sapi::v::Array<char*> camera_list(buf.data(), buf.size());

  char** sapi_camera_list;
  SAPI_ASSIGN_OR_RETURN(sapi_camera_list, api_.libraw_cameraList());

  camera_list.SetRemote(sapi_camera_list);
  SAPI_RETURN_IF_ERROR(sandbox_->TransferFromSandboxee(&camera_list));

  return buf;
}

absl::StatusOr<int> LibRaw::COLOR(int row, int col) {
  SAPI_RETURN_IF_ERROR(CheckIsInit());

  int color;
  SAPI_ASSIGN_OR_RETURN(
      color, api_.libraw_COLOR(sapi_libraw_data_t_.PtrNone(), row, col));

  return color;
}

absl::StatusOr<int> LibRaw::GetRawHeight() {
  SAPI_RETURN_IF_ERROR(CheckIsInit());

  ushort height;
  SAPI_ASSIGN_OR_RETURN(
      height, api_.libraw_get_raw_height(sapi_libraw_data_t_.PtrNone()));

  return height;
}

absl::StatusOr<int> LibRaw::GetRawWidth() {
  SAPI_RETURN_IF_ERROR(CheckIsInit());

  ushort width;
  SAPI_ASSIGN_OR_RETURN(
      width, api_.libraw_get_raw_width(sapi_libraw_data_t_.PtrNone()));

  return width;
}

absl::StatusOr<unsigned int> LibRaw::GetCBlack(int channel) {
  SAPI_RETURN_IF_ERROR(CheckIsInit());

  if (channel < 0 || channel >= LIBRAW_CBLACK_SIZE) {
    return absl::OutOfRangeError(absl::StrCat(
        channel, " is out of range for array with size ", LIBRAW_CBLACK_SIZE));
  }

  return GetImgData().color.cblack[channel];

  ushort width;
  SAPI_ASSIGN_OR_RETURN(
      width, api_.libraw_get_raw_width(sapi_libraw_data_t_.PtrNone()));

  return width;
}

int LibRaw::GetColorCount() { return GetImgData().idata.colors; }

absl::StatusOr<std::vector<uint16_t>> LibRaw::RawData() {
  SAPI_RETURN_IF_ERROR(CheckIsInit());

  int raw_height;
  int raw_width;
  SAPI_ASSIGN_OR_RETURN(raw_height, GetRawHeight());
  SAPI_ASSIGN_OR_RETURN(raw_width, GetRawWidth());
  int size = raw_height * raw_width;
  std::vector<uint16_t> buf(size);
  sapi::v::Array<uint16_t> rawdata(buf.data(), buf.size());

  rawdata.SetRemote(sapi_libraw_data_t_.data().rawdata.raw_image);
  SAPI_RETURN_IF_ERROR(sandbox_->TransferFromSandboxee(&rawdata));

  return buf;
}
