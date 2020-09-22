/*-
 * This file is in the public domain.
 * Do with it as you will.
 */

/*-
 * This is a compact "tar" program whose primary goal is small size.
 * Statically linked, it can be very small indeed.  This serves a number
 * of goals:
 *   o a testbed for libarchive (to check for link pollution),
 *   o a useful tool for space-constrained systems (boot floppies, etc),
 *   o a place to experiment with new implementation ideas for bsdtar,
 *   o a small program to demonstrate libarchive usage.
 *
 * Use the following macros to suppress features:
 *   NO_BZIP2 - Implies NO_BZIP2_CREATE and NO_BZIP2_EXTRACT
 *   NO_BZIP2_CREATE - Suppress bzip2 compression support.
 *   NO_BZIP2_EXTRACT - Suppress bzip2 auto-detection and decompression.
 *   NO_COMPRESS - Implies NO_COMPRESS_CREATE and NO_COMPRESS_EXTRACT
 *   NO_COMPRESS_CREATE - Suppress compress(1) compression support
 *   NO_COMPRESS_EXTRACT - Suppress compress(1) auto-detect and decompression.
 *   NO_CREATE - Suppress all archive creation support.
 *   NO_CPIO_EXTRACT - Suppress auto-detect and dearchiving of cpio archives.
 *   NO_GZIP - Implies NO_GZIP_CREATE and NO_GZIP_EXTRACT
 *   NO_GZIP_CREATE - Suppress gzip compression support.
 *   NO_GZIP_EXTRACT - Suppress gzip auto-detection and decompression.
 *   NO_LOOKUP - Try to avoid getpw/getgr routines, which can be very large
 *   NO_TAR_EXTRACT - Suppress tar extraction
 *
 * With all of the above macros defined (except NO_TAR_EXTRACT), you
 * get a very small program that can recognize and extract essentially
 * any uncompressed tar archive.  On FreeBSD 5.1, this minimal program
 * is under 64k, statically linked, which compares rather favorably to
 *         main(){printf("hello, world");}
 * which is over 60k statically linked on the same operating system.
 * Without any of the above macros, you get a static executable of
 * about 180k with a lot of very sophisticated modern features.
 * Obviously, it's trivial to add support for ISO, Zip, mtree,
 * lzma/xz, etc.  Just fill in the appropriate setup calls.
 */

#include <archive.h>
#include <archive_entry.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstdlib>
#include <iostream>
#include <memory>

#include "helpers.h"
#include "sandbox.h"
#include "sandboxed_api/sandbox2/util.h"
#include "sandboxed_api/sandbox2/util/path.h"
#include "sandboxed_api/var_array.h"

/*
 * NO_CREATE implies NO_BZIP2_CREATE and NO_GZIP_CREATE and NO_COMPRESS_CREATE.
 */
#ifdef NO_CREATE
#undef NO_BZIP2_CREATE
#define NO_BZIP2_CREATE
#undef NO_COMPRESS_CREATE
#define NO_COMPRESS_CREATE
#undef NO_GZIP_CREATE
#define NO_GZIP_CREATE
#endif

/*
 * The combination of NO_BZIP2_CREATE and NO_BZIP2_EXTRACT is
 * equivalent to NO_BZIP2.
 */
#ifdef NO_BZIP2_CREATE
#ifdef NO_BZIP2_EXTRACT
#undef NO_BZIP2
#define NO_BZIP2
#endif
#endif

#ifdef NO_BZIP2
#undef NO_BZIP2_EXTRACT
#define NO_BZIP2_EXTRACT
#undef NO_BZIP2_CREATE
#define NO_BZIP2_CREATE
#endif

/*
 * The combination of NO_COMPRESS_CREATE and NO_COMPRESS_EXTRACT is
 * equivalent to NO_COMPRESS.
 */
#ifdef NO_COMPRESS_CREATE
#ifdef NO_COMPRESS_EXTRACT
#undef NO_COMPRESS
#define NO_COMPRESS
#endif
#endif

#ifdef NO_COMPRESS
#undef NO_COMPRESS_EXTRACT
#define NO_COMPRESS_EXTRACT
#undef NO_COMPRESS_CREATE
#define NO_COMPRESS_CREATE
#endif

