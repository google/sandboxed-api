#include "func.h"  // NOLINT(build/include)

void png_setjmp(png_structrp ptr) {
	setjmp(png_jmpbuf(ptr));
}

void* png_fdopen(int fd, const char *mode) {
	FILE* f = fdopen(fd, mode);
	return static_cast<void*>(f);
}

void png_rewind(void* f) {
	rewind(static_cast<FILE*>(f));
}

void png_fread(void* buffer, size_t size, size_t count, void* stream) {
	fread(buffer, size, count,static_cast<FILE*>(stream));
}

void png_fclose(void* f) {
	fclose(static_cast<FILE*>(f));
}

void png_init_io_wrapper(png_structrp png_ptr, void* f) {
	png_init_io(png_ptr, static_cast<FILE*>(f));
}

png_structp png_create_read_struct_wrapper(png_const_charp user_png_ver, png_voidp error_ptr) {
	return png_create_read_struct(user_png_ver, error_ptr, NULL, NULL);
}

png_structp png_create_write_struct_wrapper(png_const_charp user_png_ver, png_voidp error_ptr) {
	return png_create_write_struct(user_png_ver, error_ptr, NULL, NULL);
}