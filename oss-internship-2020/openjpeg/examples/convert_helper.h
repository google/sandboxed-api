// imagetopnm and the two functions it calls internaly are copied from the
// library's tools; from openjpeg/src/bin/jp2/convert.c

#include "openjp2_sapi.sapi.h"

#define OPJ_TRUE 1
#define OPJ_FALSE 0

const char* opj_version(void);
static int are_comps_similar(opj_image_t* image);
int imagetopnm(opj_image_t* image, const char* outfile, int force_split);
