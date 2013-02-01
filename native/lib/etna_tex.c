#include "etna_tex.h"

#include <stdint.h>

void etna_texture_tile(void *dest, void *src, unsigned width, unsigned height, unsigned src_stride, unsigned elmtsize)
{
#define TEX_TILE_WIDTH (4)
#define TEX_TILE_HEIGHT (4)
#define TEX_TILE_WORDS (TEX_TILE_WIDTH*TEX_TILE_HEIGHT)
    unsigned ytiles = height / TEX_TILE_HEIGHT;
    unsigned xtiles = width / TEX_TILE_WIDTH;
    unsigned dst_stride = xtiles * TEX_TILE_WORDS;
    if(elmtsize == 4)
    {
        src_stride >>= 2;

        for(unsigned ty=0; ty<ytiles; ++ty)
        {
            for(unsigned tx=0; tx<xtiles; ++tx)
            {
                unsigned ofs = ty * dst_stride + tx * TEX_TILE_WORDS;
                for(unsigned y=0; y<TEX_TILE_HEIGHT; ++y)
                {
                    for(unsigned x=0; x<TEX_TILE_WIDTH; ++x)
                    {
                        unsigned srcy = ty*TEX_TILE_HEIGHT + y;
                        unsigned srcx = tx*TEX_TILE_WIDTH + x;
                        ((uint32_t*)dest)[ofs] = ((uint32_t*)src)[srcy*src_stride+srcx];
                        ofs += 1;
                    }
                }
            }
        }
    }
}

