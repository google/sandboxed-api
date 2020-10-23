#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#define PNG_DEBUG 3
#include <png.h>

struct Data {
//  int x, y;
  int width;
  int height;
  uint8_t color_type;
  uint8_t bit_depth;

  png_structp png_ptr;
  png_infop info_ptr;
  int number_of_passes;
  png_bytep * row_pointers;
}

absl::Status ReadPng(LibPNGApi& api, absl::string_view infile) {
  char header[8];    // 8 is the maximum size that can be checked

  /* open file and test for it being a png */
  absl::StaturOr<FILE*> status_or_file;
  sapi::v::ConstCStr srcfile_var(infile.c_str());
  sapi::v::ConstCStr rb_var("rb");

  SAPI_ASSIGN_OR_RETURN(
      status_or_file, api.fopen(srcfile_var.PtrBefore(), rb_var.PtrBefore()));

  sapi::v::RemotePtr file(status_or_file.value());
  if (!file.GetValue()) {
    return absl::InternalError(absl::StrCat("Could not open ", srcfile));
  }

  sapi::v::Array<char> header(8);
  SAPI_RETURN_IF_ERROR(api.fread(header.PtrBoth(), 1, header.GetSize(), &file));

  SAPI_ASSIGN_OR_RETURN(
    int return_value,
    api.png_sig_cmp(header.PtrBoth(), 0, header.GetSize()));
  if (return_value != 0) {
    return absl::InternalError(absl::StrCat(infile, " is not a PNG file"));
  }

  absl::StatusOr<png_structp> status_or_png_structp;
  sapi::v::ConstCStr ver_string_var(PNG_LIBPNG_VER_STRING);
  SAPI_ASSIGN_OR_RETURN(
    status_or_png_structp,
    api.png_create_read_struct(ver_string_var.PtrBefore(), sapi::v::NullPtr().PtrBoth(),
      sapi::v::NullPtr().PtrBoth(), sapi::v::NullPtr().PtrBoth()));

  sapi::v::RemotePtr struct_ptr(status_or_png_structp.value());
  if (!struct_ptr.GetValue()) {
    return absl::InternalError("png_create_read_struct failed");
  }


  absl::StatusOr<png_infop> status_or_png_infop;
  sapi::v::ConstCStr ver_string_var(PNG_LIBPNG_VER_STRING);
  SAPI_ASSIGN_OR_RETURN(
    status_or_png_infop,
    api.png_create_info_struct(&struct_ptr));

  sapi::v::RemotePtr info_ptr(status_or_png_infop.value());
  if (!info_ptr.GetValue()) {
    return absl::InternalError("png_create_info_struct failed");
  }

  /// what is it?
  if (setjmp(png_jmpbuf(struct_ptr)))
          abort_("[read_png_file] Error during init_io");

  SAPI_RETURN_IF_ERROR(api.png_init_io(&struct_ptr, &file));
  SAPI_RETURN_IF_ERROR(api.png_set_sig_bytes(&struct_ptr, header.GetSize()));

  SAPI_RETURN_IF_ERROR(api.png_read_info(&struct_ptr, &info_ptr));

  SAPI_ASSIGN_OR_RETURN(
    int width,
    api.png_get_image_width(&struct_ptr, &info_ptr));

  SAPI_ASSIGN_OR_RETURN(
    int height,
    api.png_get_image_height(&struct_ptr, &info_ptr));

  SAPI_ASSIGN_OR_RETURN(
    uint8_t color_type,
    api.png_get_color_type(&struct_ptr, &info_ptr));

  SAPI_ASSIGN_OR_RETURN(
    uint8_t bit_depth,
    api.png_get_bit_depth(&struct_ptr, &info_ptr));

  SAPI_ASSIGN_OR_RETURN(
    int number_of_passes,
    api.png_set_interlace_handling(&struct_ptr));

  SAPI_RETURN_IF_ERROR(api.png_read_update_info(&struct_ptr, &info_ptr));

  /* read file */
  if (setjmp(png_jmpbuf(struct_ptr)))
          abort_("[read_png_file] Error during read_image");

  row_pointers = (png_bytep*) malloc(sizeof(png_bytep) * height);
  for (y=0; y<height; y++)
          row_pointers[y] = (png_byte*) malloc(png_get_rowbytes(struct_ptr,info_ptr));

  png_read_image(struct_ptr, row_pointers);

  fclose(fp);
}


