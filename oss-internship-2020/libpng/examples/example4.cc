#include <string>
#include <cstdio>
#include <iostream>
#include "../sandboxed.h"
#include "libpng.h"

struct Sprite {
  sapi::v::Ptr file;
  std::vector<png_uint_16> buffer; // think
  uint32_t width = 0;
  uint32_t height = 0;
  std::string name;
};

constexpr absl::string_view kOperationFormat = "%d,%d%c";
constexpr absl::string_view kAddFlag = "--add=";
constexpr absl::string_view kSpriteFlag = "--sprite=";
constexpr absl::string_view kSpriteFlagFormat = "%u,%u,%s%c";

// static void
// sprite_op(const struct Sprite *sprite, int x_offset, int y_offset,
//    png_imagep image, const png_uint_16 *buffer) {
//    /* This is where the Porter-Duff 'Over' operator is evaluated; change this
//     * code to change the operator (this could be parameterized).  Any other
//     * image processing operation could be used here.
//     */


//    /* Check for an x or y offset that pushes any part of the image beyond the
//     * right or bottom of the sprite:
//     */
//    if ((y_offset < 0 || (unsigned)/*SAFE*/y_offset < sprite->height) &&
//        (x_offset < 0 || (unsigned)/*SAFE*/x_offset < sprite->width))
//    {
//       unsigned int y = 0;

//       if (y_offset < 0)
//          y = -y_offset; /* Skip to first visible row */

//       do
//       {
//          unsigned int x = 0;

//          if (x_offset < 0)
//             x = -x_offset;

//          do
//          {
//             /* In and out are RGBA values, so: */
//             const png_uint_16 *in_pixel = buffer + (y * image->width + x)*4;
//             png_uint_32 in_alpha = in_pixel[3];

//             /* This is the optimized Porter-Duff 'Over' operation, when the
//              * input alpha is 0 the output is not changed.
//              */
//             if (in_alpha > 0)
//             {
//                png_uint_16 *out_pixel = sprite->buffer +
//                   ((y+y_offset) * sprite->width + (x+x_offset))*4;

//                /* This is the weight to apply to the output: */
//                in_alpha = 65535-in_alpha;

//                if (in_alpha > 0)
//                {
//                   /* The input must be composed onto the output. This means
//                    * multiplying the current output pixel value by the inverse
//                    * of the input alpha (1-alpha). A division is required but
//                    * it is by the constant 65535.  Approximate this as:
//                    *
//                    *     (x + (x >> 16) + 32769) >> 16;
//                    *
//                    * This is exact (and does not overflow) for all values of
//                    * x in the range 0..65535*65535.  (Note that the calculation
//                    * produces the closest integer; the maximum error is <0.5).
//                    */
//                   png_uint_32 tmp;

// #                 define compose(c)\
//                      tmp = out_pixel[c] * in_alpha;\
//                      tmp = (tmp + (tmp >> 16) + 32769) >> 16;\
//                      out_pixel[c] = tmp + in_pixel[c]

//                   /* The following is very vectorizable... */
//                   compose(0);
//                   compose(1);
//                   compose(2);
//                   compose(3);
//                }

//                else
//                   out_pixel[0] = in_pixel[0],
//                   out_pixel[1] = in_pixel[1],
//                   out_pixel[2] = in_pixel[2],
//                   out_pixel[3] = in_pixel[3];
//             }
//          }
//          while (++x < image->width);
//       }
//       while (++y < image->height);
//    }
// }

// static int CreateSprite(struct Sprite *sprite, int *argc, const char ***argv) {
//    /* Read the arguments and create this sprite. The Sprite buffer has already
//     * been allocated. This reads the input PNGs one by one in linear format,
//     * composes them onto the Sprite buffer (the code in the function above)
//     * then saves the result, converting it on the fly to PNG RGBA 8-bit format.
//     */
//    while (*argc > 0)
//    {
//       char tombstone;
//       int x = 0, y = 0;

//       if ((*argv)[0][0] == '-' && (*argv)[0][1] == '-')
//       {
//          /* The only supported option is --at. */
//          if (sscanf((*argv)[0], "--at=%d,%d%c", &x, &y, &tombstone) != 2)
//             break; /* success; caller will parse this option */

//          ++*argv, --*argc;
//       }

//       else
//       {
//          /* The argument has to be a file name */
//          png_image image;

//          image.version = PNG_IMAGE_VERSION;
//          image.opaque = NULL;

//          if (png_image_begin_read_from_file(&image, (*argv)[0]))
//          {
//             png_uint_16p buffer;