/*
 * The combination of NO_GZIP_CREATE and NO_GZIP_EXTRACT is
 * equivalent to NO_GZIP.
 */
#ifdef NO_GZIP_CREATE
#ifdef NO_GZIP_EXTRACT
#undef NO_GZIP
#define NO_GZIP
#endif
#endif

#ifdef NO_GZIP
#undef NO_GZIP_EXTRACT
#define NO_GZIP_EXTRACT
#undef NO_GZIP_CREATE
#define NO_GZIP_CREATE
#endif

#ifndef NO_CREATE
static void create(const char* filename, int compress, const char** argv);
#endif
static void extract(const char* filename, int do_extract, int flags);
static int copy_data(sapi::v::RemotePtr* ar, sapi::v::RemotePtr* aw,
                     LibarchiveApi& api, SapiLibarchiveSandboxExtract& sandbox);
static void usage(void);

static int verbose = 0;

int main(int argc, const char** argv) {
  google::InitGoogleLogging(argv[0]);
  const char* filename = NULL;
  int compress, flags, mode, opt;

  (void)argc;
  mode = 'x';
  verbose = 0;
  compress = '\0';
  flags = ARCHIVE_EXTRACT_TIME;

  /* Among other sins, getopt(3) pulls in printf(3). */
  while (*++argv != NULL && **argv == '-') {
    const char* p = *argv + 1;

    while ((opt = *p++) != '\0') {
      switch (opt) {
#ifndef NO_CREATE
        case 'c':
          mode = opt;
          break;
#endif
        case 'f':
          if (*p != '\0')
            filename = p;
          else
            filename = *++argv;
          p += strlen(p);
          break;
#ifndef NO_BZIP2_CREATE
        case 'j':
          compress = opt;
          break;
#endif
        case 'p':
          flags |= ARCHIVE_EXTRACT_PERM;
          flags |= ARCHIVE_EXTRACT_ACL;
          flags |= ARCHIVE_EXTRACT_FFLAGS;
          break;
        case 't':
          mode = opt;
          break;
        case 'v':
          verbose++;
          break;
        case 'x':
          mode = opt;
          break;
#ifndef NO_BZIP2_CREATE
        case 'y':
          compress = opt;
          break;
#endif
#ifndef NO_COMPRESS_CREATE
        case 'Z':
          compress = opt;
          break;
#endif
#ifndef NO_GZIP_CREATE
        case 'z':
          compress = opt;
          break;
#endif
        default:
          usage();
      }
    }
  }

  switch (mode) {
#ifndef NO_CREATE
    case 'c':
      create(filename, compress, argv);
      break;
#endif
    case 't':
      extract(filename, 0, flags);
      break;
    case 'x':
      extract(filename, 1, flags);
      break;
  }

  return EXIT_SUCCESS;
}

#ifndef NO_CREATE

