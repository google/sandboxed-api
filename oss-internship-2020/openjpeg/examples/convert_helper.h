#include "openjp2_sapi.sapi.h"

#define OPJ_TRUE 1
#define OPJ_FALSE 0

const char* opj_version(void);
int imagetopnm(opj_image_t * image, const char *outfile, int force_split);
static int are_comps_similar(opj_image_t * image);
