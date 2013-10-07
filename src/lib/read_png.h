#ifndef H_READ_PNG
#define H_READ_PNG

#include <stdbool.h>
#include <stdint.h>

/* Read PNG, return pixels in A8R8G8B8 format */
bool read_png(char *name, int row_align, int *outStride, int *outWidth, int *outHeight, uint32_t **outData);

#endif