static void create(const char* initial_filename, int compress,
                   const char** argv) {
  // We split the filename path into dirname and filename. To the filename we
  // prepend /output/ so that it will work with the security policy.

  std::string abs_path = MakeAbsolutePathAtCWD(std::string(initial_filename));
  auto [archive_path, filename_tmp] = sandbox2::file::SplitPath(abs_path);

  std::string filename("/output/");
  filename.append(filename_tmp);

  std::vector<std::string> absolute_paths = MakeAbsolutePathsVec(argv);

  std::vector<std::string> relative_paths;
  sandbox2::util::CharPtrArrToVecString(const_cast<char* const*>(argv),
                                        &relative_paths);

  SapiLibarchiveSandboxCreate sandbox(absolute_paths, archive_path);
  CHECK(sandbox.Init().ok()) << "Error during sandbox initialization";
  LibarchiveApi api(&sandbox);

  sapi::StatusOr<archive*> ret = api.archive_write_new();
  CHECK(ret.ok()) << "write_new call failed";
  CHECK(ret.value() != NULL) << "Failed to create write archive";

  sapi::v::RemotePtr a(ret.value());

  sapi::StatusOr<int> ret2;

  switch (compress) {
#ifndef NO_BZIP2_CREATE
    case 'j':
    case 'y':
      ret2 = api.archive_write_add_filter_bzip2(&a);
      CHECK(ret2.ok()) << "write_add_filter_bzip2 call failed";
      CHECK(ret2.value() != ARCHIVE_FATAL)
          << "Unexpected result from write_add_filter_bzip2 call";
      break;
#endif
#ifndef NO_COMPRESS_CREATE
    case 'Z':
      ret2 = api.archive_write_add_filter_compress(&a);
      CHECK(ret2.ok()) << "write_add_filter_compress call failed";
      CHECK(ret2.value() != ARCHIVE_FATAL)
          << "Unexpected result from write_add_filter_compress call";
      break;
#endif
#ifndef NO_GZIP_CREATE
    case 'z':
      ret2 = api.archive_write_add_filter_gzip(&a);
      CHECK(ret2.ok()) << "write_add_filter_gzip call failed";
      CHECK(ret2.value() != ARCHIVE_FATAL)
          << "Unexpected result from write_add_filter_gzip call";
      break;
#endif
    default:
      ret2 = api.archive_write_add_filter_none(&a);
      CHECK(ret2.ok()) << "write_add_filter_none call failed";
      CHECK(ret2.value() != ARCHIVE_FATAL)
          << "Unexpected result from write_add_filter_none call";
      break;
  }

  ret2 = api.archive_write_set_format_ustar(&a);
  CHECK(ret2.ok()) << "write_set_format_ustar call failed";
  CHECK(ret2.value() != ARCHIVE_FATAL)
      << "Unexpected result from write_set_format_ustar call";

  const char* filename_ptr = filename.data();
  if (filename_ptr != NULL && strcmp(filename_ptr, "-") == 0) {
    filename_ptr = NULL;
  }

  ret2 = api.archive_write_open_filename(
      &a, sapi::v::ConstCStr(filename_ptr).PtrBefore());
  CHECK(ret2.ok()) << "write_open_filename call failed";
  CHECK(ret2.value() != ARCHIVE_FATAL)
      << "Unexpected result from write_open_filename call";

  int file_idx = 0;

  for (int file_idx = 0; file_idx < absolute_paths.size(); ++file_idx) {
    ret = api.archive_read_disk_new();
    CHECK(ret.ok()) << "read_disk_new call failed";
    CHECK(ret.value() != NULL) << "Failed to create read_disk archive";

    sapi::v::RemotePtr disk(ret.value());

#ifndef NO_LOOKUP
    ret2 = api.archive_read_disk_set_standard_lookup(&disk);
    CHECK(ret2.ok()) << "read_disk_set_standard_lookup call failed";
    CHECK(ret2.value() != ARCHIVE_FATAL)
        << "Unexpected result from read_disk_set_standard_lookup call";
#endif

    ret2 = api.archive_read_disk_open(
        &disk,
        sapi::v::ConstCStr(absolute_paths[file_idx].c_str()).PtrBefore());
    CHECK(ret2.ok()) << "read_disk_open call failed";
    CHECK(ret2.value() == ARCHIVE_OK)
        << CheckStatusAndGetString(api.archive_error_string(&disk), sandbox);

    for (;;) {
      int needcr = 0;

      sapi::StatusOr<archive_entry*> ret3;
      ret3 = api.archive_entry_new();

      CHECK(ret3.ok()) << "entry_new call failed";
      CHECK(ret3.value() != NULL) << "Failed to create archive_entry";

      sapi::v::RemotePtr entry(ret3.value());

      ret2 = api.archive_read_next_header2(&disk, &entry);
      CHECK(ret2.ok()) << "read_next_header2 call failed";

      if (ret2.value() == ARCHIVE_EOF) {
        break;
      }

      CHECK(ret2.value() == ARCHIVE_OK)
          << CheckStatusAndGetString(api.archive_error_string(&disk), sandbox);

      ret2 = api.archive_read_disk_descend(&disk);
      CHECK(ret2.ok()) << "read_disk_descend call failed";

      // After using the absolute path before, we now need to add the pathname
      // to the archive entry. This would help store the files by their relative
      // paths. However, in the case where a directory is added to the archive,
      // all of the files inside of it are addes as well so we replace the
      // absolute path prefix with the relative one. Example: we add the folder
      // test_files which becomes /absolute/path/test_files and the files inside
      // of it will become /absolute/path/test_files/file1 and we change it to
      // test_files/file1 so that it is relative.

      std::string path_name =
          CheckStatusAndGetString(api.archive_entry_pathname(&entry), sandbox);

      path_name.replace(path_name.begin(),
                        path_name.begin() + absolute_paths[file_idx].length(),
                        relative_paths[file_idx]);

      // On top of those changes, we need to remove leading '/' characters
      // and also remove everything up to the last occurrence of '../'.

      size_t found = path_name.find_first_not_of("/");
      if (found != std::string::npos) {
        path_name.erase(path_name.begin(), path_name.begin() + found);
      }

      found = path_name.rfind("../");
      if (found != std::string::npos) {
        path_name = path_name.substr(found + 3);
      }

      CHECK(api.archive_entry_set_pathname(
                   &entry, sapi::v::ConstCStr(path_name.c_str()).PtrBefore())
                .ok())
          << "Could not set pathname";

      if (verbose) {
        std::cout << CheckStatusAndGetString(api.archive_entry_pathname(&entry),
                                             sandbox);
        needcr = 1;
      }

      ret2 = api.archive_write_header(&a, &entry);
      CHECK(ret2.ok()) << "write_header call failed";

      if (ret2.value() < ARCHIVE_OK) {
        std::cout << CheckStatusAndGetString(api.archive_error_string(&a),
                                             sandbox);
        needcr = 1;
      }
      CHECK(ret2.value() != ARCHIVE_FATAL)
          << "Unexpected result from write_header call";

      if (ret2.value() > ARCHIVE_FAILED) {
        // int fd = open(CheckStatusAndGetString(
        //                   api.archive_entry_sourcepath(&entry), sandbox)
        //                   .c_str(),
        //               O_RDONLY);
        int fd = open(path_name.c_str(), O_RDONLY);
        CHECK(fd >= 0) << "Could not open file";

        sapi::v::Fd sapi_fd(fd);
        sapi::v::Int read_ret;
        sapi::v::Array<char> buff(kBuffSize);
        sapi::v::UInt ssize(kBuffSize);

        CHECK(sandbox.Allocate(&buff, true).ok())
            << "Could not allocate remote buffer";

        CHECK(sandbox.TransferToSandboxee(&sapi_fd).ok())
            << "Could not transfer file descriptor";

        CHECK(sandbox.Call("read", &read_ret, &sapi_fd, buff.PtrNone(), &ssize)
                  .ok())
            << "Read call failed";

        while (read_ret.GetValue() > 0) {
          CHECK(api.archive_write_data(&a, buff.PtrNone(), read_ret.GetValue())
                    .ok())
              << "write_data call failed";

          CHECK(
              sandbox.Call("read", &read_ret, &sapi_fd, buff.PtrNone(), &ssize)
                  .ok())
              << "Read call failed";
        }
        // sapi_fd variable goes out of scope here so both the local and the
        // remote file descriptors are closed.
      }

      CHECK(api.archive_entry_free(&entry).ok()) << "entry_free call failed";

      if (needcr) {
        std::cout << std::endl;
      }
    }

    ret2 = api.archive_read_close(&disk);
    CHECK(ret2.ok()) << "read_close call failed";
    CHECK(!ret2.value()) << "Unexpected result from read_close call";

    ret2 = api.archive_read_free(&disk);
    CHECK(ret2.ok()) << "read_free call failed";
    CHECK(!ret2.value()) << "Unexpected result from read_free call";
  }

  ret2 = api.archive_write_close(&a);
  CHECK(ret2.ok()) << "write_close call failed";
  CHECK(!ret2.value()) << "Unexpected result from write_close call";

  ret2 = api.archive_write_free(&a);
  CHECK(ret2.ok()) << "write_free call failed";
  CHECK(!ret2.value()) << "Unexpected result from write_free call";
}
#endif

