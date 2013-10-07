#include <etnaviv/etna_tex.h>

#include <stdint.h>
#include <stdio.h>

#define TEX_TILE_WIDTH (4)
#define TEX_TILE_HEIGHT (4)
#define TEX_TILE_WORDS (TEX_TILE_WIDTH*TEX_TILE_HEIGHT)

#define DO_TILE(type) \
        src_stride /= sizeof(type); \
        dst_stride = (dst_stride * TEX_TILE_HEIGHT) / sizeof(type); \
        for(unsigned srcy=0; srcy<height; ++srcy) \
        { \
            unsigned dsty = basey + srcy; \
            unsigned ty = (dsty/TEX_TILE_HEIGHT) * dst_stride + (dsty%TEX_TILE_HEIGHT) * TEX_TILE_WIDTH; \
            for(unsigned srcx=0; srcx<width; ++srcx) \
            { \
                unsigned dstx = basex + srcx; \
                ((type*)dest)[ty + (dstx/TEX_TILE_WIDTH)*TEX_TILE_WORDS + (dstx%TEX_TILE_WIDTH)] = \
                    ((type*)src)[srcy * src_stride + srcx]; \
            } \
        }

#define DO_UNTILE(type) \
        src_stride = (src_stride * TEX_TILE_HEIGHT) / sizeof(type); \
        dst_stride /= sizeof(type); \
        for(unsigned dsty=0; dsty<height; ++dsty) \
        { \
            unsigned srcy = basey + dsty; \
            unsigned sy = (srcy/TEX_TILE_HEIGHT) * src_stride + (srcy%TEX_TILE_HEIGHT) * TEX_TILE_WIDTH; \
            for(unsigned dstx=0; dstx<width; ++dstx) \
            { \
                unsigned srcx = basex + dstx; \
                ((type*)dest)[dsty * dst_stride + dstx] = \
                    ((type*)src)[sy + (srcx/TEX_TILE_WIDTH)*TEX_TILE_WORDS + (srcx%TEX_TILE_WIDTH)]; \
            } \
        }

void etna_texture_tile(void *dest, void *src, unsigned basex, unsigned basey, unsigned dst_stride, unsigned width, unsigned height, unsigned src_stride, unsigned elmtsize)
{
    if(elmtsize == 4)
    {
        DO_TILE(uint32_t)
    } else if(elmtsize == 2)
    {
        DO_TILE(uint16_t)
    } else if(elmtsize == 1)
    {
        DO_TILE(uint8_t)
    } else
    {
        /* Tiling is only used for element sizes of 1, 2 and 4 */
        printf("etna_texture_tile: unhandled element size %i\n", elmtsize);
    }
}

void etna_texture_untile(void *dest, void *src, unsigned basex, unsigned basey, unsigned src_stride, unsigned width, unsigned height, unsigned dst_stride, unsigned elmtsize)
{
    if(elmtsize == 4)
    {
        DO_UNTILE(uint32_t);
    } else if(elmtsize == 2)
    {
        DO_UNTILE(uint16_t);
    } else if(elmtsize == 1)
    {
        DO_UNTILE(uint8_t);
    } else
    {
        /* Tiling is only used for element sizes of 1, 2 and 4 */
        printf("etna_texture_tile: unhandled element size %i\n", elmtsize);
    }
}