//             image.format = PNG_FORMAT_LINEAR_RGB_ALPHA;

//             buffer = malloc(PNG_IMAGE_SIZE(image));

//             if (buffer != NULL)
//             {
//                if (png_image_finish_read(&image, NULL/*background*/, buffer,
//                   0/*row_stride*/,
//                   NULL/*colormap for PNG_FORMAT_FLAG_COLORMAP*/))
//                {
//                   /* This is the place where the Porter-Duff 'Over' operator
//                    * needs to be done by this code.  In fact, any image
//                    * processing required can be done here; the data is in
//                    * the correct format (linear, 16-bit) and source and
//                    * destination are in memory.
//                    */
//                   sprite_op(sprite, x, y, &image, buffer);
//                   free(buffer);
//                   ++*argv, --*argc;
//                   /* And continue to the next argument */
//                   continue;
//                }

//                else
//                {
//                   free(buffer);
//                   fprintf(stderr, "simpleover: read %s: %s\n", (*argv)[0],
//                       image.message);
//                }
//             }

//             else
//             {
//                fprintf(stderr, "simpleover: out of memory: %lu bytes\n",
//                   (unsigned long)PNG_IMAGE_SIZE(image));

//                /* png_image_free must be called if we abort the Simplified API
//                 * read because of a problem detected in this code.  If problems
//                 * are detected in the Simplified API it cleans up itself.
//                 */
//                png_image_free(&image);
//             }
//          }

//          else
//          {
//             /* Failed to read the first argument: */
//             fprintf(stderr, "simpleover: %s: %s\n", (*argv)[0], image.message);
//          }

//          return 0; /* failure */
//       }
//    }

//   /* All the Sprite operations have completed successfully. Save the RGBA
//   * buffer as a PNG using the simplified write API.
//   */
//   absl::StatusOr<std::string> status_or_path =
//     sandbox2::CreateNamedTempFileAndClose("file_" + sprite->name);
//   if (!status_or_path.ok()) {
    
//   }
//   ASSERT_THAT(status_or_path, IsOk()) << "Could not create temp file";
//   std::string filename = sandbox2::file::JoinPath(
//     sandbox2::file_util::fileops::GetCWD(), status_or_path.value());
//   sapi::v::ConstCStr fn(filename.c_str());


//   sprite->file = sapi::v::Remote(api._open_file(fn.PtrBefore()));
//   ASSERT_THAT(tif.GetValue(), NotNull())
//       << "Can't open temp file " << filename;

//   png_image save;

//   memset(&save, 0, sizeof save);
//   save.version = PNG_IMAGE_VERSION;
//   save.opaque = NULL;
//   save.width = sprite->width;
//   save.height = sprite->height;
//   save.format = PNG_FORMAT_LINEAR_RGB_ALPHA;
//   save.flags = PNG_IMAGE_FLAG_FAST;
//   save.colormap_entries = 0;

//   if (png_image_write_to_stdio(&save, sprite->file, 1/*convert_to_8_bit*/,
//       sprite->buffer, 0/*row_stride*/, NULL/*colormap*/))
//   {
//       /* Success; the buffer is no longer needed: */
//       free(sprite->buffer);
//       sprite->buffer = NULL;
//       return 1; /* ok */
//   }

//   else
//       fprintf(stderr, "simpleover: write Sprite %s: %s\n", sprite->name,
//         save.message);
//   }
// }

bool IsOperation(const std::string& instr) {
  return instr.rfind("--", 0) == 0;
}

int GetOperationData(const std::string& instr, const int* x, const int* y, const char* z) {
  return std::sscanf(instr.c_str(), kOperationFormat.data(), x, y, z);
}

