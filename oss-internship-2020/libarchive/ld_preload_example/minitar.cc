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

// clang-format off
#include "minitar.h"  // NOLINT(build/include)

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
void create(const char* filename, int compress, const char** argv);
#endif
void errmsg(const char*);
void extract(const char* filename, int do_extract, int flags);
int copy_data(struct archive*, struct archive*);
void msg(const char*);
void usage(void);

#ifndef NO_CREATE

void create(const char* filename, int compress, const char** argv,
            int verbose) {
  struct archive* a;
  struct archive_entry* entry;
  ssize_t len;
  int fd;

  a = archive_write_new();
  switch (compress) {
#ifndef NO_BZIP2_CREATE
    case 'j':
    case 'y':
      archive_write_add_filter_bzip2(a);
      break;
#endif
#ifndef NO_COMPRESS_CREATE
    case 'Z':
      archive_write_add_filter_compress(a);
      break;
#endif
#ifndef NO_GZIP_CREATE
    case 'z':
      archive_write_add_filter_gzip(a);
      break;
#endif
    default:
      archive_write_add_filter_none(a);
      break;
  }
  archive_write_set_format_ustar(a);
  if (filename != NULL && strcmp(filename, "-") == 0) filename = NULL;
  archive_write_open_filename(a, filename);

  while (*argv != NULL) {
    struct archive* disk = archive_read_disk_new();
#ifndef NO_LOOKUP
    archive_read_disk_set_standard_lookup(disk);
#endif
    int r;

    r = archive_read_disk_open(disk, *argv);
    if (r != ARCHIVE_OK) {
      errmsg(archive_error_string(disk));
      errmsg("\n");
      exit(1);
    }

    for (;;) {
      int needcr = 0;

      entry = archive_entry_new();
      r = archive_read_next_header2(disk, entry);
      if (r == ARCHIVE_EOF) break;
      if (r != ARCHIVE_OK) {
        errmsg(archive_error_string(disk));
        errmsg("\n");
        exit(1);
      }
      archive_read_disk_descend(disk);
      if (verbose) {
        msg("a ");
        msg(archive_entry_pathname(entry));
        needcr = 1;
      }
      r = archive_write_header(a, entry);
      if (r < ARCHIVE_OK) {
        errmsg(": ");
        errmsg(archive_error_string(a));
        needcr = 1;
      }
      if (r == ARCHIVE_FATAL) exit(1);
      if (r > ARCHIVE_FAILED) {
        static char buff[16384];
        fd = open(archive_entry_sourcepath(entry), O_RDONLY);
        len = read(fd, buff, sizeof(buff));
        while (len > 0) {
          archive_write_data(a, buff, len);
          len = read(fd, buff, sizeof(buff));
        }
        close(fd);
      }
      archive_entry_free(entry);
      if (needcr) msg("\n");
    }
    archive_read_close(disk);
    archive_read_free(disk);
    argv++;
  }
  archive_write_close(a);
  archive_write_free(a);
}
#endif

void extract(const char* filename, int do_extract, int flags, int verbose) {
  struct archive* a;
  struct archive* ext;
  struct archive_entry* entry;
  int r;

  a = archive_read_new();
  ext = archive_write_disk_new();
  archive_write_disk_set_options(ext, flags);
#ifndef NO_BZIP2_EXTRACT
  archive_read_support_filter_bzip2(a);
#endif
#ifndef NO_GZIP_EXTRACT
  archive_read_support_filter_gzip(a);
#endif
#ifndef NO_COMPRESS_EXTRACT
  archive_read_support_filter_compress(a);
#endif
#ifndef NO_TAR_EXTRACT
  archive_read_support_format_tar(a);
#endif
#ifndef NO_CPIO_EXTRACT
  archive_read_support_format_cpio(a);
#endif
#ifndef NO_LOOKUP
  archive_write_disk_set_standard_lookup(ext);
#endif
  if (filename != NULL && strcmp(filename, "-") == 0) filename = NULL;
  if ((r = archive_read_open_filename(a, filename, 10240))) {
    errmsg(archive_error_string(a));
    errmsg("\n");
    exit(r);
  }
  for (;;) {
    int needcr = 0;
    r = archive_read_next_header(a, &entry);
    if (r == ARCHIVE_EOF) break;
    if (r != ARCHIVE_OK) {
      errmsg(archive_error_string(a));
      errmsg("\n");
      exit(1);
    }
    if (verbose && do_extract) msg("x ");

    if (verbose || !do_extract) {
      std::cout << archive_entry_pathname(entry) << std::endl;
      msg(" ");
      needcr = 1;
    }
    if (do_extract) {
      r = archive_write_header(ext, entry);
      if (r != ARCHIVE_OK) {
        errmsg(archive_error_string(a));
        needcr = 1;
      } else {
        r = copy_data(a, ext);
        if (r != ARCHIVE_OK) needcr = 1;
      }
    }
    if (needcr) msg("\n");
  }
  archive_read_close(a);
  archive_read_free(a);

  archive_write_close(ext);
  archive_write_free(ext);
}

int copy_data(struct archive* ar, struct archive* aw) {
  int r;
  const void* buff;
  size_t size;
  int64_t offset;

  for (;;) {
    r = archive_read_data_block(ar, &buff, &size, &offset);
    if (r == ARCHIVE_EOF) return (ARCHIVE_OK);
    if (r != ARCHIVE_OK) {
      errmsg(archive_error_string(ar));
      return (r);
    }
    r = archive_write_data_block(aw, buff, size, offset);
    if (r != ARCHIVE_OK) {
      errmsg(archive_error_string(ar));
      return (r);
    }
  }
}

void msg(const char* m) { write(1, m, strlen(m)); }

void errmsg(const char* m) {
  if (m == NULL) {
    m = "Error: No error description provided.\n";
  }
  write(2, m, strlen(m));
}

void usage(void) {
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

  errmsg(m);
  exit(1);
}

// clang-format on
