#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
//#include <png.h>
#include <zlib.h>

#include "sandboxed_api/sandbox2/util/fileops.h"
#include "sandboxed_api/sandbox2/util/path.h"
#include "sandboxed_api/vars.h"

#include "../sandboxed.h"

absl::Status LibPNGMain(const std::string& infile, const std::string& outfile) {
  LibPNGSapiSandbox sandbox;
  sandbox.AddFile(infile);
  sandbox.AddFile(outfile);

  SAPI_RETURN_IF_ERROR(sandbox.Init());
  LibPNGApi api(&sandbox);

  sapi::v::Struct<png_image> image;
  sapi::v::ConstCStr infile_var(infile.c_str());
  sapi::v::ConstCStr outfile_var(outfile.c_str());

  /* Initialize the 'png_image' structure. */
  // memset(&image, 0, (sizeof image));
  // may be it initializing with zeros?

  image.mutable_data()->version = PNG_IMAGE_VERSION;

  SAPI_ASSIGN_OR_RETURN(int result,
    api.png_image_begin_read_from_file(image.PtrBoth(), infile_var.PtrBefore()));
  if (!result) {
    return absl::InternalError(absl::StrCat("pngtopng: error: ", image.mutable_data()->message));
  }

  image.mutable_data()->format = PNG_FORMAT_RGBA;

  png_bytep buffer;
  buffer = malloc(PNG_IMAGE_SIZE(*image.mutable_data()));
  if (!buffer) {
    SAPI_RETURN_IF_ERROR(api.png_image_free(image.PtrBoth()));
    return absl::OkStatus();
  }

  free(buffer);
  sapi::v::Array<uint8_t> buffer_(PNG_IMAGE_SIZE(*image.mutable_data()));

  SAPI_ASSIGN_OR_RETURN(result,
    api.png_image_finish_read(image.PtrBoth(), sapi::v::NullPtr().PtrBoth(), buffer_.PtrBoth(), 0, sapi::v::NullPtr().PtrBoth()));
  if (!result) {
    return absl::InternalError(absl::StrCat("pngtopng: error: ", image.mutable_data()->message));
  }

  SAPI_ASSIGN_OR_RETURN(result,
    api.png_image_write_to_file(image.PtrBoth(), outfile_var.PtrBefore(), 0, buffer_.PtrBoth(), 0, sapi::v::NullPtr().PtrBoth()));
  if (!result) {
    return absl::InternalError(absl::StrCat("pngtopng: error: ", image.mutable_data()->message));
  }

  return absl::OkStatus();
}

int main(int argc, const char **argv) {
  if (argc != 3) {
    LOG_ERROR("pngtopng: usage: pngtopng input-file output-file");
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