absl::Status AddSprite(LibPNGApi& api, sapi::v::Struct<png_image>& image, sapi::v::Array<uint8_t>& buffer,
  const std::vector<std::string> commands, size_t& index, struct Sprite *sprite) {
  absl::Status status = absl::OkStatus();
  absl::StatusOr<int> status_or_int;

  while (index < commands.size() && status.ok()) {
    if (IsOperation(commands[index])) {
      break;
    }

    int x, y;
    char tombstone;
    if (GetOperationData(commands[index], &x, &y, &tombstone) != 2) {
      return absl::InternalError(absl::StrCat("--add=", sprite->name, ": invalid position ", commands[index]));
    }

    auto width = image.mutable_data()->width;
    auto height = image.mutable_data()->height;
    if (x < 0 || y < 0 || static_cast<uint32_t>(x) >= width ||
      static_cast<uint32_t>(y) >= height || sprite->width > width - x ||
      sprite->height > height - y) {
      return absl::InternalError(absl::StrCat("Sprite ", sprite->name, " @ (", x, ",", y, ") outside image"));
    }

    sapi::v::Struct<png_image> in;

    in.mutable_data()->version = PNG_IMAGE_VERSION;
    // if (!api._rewind_file(&sprite->file).ok()) {
    //   return absl::InternalError("_rewind_file failed");
    // }

    status_or_int = api.png_image_begin_read_from_stdio(in.PtrBoth(), &sprite->file);
    if (!status_or_int.ok()) {
      return absl::InternalError("png_image_begin_read_from_stdio failed");
    }
    if (!status_or_int.value()) {
      return absl::InternalError(absl::StrCat("add Sprite ", sprite->name, ": ", in.mutable_data()->message));
    }

    in.mutable_data()->format = PNG_FORMAT_RGB;

    // I should pass ptr to array[some index:] ? How?
    status_or_int = api.png_image_finish_read(in.PtrBoth(), sapi::v::NullPtr().PtrBoth(),
        buffer.PtrBoth(), image.mutable_data()->width * 3, sapi::v::NullPtr().PtrBoth());

    if (!status_or_int.ok()) {
      return absl::InternalError("png_image_finish_read failed");
    }
    if (!status_or_int.value()) {
      return absl::InternalError(absl::StrCat("add Sprite ", sprite->name, :": ", in.mutable_data()->message));
    }
  }
  return status;
}

bool IsSprite(const std::string& arg) {
  return arg.rfind(kSpriteFlag, 0) == 0;
}

int GetSprite(const std::string& instr, Sprite* sprite) {
  char tombstone;
  return std::sscanf(instr.c_str(), (kSpriteFlag + kSpriteFlagFormat).c_str(),
    &sprite->width, &sprite->height, &sprite->name, &tombstone);
}

bool IsAdd(const std::string& arg) {
  return arg.rfind(kAddFlag, 0) == 0;
}

std::string GetAddName(const std::string& instr) {
  return instr.substr(kAddFlag.size());
}

absl::Status SimpleoverProcess(LibPNGApi& api, sapi::v::Struct<png_image>& image, sapi::v::Array<uint8_t>& buffer, const std::vector<std::string>& commands) {
  bool is_found = true;
  constexpr size_t csprites = 10;
  std::vector<Sprite> sprites;
  absl::Status status = absl::OkStatus();

  for (size_t index = 0; index < commands.size() && status.ok();) {
    if (IsSprite(commands[index])) {
      if (sprites.size() == csprites) {
        return absl::InternalError("too many sprites");
      }

      Sprite sprite;
      auto read_count = GetSprite(commands[index], &sprite);
      if (read_count != 2 && read_count != 3 || sprite.width == 0 || sprite.height == 0) {
        status = absl::InternalError(absl::StrCat("invalid Sprite ", index));
        break;
      }

      if (sprite.name.empty()) {
        sprite.name = "sprite-" + std::to_string(sprites.size() + 1);
      }      

      uint32_t buf_size;
      if (!__builtin_umull_overflow(sizeof(png_uint_16) * 4, sprite.width, &buf_size)
        || !__builtin_umull_overflow(buf_size, sprite.height, &buf_size)) {
        status = absl::InternalError(absl::StrCat("Sprite ", index, "too large"));
        break;
      }

      sprite.buffer.buffer.resize(buf_size, 0);
      sprites.push_back(sprite);
      ++index;

      status = CreateSprite(image, buffer, commands, index, sprite);
    } else if (IsAdd(commands[index])) {
      auto name = GetAddName(commands[index]);
      ++index;
      bool successfully = false;

      for (int i = sprites.size(); i != -1; --i)
        if (sprites[i].name == name) {
          successfully = true;
          status = AddSprite(api, image, buffer, commands, index, &sprites[i]);
          break;
        }
      }
      if (!successfully) {
        status = absl::InternalError(absl::StrCat("Sprite ", name, " not found"));
        break;
      }
    } else {
      status = absl::InternalError(absl::StrCat("unrecognized operation ", commands[index]));
      break;
    }
  }

  for (const auto& sprite : sprites) {
    if (sprite.file.GetValue()) {
      // api._close_file(sprite.file.PtrBoth());
    }
  }

  return status;
}

