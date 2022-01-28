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

#ifndef LIBARCHIVE_LD_PRELOAD_EXAMPLE_MINITAR_H_
#define LIBARCHIVE_LD_PRELOAD_EXAMPLE_MINITAR_H_

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

// The declarations below have been extracted from upstream's minitar.c, hence
// the formatting.
//
// clang-format off

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
void create(const char *filename, int compress, const char **argv,
            int verbose = 1);
#endif
void errmsg(const char *);
void extract(const char *filename, int do_extract, int flags, int verbose = 1);
int copy_data(struct archive *, struct archive *);
void msg(const char *);
void usage(void);

// clang-format on
#endif  // LIBARCHIVE_LD_PRELOAD_EXAMPLE_MINITAR_H_
