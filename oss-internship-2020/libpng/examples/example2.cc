#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <iostream>

#include "../sandboxed.h"
#include "libpng.h"

struct Data {
  Data() {}

  int width;
  int height;
  uint8_t color_type;
  uint8_t bit_depth;
  int number_of_passes;
  size_t rowbytes;
  std::unique_ptr<sapi::v::Array<uint8_t>> row_pointers;
};

absl::Status ReadPng(LibPNGApi& api, LibPNGSapiSandbox& sandbox,
                     absl::string_view infile, Data& d) {
  sapi::v::Fd fd(open(infile.data(), O_RDONLY));

  if (fd.GetValue() < 0) {
    return absl::InternalError("Error opening input file");
  }

  SAPI_RETURN_IF_ERROR(sandbox.TransferToSandboxee(&fd));
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
  SAPI_ASSIGN_OR_RETURN(
      status_or_png_structp,
      api.png_create_read_struct_wrapper(ver_string_var.PtrBefore(),
                                         sapi::v::NullPtr().PtrBoth()));

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

  SAPI_ASSIGN_OR_RETURN(d.width,
                        api.png_get_image_width(&struct_ptr, &info_ptr));

  SAPI_ASSIGN_OR_RETURN(d.height,
                        api.png_get_image_height(&struct_ptr, &info_ptr));

  SAPI_ASSIGN_OR_RETURN(d.color_type,
                        api.png_get_color_type(&struct_ptr, &info_ptr));

  SAPI_ASSIGN_OR_RETURN(d.bit_depth,
                        api.png_get_bit_depth(&struct_ptr, &info_ptr));

  SAPI_ASSIGN_OR_RETURN(d.number_of_passes,
                        api.png_set_interlace_handling(&struct_ptr));

  SAPI_RETURN_IF_ERROR(api.png_read_update_info(&struct_ptr, &info_ptr));
  SAPI_RETURN_IF_ERROR(api.png_setjmp(&struct_ptr));

  SAPI_ASSIGN_OR_RETURN(d.rowbytes,
                        api.png_get_rowbytes(&struct_ptr, &info_ptr));
  d.row_pointers =
      std::make_unique<sapi::v::Array<uint8_t>>(d.height * d.rowbytes);

  SAPI_RETURN_IF_ERROR(api.png_read_image_wrapper(
      &struct_ptr, d.row_pointers->PtrAfter(), d.height, d.rowbytes));

  SAPI_RETURN_IF_ERROR(api.png_fclose(&file));
  return absl::OkStatus();
}

absl::Status WritePng(LibPNGApi& api, LibPNGSapiSandbox& sandbox,
                      absl::string_view outfile, Data& d) {
  sapi::v::Fd fd(open(outfile.data(), O_WRONLY));
  if (fd.GetValue() < 0) {
    return absl::InternalError("Error opening output file");
  }

  SAPI_RETURN_IF_ERROR(sandbox.TransferToSandboxee(&fd));
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
  SAPI_ASSIGN_OR_RETURN(
      status_or_png_structp,
      api.png_create_write_struct_wrapper(ver_string_var.PtrBefore(),
                                          sapi::v::NullPtr().PtrBoth()));

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
  SAPI_RETURN_IF_ERROR(api.png_set_IHDR(
      &struct_ptr, &info_ptr, d.width, d.height, d.bit_depth, d.color_type,
      PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE));

  SAPI_RETURN_IF_ERROR(api.png_write_info(&struct_ptr, &info_ptr));

  SAPI_RETURN_IF_ERROR(api.png_setjmp(&struct_ptr));
  SAPI_RETURN_IF_ERROR(api.png_write_image_wrapper(
      &struct_ptr, d.row_pointers->PtrBefore(), d.height, d.rowbytes));

  SAPI_RETURN_IF_ERROR(api.png_setjmp(&struct_ptr));
  SAPI_RETURN_IF_ERROR(api.png_write_end_wrapper(&struct_ptr));

  SAPI_RETURN_IF_ERROR(api.png_fclose(&file));
  return absl::OkStatus();
}

absl::Status LibPNGMain(const std::string& infile, const std::string& outfile) {
  //  sapi::v::Array<sapi::v::Array<uint8_t>> row_pointers;

  LibPNGSapiSandbox sandbox;
  sandbox.AddFile(infile);
  sandbox.AddFile(outfile);

  SAPI_RETURN_IF_ERROR(sandbox.Init());
  LibPNGApi api(&sandbox);

  Data d;
  SAPI_RETURN_IF_ERROR(ReadPng(api, sandbox, infile, d));

  if (d.color_type != PNG_COLOR_TYPE_RGBA &&
      d.color_type != PNG_COLOR_TYPE_RGB) {
    return absl::InternalError(absl::StrCat(
        infile, " has unexpected color type. Expected RGB or RGBA"));
  }

  size_t channel_count = 3;
  if (d.color_type == PNG_COLOR_TYPE_RGBA) {
    channel_count = 4;
  }

  // RGB to BGR
  for (size_t i = 0; i != d.height; ++i) {
    for (size_t j = 0; j != d.width; ++j) {
      uint8_t r = (*d.row_pointers)[i * d.rowbytes + j * channel_count];
      uint8_t g = (*d.row_pointers)[i * d.rowbytes + j * channel_count + 1];
      uint8_t b = (*d.row_pointers)[i * d.rowbytes + j * channel_count + 2];
      (*d.row_pointers)[i * d.rowbytes + j * channel_count] = b;
      (*d.row_pointers)[i * d.rowbytes + j * channel_count + 2] = r;
    }
  }

  SAPI_RETURN_IF_ERROR(WritePng(api, sandbox, outfile, d));
  return absl::OkStatus();
}

int main(int argc, const char** argv) {
  // RGB to BGR
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
