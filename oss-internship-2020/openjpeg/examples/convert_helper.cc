/*
 * The copyright in this software is being made available under the 2-clauses
 * BSD License, included below. This software may be subject to other third
 * party and contributor rights, including patent rights, and no such rights
 * are granted under this license.
 *
 * Copyright (c) 2002-2014, Universite catholique de Louvain (UCL), Belgium
 * Copyright (c) 2002-2014, Professor Benoit Macq
 * Copyright (c) 2001-2003, David Janssens
 * Copyright (c) 2002-2003, Yannick Verschueren
 * Copyright (c) 2003-2007, Francois-Olivier Devaux
 * Copyright (c) 2003-2014, Antonin Descampe
 * Copyright (c) 2005, Herve Drolon, FreeImage Team
 * Copyright (c) 2006-2007, Parvatha Elangovan
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS `AS IS'
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

// copies of a few library tools

#include "convert_helper.h"

#define OPJ_TRUE 1
#define OPJ_FALSE 0

const char *opj_version(void) { return "2.3.1"; }

static int are_comps_similar(opj_image_t *image) {
  unsigned int i;
  for (i = 1; i < image->numcomps; i++) {
    if (image->comps[0].dx != image->comps[i].dx ||
        image->comps[0].dy != image->comps[i].dy ||
        (i <= 2 && (image->comps[0].prec != image->comps[i].prec ||
                    image->comps[0].sgnd != image->comps[i].sgnd))) {
      return OPJ_FALSE;
    }
  }
  return OPJ_TRUE;
}

int imagetopnm(opj_image_t *image, const char *outfile, int force_split) {
  int *red, *green, *blue, *alpha;
  int wr, hr, max;
  int i;
  unsigned int compno, ncomp;
  int adjustR, adjustG, adjustB, adjustA;
  int fails, two, want_gray, has_alpha, triple;
  int prec, v;
  FILE *fdest = NULL;
  const char *tmp = outfile;
  char *destname;

  alpha = NULL;

  if ((prec = (int)image->comps[0].prec) > 16) {
    fprintf(stderr,
            "%s:%d:imagetopnm\n\tprecision %d is larger than 16"
            "\n\t: refused.\n",
            __FILE__, __LINE__, prec);
    return 1;
  }

  two = has_alpha = 0;
  fails = 1;
  ncomp = image->numcomps;

  while (*tmp) {
    ++tmp;
  }
  tmp -= 2;
  want_gray = (*tmp == 'g' || *tmp == 'G');
  ncomp = image->numcomps;

  if (want_gray) {
    ncomp = 1;
  }

  if ((force_split == 0) && ncomp >= 2 && are_comps_similar(image)) {
    fdest = fopen(outfile, "wb");

    if (!fdest) {
      fprintf(stderr, "ERROR -> failed to open %s for writing\n", outfile);
      return fails;
    }
    two = (prec > 8);
    triple = (ncomp > 2);
    wr = (int)image->comps[0].w;
    hr = (int)image->comps[0].h;
    max = (1 << prec) - 1;
    has_alpha = (ncomp == 4 || ncomp == 2);

    red = image->comps[0].data;
    if (red == NULL) {
      fprintf(stderr, "imagetopnm: planes[%d] == NULL.\n", 0);
      fprintf(stderr, "\tAborting\n");
      fclose(fdest);
      return fails;
    }

    if (triple) {
      green = image->comps[1].data;
      blue = image->comps[2].data;
      for (i = 1; i <= 2; i++) {
        if (image->comps[i].data == NULL) {
          fprintf(stderr, "imagetopnm: planes[%d] == NULL.\n", i);
          fprintf(stderr, "\tAborting\n");
          fclose(fdest);
          return fails;
        }
      }
    } else {
      green = blue = NULL;
    }

    if (has_alpha) {
      const char *tt = (triple ? "RGB_ALPHA" : "GRAYSCALE_ALPHA");

      fprintf(fdest,
              "P7\n# OpenJPEG-%s\nWIDTH %d\nHEIGHT %d\nDEPTH %u\n"
              "MAXVAL %d\nTUPLTYPE %s\nENDHDR\n",
              opj_version(), wr, hr, ncomp, max, tt);
      alpha = image->comps[ncomp - 1].data;
      adjustA = (image->comps[ncomp - 1].sgnd
                     ? 1 << (image->comps[ncomp - 1].prec - 1)
                     : 0);
    } else {
      fprintf(fdest, "P6\n# OpenJPEG-%s\n%d %d\n%d\n", opj_version(), wr, hr,
              max);
      adjustA = 0;
    }
    adjustR = (image->comps[0].sgnd ? 1 << (image->comps[0].prec - 1) : 0);

    if (triple) {
      adjustG = (image->comps[1].sgnd ? 1 << (image->comps[1].prec - 1) : 0);
      adjustB = (image->comps[2].sgnd ? 1 << (image->comps[2].prec - 1) : 0);
    } else {
      adjustG = adjustB = 0;
    }

    for (i = 0; i < wr * hr; ++i) {
      if (two) {
        v = *red + adjustR;
        ++red;
        if (v > 65535) {
          v = 65535;
        } else if (v < 0) {
          v = 0;
        }

        /* netpbm: */
        fprintf(fdest, "%c%c", (unsigned char)(v >> 8), (unsigned char)v);

        if (triple) {
          v = *green + adjustG;
          ++green;
          if (v > 65535) {
            v = 65535;
          } else if (v < 0) {
            v = 0;
          }

          /* netpbm: */
          fprintf(fdest, "%c%c", (unsigned char)(v >> 8), (unsigned char)v);

          v = *blue + adjustB;
          ++blue;
          if (v > 65535) {
            v = 65535;
          } else if (v < 0) {
            v = 0;
          }

          /* netpbm: */
          fprintf(fdest, "%c%c", (unsigned char)(v >> 8), (unsigned char)v);

        } /* if(triple) */

        if (has_alpha) {
          v = *alpha + adjustA;
          ++alpha;
          if (v > 65535) {
            v = 65535;
          } else if (v < 0) {
            v = 0;
          }

          /* netpbm: */
          fprintf(fdest, "%c%c", (unsigned char)(v >> 8), (unsigned char)v);
        }
        continue;

      } /* if(two) */

      /* prec <= 8: */
      v = *red++;
      if (v > 255) {
        v = 255;
      } else if (v < 0) {
        v = 0;
      }

      fprintf(fdest, "%c", (unsigned char)v);
      if (triple) {
        v = *green++;
        if (v > 255) {
          v = 255;
        } else if (v < 0) {
          v = 0;
        }

        fprintf(fdest, "%c", (unsigned char)v);
        v = *blue++;
        if (v > 255) {
          v = 255;
        } else if (v < 0) {
          v = 0;
        }

        fprintf(fdest, "%c", (unsigned char)v);
      }
      if (has_alpha) {
        v = *alpha++;
        if (v > 255) {
          v = 255;
        } else if (v < 0) {
          v = 0;
        }

        fprintf(fdest, "%c", (unsigned char)v);
      }
    } /* for(i */

    fclose(fdest);
    return 0;
  }

  /* YUV or MONO: */

  if (image->numcomps > ncomp) {
    fprintf(stderr, "WARNING -> [PGM file] Only the first component\n");
    fprintf(stderr, "           is written to the file\n");
  }
  destname = (char *)malloc(strlen(outfile) + 8);
  if (destname == NULL) {
    fprintf(stderr, "imagetopnm: memory out\n");
    return 1;
  }
  for (compno = 0; compno < ncomp; compno++) {
    if (ncomp > 1) {
      /*sprintf(destname, "%d.%s", compno, outfile);*/
      const size_t olen = strlen(outfile);
      const size_t dotpos = olen - 4;

      strncpy(destname, outfile, dotpos);
      sprintf(destname + dotpos, "_%u.pgm", compno);
    } else {
      sprintf(destname, "%s", outfile);
    }

    fdest = fopen(destname, "wb");
    if (!fdest) {
      fprintf(stderr, "ERROR -> failed to open %s for writing\n", destname);
      free(destname);
      return 1;
    }
    wr = (int)image->comps[compno].w;
    hr = (int)image->comps[compno].h;
    prec = (int)image->comps[compno].prec;
    max = (1 << prec) - 1;

    fprintf(fdest, "P5\n#OpenJPEG-%s\n%d %d\n%d\n", opj_version(), wr, hr, max);

    red = image->comps[compno].data;

    if (!red) {
      fclose(fdest);
      continue;
    }

    adjustR =
        (image->comps[compno].sgnd ? 1 << (image->comps[compno].prec - 1) : 0);

    if (prec > 8) {
      for (i = 0; i < wr * hr; i++) {
        v = *red + adjustR;
        ++red;
        if (v > 65535) {
          v = 65535;
        } else if (v < 0) {
          v = 0;
        }

        /* netpbm: */
        fprintf(fdest, "%c%c", (unsigned char)(v >> 8), (unsigned char)v);

        if (has_alpha) {
          v = *alpha++;
          if (v > 65535) {
            v = 65535;
          } else if (v < 0) {
            v = 0;
          }

          /* netpbm: */
          fprintf(fdest, "%c%c", (unsigned char)(v >> 8), (unsigned char)v);
        }
      }      /* for(i */
    } else { /* prec <= 8 */
      for (i = 0; i < wr * hr; ++i) {
        v = *red + adjustR;
        ++red;
        if (v > 255) {
          v = 255;
        } else if (v < 0) {
          v = 0;
        }
        fprintf(fdest, "%c", (unsigned char)v);
      }
    }
    fclose(fdest);
  } /* for (compno */
  free(destname);

  return 0;
} /* imagetopnm() */