static void extract(const char* filename, int do_extract, int flags) {
  std::string tmp_dir;
  if (do_extract) {
    tmp_dir = CreateTempDirAtCWD();
  }

  // We can use a struct like this in order to delete the temporary
  // directory that was created earlier whenever the function ends.
  struct ExtractTempDirectoryCleanup {
    ~ExtractTempDirectoryCleanup() {
      sandbox2::file_util::fileops::DeleteRecursively(dir);
    }
    std::string dir;
  };

  // We should only delete it if the do_extract flag is true which
  // means that this struct is instantiated only in that case.
  std::shared_ptr<ExtractTempDirectoryCleanup> cleanup_ptr;
  if (do_extract) {
    cleanup_ptr = std::make_unique<ExtractTempDirectoryCleanup>();
    cleanup_ptr->dir = tmp_dir;
  }

  std::string filename_absolute = MakeAbsolutePathAtCWD(filename);

  SapiLibarchiveSandboxExtract sandbox(filename_absolute, do_extract, tmp_dir);
  CHECK(sandbox.Init().ok()) << "Error during sandbox initialization";

  LibarchiveApi api(&sandbox);

  sapi::StatusOr<archive*> ret = api.archive_read_new();
  CHECK(ret.ok()) << "archive_read_new call failed";
  CHECK(ret.value() != NULL) << "Failed to create read archive";

  sapi::v::RemotePtr a(ret.value());

  ret = api.archive_write_disk_new();
  CHECK(ret.ok()) << "write_disk_new call failed";
  CHECK(ret.value() != NULL) << "Failed to create write disk archive";

  sapi::v::RemotePtr ext(ret.value());

  sapi::StatusOr<int> ret2;
  ret2 = api.archive_write_disk_set_options(&ext, flags);
  CHECK(ret2.ok()) << "write_disk_set_options call failed";
  CHECK(ret2.value() != ARCHIVE_FATAL)
      << "Unexpected result from write_disk_set_options call";

#ifndef NO_BZIP2_EXTRACT
  ret2 = api.archive_read_support_filter_bzip2(&a);
  CHECK(ret2.ok()) << "read_support_filter_bzip2 call failed";
  CHECK(ret2.value() != ARCHIVE_FATAL)
      << "Unexpected result from read_support_filter_bzip2 call";
#endif
#ifndef NO_GZIP_EXTRACT
  ret2 = api.archive_read_support_filter_gzip(&a);
  CHECK(ret2.ok()) << "read_suppport_filter_gzip call failed";
  CHECK(ret2.value() != ARCHIVE_FATAL)
      << "Unexpected result from read_suppport_filter_gzip call";
#endif
#ifndef NO_COMPRESS_EXTRACT
  ret2 = api.archive_read_support_filter_compress(&a);
  CHECK(ret2.ok()) << "read_support_filter_compress call failed";
  CHECK(ret2.value() != ARCHIVE_FATAL)
      << "Unexpected result from read_support_filter_compress call";
#endif
#ifndef NO_TAR_EXTRACT
  ret2 = api.archive_read_support_format_tar(&a);
  CHECK(ret2.ok()) << "read_support_format_tar call failed";
  CHECK(ret2.value() != ARCHIVE_FATAL)
      << "Unexpected result fromread_support_format_tar call";
#endif
#ifndef NO_CPIO_EXTRACT
  ret2 = api.archive_read_support_format_cpio(&a);
  CHECK(ret2.ok()) << "read_support_format_cpio call failed";
  CHECK(ret2.value() != ARCHIVE_FATAL)
      << "Unexpected result from read_support_format_tar call";
#endif
#ifndef NO_LOOKUP
  ret2 = api.archive_write_disk_set_standard_lookup(&ext);
  CHECK(ret2.ok()) << "write_disk_set_standard_lookup call failed";
  CHECK(ret2.value() != ARCHIVE_FATAL)
      << "Unexpected result from write_disk_set_standard_lookup call";
#endif

  const char* filename_ptr = filename_absolute.c_str();
  if (filename_ptr != NULL && strcmp(filename_ptr, "-") == 0) {
    filename_ptr = NULL;
  }

  ret2 = api.archive_read_open_filename(
      &a, sapi::v::ConstCStr(filename_ptr).PtrBefore(), kBlockSize);
  CHECK(ret2.ok()) << "read_open_filename call failed";
  CHECK(!ret2.value()) << CheckStatusAndGetString(api.archive_error_string(&a),
                                                  sandbox);

  for (;;) {
    int needcr = 0;
    sapi::v::IntBase<struct archive_entry*> entry_ptr_tmp(0);

    ret2 = api.archive_read_next_header(&a, entry_ptr_tmp.PtrAfter());
    CHECK(ret2.ok()) << "read_next_header call failed";

    if (ret2.value() == ARCHIVE_EOF) {
      break;
    }

    CHECK(ret2.value() == ARCHIVE_OK)
        << CheckStatusAndGetString(api.archive_error_string(&a), sandbox);

    sapi::v::RemotePtr entry(entry_ptr_tmp.GetValue());

    if (verbose && do_extract) {
      std::cout << "x ";
    }

    if (verbose || !do_extract) {
      std::cout << CheckStatusAndGetString(api.archive_entry_pathname(&entry),
                                           sandbox)
                << " ";
      needcr = 1;
    }

    if (do_extract) {
      ret2 = api.archive_write_header(&ext, &entry);
      CHECK(ret2.ok()) << "write_header call failed";

      if (ret2.value() != ARCHIVE_OK) {
        std::cout << CheckStatusAndGetString(api.archive_error_string(&a),
                                             sandbox);
        needcr = 1;
      } else if (copy_data(&a, &ext, api, sandbox) != ARCHIVE_OK) {
        needcr = 1;
      }
    }
    // use the needcr stuff here TODO
    if (needcr) {
      std::cout << std::endl;
    }
  }

  ret2 = api.archive_read_close(&a);
  CHECK(ret2.ok()) << "read_close call failed";
  CHECK(!ret2.value()) << "Unexpected value from read_close call";

  ret2 = api.archive_read_free(&a);
  CHECK(ret2.ok()) << "read_free call failed";
  CHECK(!ret2.value()) << "Unexpected result from read_free call";

  ret2 = api.archive_write_close(&ext);
  CHECK(ret2.ok()) << "write_close call failed";
  CHECK(!ret2.value()) << "Unexpected result from write_close call";

  ret2 = api.archive_write_free(&ext);
  CHECK(ret2.ok()) << "write_free call failed";
  CHECK(!ret2.value()) << "Unexpected result from write_free call";
}

