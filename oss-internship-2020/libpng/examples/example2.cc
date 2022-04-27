// Copyright 2020 Google LLC
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

#include <fcntl.h>
#include <unistd.h>

#include "../sandboxed.h"     // NOLINT(build/include)
#include "../tests/libpng.h"  // NOLINT(build/include)

struct Data {
  int width;
  int height;
  uint8_t color_type;
  uint8_t bit_depth;
  int number_of_passes;
  size_t rowbytes;
  std::unique_ptr<sapi::v::Array<uint8_t>> row_pointers;
};

absl::StatusOr<Data> ReadPng(LibPNGApi& api, absl::string_view infile) {
  sapi::v::Fd fd(open(infile.data(), O_RDONLY));

  if (fd.GetValue() < 0) {
    return absl::InternalError("Error opening input file");
  }

  SAPI_RETURN_IF_ERROR((&api)->sandbox()->TransferToSandboxee(&fd));

  if (fd.GetRemoteFd() < 0) {
    return absl::InternalError("Error receiving remote FD");
  }

  absl::StatusOr<void*> status_or_file;
  sapi::v::ConstCStr rb_var("rb");
  SAPI_ASSIGN_OR_RETURN(status_or_file,
                        api.png_fdopen(fd.GetRemoteFd(), rb_var.PtrBefore()));

  sapi::v::RemotePtr file(status_or_file.value());
  if (!file.GetValue()) {
    return absl::InternalError(absl::StrCat("Could not open ", infile));
  }

  sapi::v::Array<char> header(8);
  SAPI_RETURN_IF_ERROR(
      api.png_fread(header.PtrBoth(), 1, header.GetSize(), &file));

  SAPI_ASSIGN_OR_RETURN(int return_value,
                        api.png_sig_cmp(header.PtrBoth(), 0, header.GetSize()));
  if (return_value != 0) {
    return absl::InternalError(absl::StrCat(infile, " is not a PNG file"));
  }

  absl::StatusOr<png_structp> status_or_png_structp;
  sapi::v::ConstCStr ver_string_var(PNG_LIBPNG_VER_STRING);
  sapi::v::NullPtr null = sapi::v::NullPtr();
  SAPI_ASSIGN_OR_RETURN(
      status_or_png_structp,
      api.png_create_read_struct_wrapper(ver_string_var.PtrBefore(), &null));

  sapi::v::RemotePtr struct_ptr(status_or_png_structp.value());
  if (!struct_ptr.GetValue()) {
    return absl::InternalError("png_create_read_struct_wrapper failed");
  }

  absl::StatusOr<png_infop> status_or_png_infop;
  SAPI_ASSIGN_OR_RETURN(status_or_png_infop,
                        api.png_create_info_struct(&struct_ptr));

  sapi::v::RemotePtr info_ptr(status_or_png_infop.value());
  if (!info_ptr.GetValue()) {
    return absl::InternalError("png_create_info_struct failed");
  }

  SAPI_RETURN_IF_ERROR(api.png_setjmp(&struct_ptr));
  SAPI_RETURN_IF_ERROR(api.png_init_io_wrapper(&struct_ptr, &file));
  SAPI_RETURN_IF_ERROR(api.png_set_sig_bytes(&struct_ptr, header.GetSize()));
  SAPI_RETURN_IF_ERROR(api.png_read_info(&struct_ptr, &info_ptr));

  Data data;
  SAPI_ASSIGN_OR_RETURN(data.width,
                        api.png_get_image_width(&struct_ptr, &info_ptr));

  SAPI_ASSIGN_OR_RETURN(data.height,
                        api.png_get_image_height(&struct_ptr, &info_ptr));

  SAPI_ASSIGN_OR_RETURN(data.color_type,
                        api.png_get_color_type(&struct_ptr, &info_ptr));

  SAPI_ASSIGN_OR_RETURN(data.bit_depth,
                        api.png_get_bit_depth(&struct_ptr, &info_ptr));

  SAPI_ASSIGN_OR_RETURN(data.number_of_passes,
                        api.png_set_interlace_handling(&struct_ptr));

  SAPI_RETURN_IF_ERROR(api.png_read_update_info(&struct_ptr, &info_ptr));
  SAPI_RETURN_IF_ERROR(api.png_setjmp(&struct_ptr));

  SAPI_ASSIGN_OR_RETURN(data.rowbytes,
                        api.png_get_rowbytes(&struct_ptr, &info_ptr));
  data.row_pointers =
      std::make_unique<sapi::v::Array<uint8_t>>(data.height * data.rowbytes);

  SAPI_RETURN_IF_ERROR(api.png_read_image_wrapper(
      &struct_ptr, data.row_pointers->PtrAfter(), data.height, data.rowbytes));

  SAPI_RETURN_IF_ERROR(api.png_fclose(&file));
  return data;
}