absl::Status WritePng(LibPNGApi& api, absl::string_view outfile) {
  /* create file */
  FILE *fp = fopen(file_name, "wb");
  if (!fp)
          abort_("[write_png_file] File %s could not be opened for writing", file_name);


  /* initialize stuff */
  png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

  if (!png_ptr)
          abort_("[write_png_file] png_create_write_struct failed");

  info_ptr = png_create_info_struct(png_ptr);
  if (!info_ptr)
          abort_("[write_png_file] png_create_info_struct failed");

  if (setjmp(png_jmpbuf(png_ptr)))
          abort_("[write_png_file] Error during init_io");

  png_init_io(png_ptr, fp);


  /* write header */
  if (setjmp(png_jmpbuf(png_ptr)))
          abort_("[write_png_file] Error during writing header");

  png_set_IHDR(png_ptr, info_ptr, width, height,
                bit_depth, color_type, PNG_INTERLACE_NONE,
                PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

  png_write_info(png_ptr, info_ptr);


  /* write bytes */
  if (setjmp(png_jmpbuf(png_ptr)))
          abort_("[write_png_file] Error during writing bytes");

  png_write_image(png_ptr, row_pointers);


  /* end write */
  if (setjmp(png_jmpbuf(png_ptr)))
          abort_("[write_png_file] Error during end of write");

  png_write_end(png_ptr, NULL);

  /* cleanup heap allocation */
  for (y=0; y<height; y++)
          free(row_pointers[y]);
  free(row_pointers);

  fclose(fp);
}


absl::Status LibPNGMain(const std::string& infile, const std::string& outfile) {
  LibPNGSapiSandbox sandbox;
  sandbox.AddFile(infile);
  sandbox.AddFile(outfile);

  SAPI_RETURN_IF_ERROR(sandbox.Init());
  LibPNGApi api(&sandbox);

  int x, y;
  int width, height;
  png_byte color_type;
  png_byte bit_depth;

  png_structp png_ptr;
  png_infop info_ptr;
  int number_of_passes;
  png_bytep * row_pointers;

  SAPI_RETURN_IF_ERROR(ReadPng(api, infile));
  if (png_get_color_type(png_ptr, info_ptr) == PNG_COLOR_TYPE_RGB)
          abort_("[process_file] input file is PNG_COLOR_TYPE_RGB but must be PNG_COLOR_TYPE_RGBA "
                  "(lacks the alpha channel)");

  if (png_get_color_type(png_ptr, info_ptr) != PNG_COLOR_TYPE_RGBA)
          abort_("[process_file] color_type of input file must be PNG_COLOR_TYPE_RGBA (%d) (is %d)",
                  PNG_COLOR_TYPE_RGBA, png_get_color_type(png_ptr, info_ptr));

  for (y=0; y<height; y++) {
    png_byte* row = row_pointers[y];
    for (x=0; x<width; x++) {
      png_byte* ptr = &(row[x*4]);
      printf("Pixel at position [ %d - %d ] has RGBA values: %d - %d - %d - %d\n",
              x, y, ptr[0], ptr[1], ptr[2], ptr[3]);

      /* set red value to 0 and green value to the blue one */
      ptr[0] = 0;
      ptr[1] = ptr[2];
    }
  }
  SAPI_RETURN_IF_ERROR(WritePng(api, outfile));
}


int main(int argc, const char **argv) {
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
