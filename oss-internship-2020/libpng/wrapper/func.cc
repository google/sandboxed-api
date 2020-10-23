#include "func.h"  // NOLINT(build/include)

FILE* png_fopen(const char* filename, const char* mode) {
	return fopen(filename, mode);
}