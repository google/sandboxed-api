#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

#include <iostream>

#include "guetzli_transaction.h"
#include "sandboxed_api/sandbox2/util/fileops.h"
#include "sandboxed_api/util/statusor.h"

namespace {

constexpr int kDefaultJPEGQuality = 95;
constexpr int kDefaultMemlimitMB = 6000; // in MB
//constexpr absl::string_view kMktempSuffix = "XXXXXX";

// sapi::StatusOr<std::pair<std::string, int>> CreateNamedTempFile(
//     absl::string_view prefix) {
//   std::string name_template = absl::StrCat(prefix, kMktempSuffix);
//   int fd = mkstemp(&name_template[0]);
//   if (fd < 0) {
//     return absl::UnknownError("Error creating temp file");
//   }
//   return std::pair<std::string, int>{std::move(name_template), fd};
// }

void TerminateHandler() {
  fprintf(stderr, "Unhandled exception. Most likely insufficient memory available.\n"
          "Make sure that there is 300MB/MPix of memory available.\n");
  exit(1);
}

void Usage() {
  fprintf(stderr,
      "Guetzli JPEG compressor. Usage: \n"
      "guetzli [flags] input_filename output_filename\n"
      "\n"
      "Flags:\n"
      "  --verbose    - Print a verbose trace of all attempts to standard output.\n"
      "  --quality Q  - Visual quality to aim for, expressed as a JPEG quality value.\n"
      "                 Default value is %d.\n"
      "  --memlimit M - Memory limit in MB. Guetzli will fail if unable to stay under\n"
      "                 the limit. Default limit is %d MB.\n"
      "  --nomemlimit - Do not limit memory usage.\n", kDefaultJPEGQuality, kDefaultMemlimitMB);
  exit(1);
}

}  // namespace

int main(int argc, const char** argv) {
  std::set_terminate(TerminateHandler);

  int verbose = 0;
  int quality = kDefaultJPEGQuality;
  int memlimit_mb = kDefaultMemlimitMB;   

  int opt_idx = 1;
  for(;opt_idx < argc;opt_idx++) {
    if (strnlen(argv[opt_idx], 2) < 2 || argv[opt_idx][0] != '-' || argv[opt_idx][1] != '-')
      break;

    if (!strcmp(argv[opt_idx], "--verbose")) {
      verbose = 1;
    } else if (!strcmp(argv[opt_idx], "--quality")) {
      opt_idx++;
      if (opt_idx >= argc)
        Usage();
      quality = atoi(argv[opt_idx]);
    } else if (!strcmp(argv[opt_idx], "--memlimit")) {
      opt_idx++;
      if (opt_idx >= argc)
        Usage();
      memlimit_mb = atoi(argv[opt_idx]);
    } else if (!strcmp(argv[opt_idx], "--nomemlimit")) {
      memlimit_mb = -1;
    } else if (!strcmp(argv[opt_idx], "--")) {
      opt_idx++;
      break;
    } else {
      fprintf(stderr, "Unknown commandline flag: %s\n", argv[opt_idx]);
      Usage();
    }
  }

  if (argc - opt_idx != 2) {
    Usage();
  }

  sandbox2::file_util::fileops::FDCloser in_fd_closer(
    open(argv[opt_idx], O_RDONLY));
  
  if (in_fd_closer.get() < 0) {
    fprintf(stderr, "Can't open input file: %s\n", argv[opt_idx]);
    return 1;
  }

  // auto out_temp_file = CreateNamedTempFile("/tmp/" + std::string(argv[opt_idx + 1]));
  // if (!out_temp_file.ok()) {
  //   fprintf(stderr, "Can't create temporary output file: %s\n", 
  //     argv[opt_idx + 1]);
  //   return 1;
  // }
  // sandbox2::file_util::fileops::FDCloser out_fd_closer(
  //     out_temp_file.value().second);

  // if (unlink(out_temp_file.value().first.c_str()) < 0) {
  //   fprintf(stderr, "Error unlinking temp out file: %s\n", 
  //       out_temp_file.value().first.c_str());
  //   return 1;
  // }

  sandbox2::file_util::fileops::FDCloser out_fd_closer(
    open(".", O_TMPFILE | O_RDWR, S_IRUSR | S_IWUSR));

  if (out_fd_closer.get() < 0) {
    fprintf(stderr, "Can't create temporary output file: %s\n", argv[opt_idx]);
    return 1;
  }

  // sandbox2::file_util::fileops::FDCloser out_fd_closer(open(argv[opt_idx + 1], 
  //   O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP));
  
  // if (out_fd_closer.get() < 0) {
  //   fprintf(stderr, "Can't open output file: %s\n", argv[opt_idx + 1]);
  //   return 1;
  // }

  guetzli::sandbox::TransactionParams params = {
    in_fd_closer.get(),
    out_fd_closer.get(),
    verbose,
    quality,
    memlimit_mb
  };

  guetzli::sandbox::GuetzliTransaction transaction(std::move(params));
  auto result = transaction.Run();

  if (result.ok()) {
    if (access(argv[opt_idx + 1], F_OK) != -1) {
      if (remove(argv[opt_idx + 1]) < 0) {
        fprintf(stderr, "Error deleting existing output file: %s\n", 
          argv[opt_idx + 1]);
      }
    } 

    std::stringstream path;
    path << "/proc/self/fd/" << out_fd_closer.get();
    linkat(AT_FDCWD, path.str().c_str(), AT_FDCWD, argv[opt_idx + 1],
                                          AT_SYMLINK_FOLLOW);
  }
  else {
    fprintf(stderr, "%s\n", result.ToString().c_str()); // Use cerr instead ?
    return 1;
  }

  return 0;
}