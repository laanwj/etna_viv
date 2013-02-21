#ifndef H_ETNA_TEX

#include <stdint.h>

void etna_texture_tile(void *dest, void *src, unsigned width, unsigned height, unsigned src_stride, unsigned elmtsize);
void etna_convert_r8g8b8_to_b8g8r8x8(uint32_t *dst, const uint8_t *src, unsigned num_pixels);

#endif