absl::Status WritePng(LibPNGApi& api, absl::string_view outfile, Data& data) {
  sapi::v::Fd fd(open(outfile.data(), O_WRONLY));
  if (fd.GetValue() < 0) {
    return absl::InternalError("Error opening output file");
  }

  SAPI_RETURN_IF_ERROR((&api)->sandbox()->TransferToSandboxee(&fd));
  if (fd.GetRemoteFd() < 0) {
    return absl::InternalError("Error receiving remote FD");
  }

  absl::StatusOr<void*> status_or_file;
  sapi::v::ConstCStr wb_var("wb");
  SAPI_ASSIGN_OR_RETURN(status_or_file,
                        api.png_fdopen(fd.GetRemoteFd(), wb_var.PtrBefore()));

  sapi::v::RemotePtr file(status_or_file.value());
  if (!file.GetValue()) {
    return absl::InternalError(absl::StrCat("Could not open ", outfile));
  }

  absl::StatusOr<png_structp> status_or_png_structp;
  sapi::v::ConstCStr ver_string_var(PNG_LIBPNG_VER_STRING);
  sapi::v::NullPtr null = sapi::v::NullPtr();
  SAPI_ASSIGN_OR_RETURN(
      status_or_png_structp,
      api.png_create_write_struct_wrapper(ver_string_var.PtrBefore(), &null));

  sapi::v::RemotePtr struct_ptr(status_or_png_structp.value());
  if (!struct_ptr.GetValue()) {
    return absl::InternalError("png_create_write_struct_wrapper failed");
  }

  absl::StatusOr<png_infop> status_or_png_infop;
  SAPI_ASSIGN_OR_RETURN(status_or_png_infop,
                        api.png_create_info_struct(&struct_ptr));

  sapi::v::RemotePtr info_ptr(status_or_png_infop.value());
  if (!info_ptr.GetValue()) {
    return absl::InternalError("png_create_info_struct failed");
  }

  SAPI_RETURN_IF_ERROR(api.png_setjmp(&struct_ptr));
  SAPI_RETURN_IF_ERROR(api.png_init_io_wrapper(&struct_ptr, &file));

  SAPI_RETURN_IF_ERROR(api.png_setjmp(&struct_ptr));
  SAPI_RETURN_IF_ERROR(
      api.png_set_IHDR(&struct_ptr, &info_ptr, data.width, data.height,
                       data.bit_depth, data.color_type, PNG_INTERLACE_NONE,
                       PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE));

  SAPI_RETURN_IF_ERROR(api.png_write_info(&struct_ptr, &info_ptr));

  SAPI_RETURN_IF_ERROR(api.png_setjmp(&struct_ptr));
  SAPI_RETURN_IF_ERROR(api.png_write_image_wrapper(
      &struct_ptr, data.row_pointers->PtrBefore(), data.height, data.rowbytes));

  SAPI_RETURN_IF_ERROR(api.png_setjmp(&struct_ptr));
  SAPI_RETURN_IF_ERROR(api.png_write_end(&struct_ptr, &null));

  SAPI_RETURN_IF_ERROR(api.png_fclose(&file));
  return absl::OkStatus();
}

absl::Status LibPNGMain(const std::string& infile, const std::string& outfile) {
  LibPNGSapiSandbox sandbox;
  sandbox.AddFile(infile);
  sandbox.AddFile(outfile);

  SAPI_RETURN_IF_ERROR(sandbox.Init());
  LibPNGApi api(&sandbox);

  SAPI_ASSIGN_OR_RETURN(Data data, ReadPng(api, infile));

  if (data.color_type != PNG_COLOR_TYPE_RGBA &&
      data.color_type != PNG_COLOR_TYPE_RGB) {
    return absl::InternalError(absl::StrCat(
        infile, " has unexpected color type. Expected RGB or RGBA"));
  }

  size_t channel_count = 3;
  if (data.color_type == PNG_COLOR_TYPE_RGBA) {
    channel_count = 4;
  }

  // RGB to BGR
  for (size_t i = 0; i != data.height; ++i) {
    for (size_t j = 0; j != data.width; ++j) {
      uint8_t r = (*data.row_pointers)[i * data.rowbytes + j * channel_count];
      uint8_t g =
          (*data.row_pointers)[i * data.rowbytes + j * channel_count + 1];
      uint8_t b =
          (*data.row_pointers)[i * data.rowbytes + j * channel_count + 2];
      (*data.row_pointers)[i * data.rowbytes + j * channel_count] = b;
      (*data.row_pointers)[i * data.rowbytes + j * channel_count + 2] = r;
    }
  }

  SAPI_RETURN_IF_ERROR(WritePng(api, outfile, data));
  return absl::OkStatus();
}

int main(int argc, char* argv[]) {
  if (argc != 3) {
    LOG(ERROR) << "Usage: example5 infile outfile";
    return EXIT_FAILURE;
  }

  auto status = LibPNGMain(argv[1], argv[2]);
  if (!status.ok()) {
    LOG(ERROR) << "LibPNGMain failed with error:\n"
               << status.ToString() << '\n';
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