static int copy_data(sapi::v::RemotePtr* ar, sapi::v::RemotePtr* aw,
                     LibarchiveApi& api,
                     SapiLibarchiveSandboxExtract& sandbox) {
  sapi::StatusOr<int> ret;

  sapi::v::IntBase<struct archive_entry*> buff_ptr_tmp(0);
  sapi::v::ULLong size;
  sapi::v::SLLong offset;

  for (;;) {
    ret = api.archive_read_data_block(ar, buff_ptr_tmp.PtrAfter(),
                                      size.PtrAfter(), offset.PtrAfter());
    CHECK(ret.ok()) << "read_data_block call failed";

    if (ret.value() == ARCHIVE_EOF) {
      return ARCHIVE_OK;
    }
    if (ret.value() != ARCHIVE_OK) {
      std::cout << CheckStatusAndGetString(api.archive_error_string(ar),
                                           sandbox);
      return ret.value();
    }

    sapi::v::RemotePtr buff(buff_ptr_tmp.GetValue());

    ret = api.archive_write_data_block(aw, &buff, size.GetValue(),
                                       offset.GetValue());

    CHECK(ret.ok()) << "write_data_block call failed";

    if (ret.value() != ARCHIVE_OK) {
      std::cout << CheckStatusAndGetString(api.archive_error_string(ar),
                                           sandbox);
      return ret.value();
    }
  }
}

// static void msg(const char* m) { write(1, m, strlen(m)); }

// static void errmsg(const char* m) {
//   if (m == NULL) {
//     m = "Error: No error description provided.\n";
//   }
//   write(2, m, strlen(m));
// }

static void usage(void) {
  /* Many program options depend on compile options. */
  const char* m =
      "Usage: minitar [-"
#ifndef NO_CREATE
      "c"
#endif
#ifndef NO_BZIP2
      "j"
#endif
      "tvx"
#ifndef NO_BZIP2
      "y"
#endif
#ifndef NO_COMPRESS
      "Z"
#endif
#ifndef NO_GZIP
      "z"
#endif
      "] [-f file] [file]\n";

  std::cout << m << std::endl;
  exit(EXIT_FAILURE);
}
