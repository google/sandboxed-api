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

#include <iostream>

#include "helpers.h"
#include "sandbox.h"

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
static void create(const char *filename, int compress, const char **argv);
#endif
static void errmsg(const char *);
static void extract(const char *filename, int do_extract, int flags);
static int copy_data(struct archive *, struct archive *);
static void msg(const char *);
static void usage(void);

static int verbose = 0;

int main(int argc, const char **argv) {
google::InitGoogleLogging(argv[0]);
  std::cout << "BEGIN\n";
  const char *filename = NULL;
  int compress, flags, mode, opt;

  (void)argc;
  mode = 'x';
  verbose = 0;
  compress = '\0';
  flags = ARCHIVE_EXTRACT_TIME;

  /* Among other sins, getopt(3) pulls in printf(3). */
  while (*++argv != NULL && **argv == '-') {
    const char *p = *argv + 1;

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

  return (0);
}

#ifndef NO_CREATE
static char buff[16384];

static void create(const char *filename, int compress, const char **argv) {
  std::cout << "CREATE FILENAME=" << filename << std::endl;
}
#endif

static void extract(const char *filename, int do_extract, int flags) {
  std::cout << "extract" << std::endl;
    std::string filename_absolute = MakeAbsolutePathAtCWD(filename);

    std::cout << "filename = " << filename_absolute << std::endl;
    
    SapiLibarchiveSandboxExtract sandbox(filename_absolute, do_extract);
    CHECK(sandbox.Init().ok()) << "Error during sandbox initialization";

    LibarchiveApi api(&sandbox);

    struct archive *a;
    struct archive *ext;
    struct archive_entry *entry;
    int r;
    api.archive_read_new().IgnoreError();
    sapi::StatusOr<archive *> ret = api.archive_read_new();
    CHECK(ret.ok()) << "archive_read_new call failed";
    // std::cout << "RET VALUE = " << ret.value() << std::endl;
    CHECK(ret.value() != NULL) << "Failed to create read archive";
    a = ret.value();

    ret = api.archive_write_disk_new();
    CHECK(ret.ok()) << "write_disk_new call failed";
    CHECK(ret.value() != NULL) << "Failed to create write disk archive";
    ext = ret.value();

    sapi::v::RemotePtr a_ptr(a);
    sapi::v::RemotePtr ext_ptr(ext);

    sapi::StatusOr<int> ret2;
    ret2 = api.archive_write_disk_set_options(&ext_ptr, flags);
    CHECK(ret2.ok()) << "write_disk_set_options call failed";
    CHECK(ret2.value() != ARCHIVE_FATAL) << "Unexpected result from write_disk_set_options call";

#ifndef NO_BZIP2_EXTRACT
	ret2 = api.archive_read_support_filter_bzip2(&a_ptr);
    CHECK(ret2.ok()) << "read_support_filter_bzip2 call failed";
    CHECK(ret2.value() != ARCHIVE_FATAL) << "Unexpected result from read_support_filter_bzip2 call";
#endif
#ifndef NO_GZIP_EXTRACT
	ret2 = api.archive_read_support_filter_gzip(&a_ptr);
    CHECK(ret2.ok()) << "read_suppport_filter_gzip call failed";
    CHECK(ret2.value() != ARCHIVE_FATAL) << "Unexpected result from read_suppport_filter_gzip call";
#endif
#ifndef NO_COMPRESS_EXTRACT
	ret2 = api.archive_read_support_filter_compress(&a_ptr);
    CHECK(ret2.ok()) << "read_support_filter_compress call failed";
    CHECK(ret2.value() != ARCHIVE_FATAL) << "Unexpected result from read_support_filter_compress call";
#endif
#ifndef NO_TAR_EXTRACT
	ret2 = api.archive_read_support_format_tar(&a_ptr);
    CHECK(ret2.ok()) << "read_support_format_tar call failed";
    CHECK(ret2.value() != ARCHIVE_FATAL) << "Unexpected result fromread_support_format_tar call";
#endif
#ifndef NO_CPIO_EXTRACT
	ret2 = api.archive_read_support_format_cpio(&a_ptr);
    CHECK(ret2.ok()) << "read_support_format_cpio call failed";
    CHECK(ret2.value() != ARCHIVE_FATAL) << "Unexpected result from read_support_format_tar call";
#endif
#ifndef NO_LOOKUP
	ret2 = api.archive_write_disk_set_standard_lookup(&ext_ptr); 
    CHECK(ret2.ok()) << "write_disk_set_standard_lookup call failed";
    CHECK(ret2.value() != ARCHIVE_FATAL) << "Unexpected result from write_disk_set_standard_lookup call";
#endif


	if (filename != NULL && strcmp(filename, "-") == 0)
		filename = NULL;

    sapi::v::ConstCStr sapi_filename(filename_absolute.c_str());
    
    std::cout << "opening filename" << std::endl;
    
    ret2 = api.archive_read_open_filename(&a_ptr, sapi_filename.PtrBefore(), 10240);
    CHECK(ret2.ok()) << "read_open_filename call failed";
    // CHECK(!ret2.value()) << GetErrorString(&a_ptr, sandbox, api);
    CHECK(!ret2.value()) << CheckStatusAndGetString(api.archive_error_string(&a_ptr), sandbox);
    // CHECK(!ret2.value()) << CallFunctionAndGetString(&a_ptr, sandbox, &api, &api.archive_error_string);

            sapi::v::IntBase<struct archive_entry *> entry_ptr_tmp(0);


    for (;;) {
        
        std::cout << "================reading headers==============" << std::endl;

        ret2 = api.archive_read_next_header(&a_ptr, entry_ptr_tmp.PtrBoth());
        //std::cout << "val = " << ret2.value() << std::endl;
        CHECK(ret2.ok()) << "read_next_header call failed";
        // CHECK(ret2.value() != ARCHIVE_OK) << GetErrorString(&a_ptr, sandbox, api);

        if(ret2.value() == ARCHIVE_EOF) {
            break;
        }

                CHECK(ret2.value() == ARCHIVE_OK) << CheckStatusAndGetString(api.archive_error_string(&a_ptr), sandbox);


        sapi::v::RemotePtr entry_ptr(entry_ptr_tmp.GetValue());

        if(verbose && do_extract) {
            std::cout << "x " ;
        }
        

        if (verbose || !do_extract) {
            std::cout << CheckStatusAndGetString(api.archive_entry_pathname(&entry_ptr), sandbox) << " ";
        }

        if (do_extract) {
            std::cout << "EXTRACT HERE";
        }
        // use the needcr stuff here TODO
        std::cout << std::endl;

    }

    std::cout << "out of loop" << std::endl;

            ret2 = api.archive_read_close(&a_ptr);
        CHECK(ret2.ok()) << "read_close call failed";
        CHECK(!ret2.value()) << "Unexpected value from read_close call";

        ret2 = api.archive_read_free(&a_ptr);
        CHECK(ret2.ok()) << "read_free call failed";
        CHECK(!ret2.value()) << "Unexpected result from read_free call";


        ret2 = api.archive_write_close(&ext_ptr);
        CHECK(ret2.ok()) << "write_close call failed";
        CHECK(!ret2.value()) << "Unexpected result from write_close call";


        ret2 = api.archive_write_free(&ext_ptr);
        CHECK(ret2.ok()) << "write_free call failed";
        CHECK(!ret2.value()) << "Unexpected result from write_free call";

}

static int copy_data(struct archive *ar, struct archive *aw) {
    return 0;
}

static void msg(const char *m) { write(1, m, strlen(m)); }

static void errmsg(const char *m) {
  if (m == NULL) {
    m = "Error: No error description provided.\n";
  }
  write(2, m, strlen(m));
}

static void usage(void) {
  /* Many program options depend on compile options. */
  const char *m =
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

  errmsg(m);
  exit(1);
}