absl::Status LibPNGMain(const std::string infile, const std::vector<std::string>& commands, const std::string& outfile) {
  LibPNGSapiSandbox sandbox;
  sandbox.AddFile(infile);
  sandbox.AddFile(outfile);

  SAPI_RETURN_IF_ERROR(sandbox.Init());
  LibPNGApi api(&sandbox);

  sapi::v::Struct<png_image> image;
  sapi::v::ConstCStr infile_var(infile.c_str());

  image.mutable_data()->version = PNG_IMAGE_VERSION;
  image.mutable_data()->opaque = NULL;

  SAPI_ASSIGN_OR_RETURN(int result,
    api.png_image_begin_read_from_file(image.PtrBoth(), infile_var.PtrBefore()));
  if (!result) {
    return absl::InternalError(absl::StrCat("simpleover: error: ", image.mutable_data()->message));
  }

  png_bytep buffer;

  image.mutable_data()->format = PNG_FORMAT_RGB; /* 24-bit RGB */

  buffer = malloc(PNG_IMAGE_SIZE(image));
  sapi::v::Array<uint8_t> buffer_sapi(PNG_IMAGE_SIZE(*image.mutable_data()));

  sapi::v::Struct<png_color> green_background;
  *green_background.mutable_data() = {0, 0xff, 0};

  SAPI_ASSIGN_OR_RETURN(result, api.png_image_finish_read(image.PtrBoth(), green_background.PtrBefore(), buffer_sapi.PtrBoth(),
      0, sapi::v::NullPtr().PtrBoth()));
  if (!result) {
    SAPI_RETURN_IF_ERROR(api.png_image_free(image.PtrBoth()));
    return absl::InternalError(absl::StrCat("simpleover: read ", infile, ": ", image.mutable_data()->message));
  }

  auto status = SimpleoverProcess(api, image, buffer_sapi, commands));
  if (!status.ok()) {
    SAPI_RETURN_IF_ERROR(api.png_image_free(image.PtrBoth()));
    return status;
  }


  sapi::v::ConstCStr outfile_var(outfile.c_str());
  SAPI_ASSIGN_OR_RETURN(result, api.png_image_write_to_file(image.PtrBoth(), outfile_var.PtrBefore(), 0, buffer_sapi.PtrBoth(),
    0, sapi::v::NullPtr().PtrBoth()));
  if (!result) {
    status = absl::InternalError(absl::StrCat("simpleover: write to file ", outfile, " failed: ", image.mutable_data()->message));
  }

  SAPI_RETURN_IF_ERROR(api.png_image_free(image.PtrBoth()));
  return status;
}

int main(int argc, const char **argv) {
  if (argc < 2) {
    LOG(ERROR) <<
      "simpleover: usage: simpleover background.png [output.png]\n"
      "  Output 'background.png' as a 24-bit RGB PNG file in 'output.png'\n"
      "   or, if not given, stdout.  'background.png' will be composited\n"
      "   on fully saturated green.\n"
      "\n"
      "  Optionally, before output, process additional PNG files:\n"
      "\n"
      "   --sprite=width,height,name {[--at=x,y] {sprite.png}}\n"
      "    Produce a transparent Sprite of size (width,height) and with\n"
      "     name 'name'.\n"
      "    For each sprite.png composite it using a Porter-Duff 'Over'\n"
      "     operation at offset (x,y) in the Sprite (defaulting to (0,0)).\n"
      "     Input PNGs will be truncated to the area of the sprite.\n"
      "\n"
      "   --add='name' {x,y}\n"
      "    Optionally, before output, composite a sprite, 'name', which\n"
      "     must have been previously produced using --sprite, at each\n"
      "     offset (x,y) in the output image.  Each Sprite must fit\n"
      "     completely within the output image.\n"
      "\n"
      "  PNG files are processed in the order they occur on the command\n"
      "  line and thus the first PNG processed appears as the bottommost\n"
      "  in the output image.\n");

    return EXIT_FAILURE;
  }

  size_t start_command_id = 2;
  std::optional<std::string> out_file_name;
  std::vector<std::string> commands;

  if (argc > start_command_id && argv[start_command_id][0] != '-') {
    out_file_name = argv[start_command_id];
    ++start_command_id;
  }

  commands.reserve(argc - start_command_id);
  for (size_t i = start_command_id; i != argc; ++i) {
    commands.push_back(argv[i]);
  }

  auto status = LibPNGMain(argv[1], commands, out_file_name);
  if (!status.ok()) {
    LOG(ERROR) << "LibPNGMain failed with error:\n"
               << status.ToString() << '\n';
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

