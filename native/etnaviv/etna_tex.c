#include <etnaviv/etna_tex.h>

#include <stdint.h>
#include <stdio.h>

#define TEX_TILE_WIDTH (4)
#define TEX_TILE_HEIGHT (4)
#define TEX_TILE_WORDS (TEX_TILE_WIDTH*TEX_TILE_HEIGHT)

void etna_texture_tile(void *dest, void *src, unsigned width, unsigned height, unsigned src_stride, unsigned elmtsize)
{
    //unsigned ytiles = (height + TEX_TILE_HEIGHT - 1) / TEX_TILE_HEIGHT;
    unsigned xtiles = (width + TEX_TILE_WIDTH - 1) / TEX_TILE_WIDTH;
    unsigned dst_stride = xtiles * TEX_TILE_WORDS;
    if(elmtsize == 4)
    {
        src_stride >>= 2;
        for(unsigned srcy=0; srcy<height; ++srcy)
        {
            unsigned ty = (srcy/TEX_TILE_HEIGHT) * dst_stride + (srcy%TEX_TILE_HEIGHT) * TEX_TILE_WIDTH;
            for(unsigned srcx=0; srcx<width; ++srcx)
            {
                ((uint32_t*)dest)[ty + (srcx/TEX_TILE_WIDTH)*TEX_TILE_WORDS + (srcx%TEX_TILE_WIDTH)] = 
                    ((uint32_t*)src)[srcy * src_stride + srcx];
            }
        }
    } else if(elmtsize == 2)
    {
        src_stride >>= 1;
        for(unsigned srcy=0; srcy<height; ++srcy)
        {
            unsigned ty = (srcy/TEX_TILE_HEIGHT) * dst_stride + (srcy%TEX_TILE_HEIGHT) * TEX_TILE_WIDTH;
            for(unsigned srcx=0; srcx<width; ++srcx)
            {
                ((uint16_t*)dest)[ty + (srcx/TEX_TILE_WIDTH)*TEX_TILE_WORDS + (srcx%TEX_TILE_WIDTH)] = 
                    ((uint16_t*)src)[srcy * src_stride + srcx];
            }
        }
    } else if(elmtsize == 1)
    {
        for(unsigned srcy=0; srcy<height; ++srcy)
        {
            unsigned ty = (srcy/TEX_TILE_HEIGHT) * dst_stride + (srcy%TEX_TILE_HEIGHT) * TEX_TILE_WIDTH;
            for(unsigned srcx=0; srcx<width; ++srcx)
            {
                ((uint8_t*)dest)[ty + (srcx/TEX_TILE_WIDTH)*TEX_TILE_WORDS + (srcx%TEX_TILE_WIDTH)] = 
                    ((uint8_t*)src)[srcy * src_stride + srcx];
            }
        }
    } else
    {
        /* Tiling is only used for element sizes of 1, 2 and 4 */
        printf("etna_texture_tile: unhandled element size %i\n", elmtsize);
    }
}

void etna_texture_untile(void *dest, void *src, unsigned width, unsigned height, unsigned dst_stride, unsigned elmtsize)
{
    //unsigned ytiles = (height + TEX_TILE_HEIGHT - 1) / TEX_TILE_HEIGHT;
    unsigned xtiles = (width + TEX_TILE_WIDTH - 1) / TEX_TILE_WIDTH;
    unsigned src_stride = xtiles * TEX_TILE_WORDS;
    if(elmtsize == 4)
    {
        dst_stride >>= 2;
        for(unsigned dsty=0; dsty<height; ++dsty)
        {
            unsigned ty = (dsty/TEX_TILE_HEIGHT) * src_stride + (dsty%TEX_TILE_HEIGHT) * TEX_TILE_WIDTH;
            for(unsigned dstx=0; dstx<width; ++dstx)
            {
                ((uint32_t*)dest)[dsty * dst_stride + dstx] = 
                    ((uint32_t*)src)[ty + (dstx/TEX_TILE_WIDTH)*TEX_TILE_WORDS + (dstx%TEX_TILE_WIDTH)];
            }
        }
    } else if(elmtsize == 2)
    {
        dst_stride >>= 1;
        for(unsigned dsty=0; dsty<height; ++dsty)
        {
            unsigned ty = (dsty/TEX_TILE_HEIGHT) * src_stride + (dsty%TEX_TILE_HEIGHT) * TEX_TILE_WIDTH;
            for(unsigned dstx=0; dstx<width; ++dstx)
            {
                ((uint16_t*)dest)[dsty * dst_stride + dstx] =
                    ((uint16_t*)src)[ty + (dstx/TEX_TILE_WIDTH)*TEX_TILE_WORDS + (dstx%TEX_TILE_WIDTH)];
            }
        }
    } else if(elmtsize == 1)
    {
        for(unsigned dsty=0; dsty<height; ++dsty)
        {
            unsigned ty = (dsty/TEX_TILE_HEIGHT) * src_stride + (dsty%TEX_TILE_HEIGHT) * TEX_TILE_WIDTH;
            for(unsigned dstx=0; dstx<width; ++dstx)
            {
                ((uint8_t*)dest)[dsty * dst_stride + dstx] =
                    ((uint8_t*)src)[ty + (dstx/TEX_TILE_WIDTH)*TEX_TILE_WORDS + (dstx%TEX_TILE_WIDTH)];
            }
        }
    } else
    {
        /* Tiling is only used for element sizes of 1, 2 and 4 */
        printf("etna_texture_tile: unhandled element size %i\n", elmtsize);
    }
}


