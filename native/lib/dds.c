/*
 * Copyright (c) 2012-2013 Etnaviv Project
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
/* DDS loader - Vaguely based on code by Jon Watte 2002 */
/* Permission granted to use freely, as long as Jon Watte */
/* is held harmless for all possible damages resulting from */
/* your use or failure to use this code. */
/* No warranty is expressed or implied. Use at your own risk, */
/* or not at all. */

#include "dds.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#define DEBUG

//  little-endian, of course
#define DDS_MAGIC 0x20534444

//  DDS_header.dwFlags
#define DDSD_CAPS                   0x00000001
#define DDSD_HEIGHT                 0x00000002
#define DDSD_WIDTH                  0x00000004
#define DDSD_PITCH                  0x00000008
#define DDSD_PIXELFORMAT            0x00001000
#define DDSD_MIPMAPCOUNT            0x00020000
#define DDSD_LINEARSIZE             0x00080000
#define DDSD_DEPTH                  0x00800000

//  DDS_header.sPixelFormat.dwFlags
#define DDPF_ALPHAPIXELS            0x00000001
#define DDPF_ALPHA                  0x00000002
#define DDPF_FOURCC                 0x00000004
#define DDPF_INDEXED                0x00000020
#define DDPF_RGB                    0x00000040
#define DDPF_LUMINANCE              0x00020000

//  DDS_header.sCaps.dwCaps1
#define DDSCAPS_COMPLEX             0x00000008
#define DDSCAPS_TEXTURE             0x00001000
#define DDSCAPS_MIPMAP              0x00400000

//  DDS_header.sCaps.dwCaps2
#define DDSCAPS2_CUBEMAP            0x00000200
#define DDSCAPS2_CUBEMAP_POSITIVEX  0x00000400
#define DDSCAPS2_CUBEMAP_NEGATIVEX  0x00000800
#define DDSCAPS2_CUBEMAP_POSITIVEY  0x00001000
#define DDSCAPS2_CUBEMAP_NEGATIVEY  0x00002000
#define DDSCAPS2_CUBEMAP_POSITIVEZ  0x00004000
#define DDSCAPS2_CUBEMAP_NEGATIVEZ  0x00008000
#define DDSCAPS2_VOLUME             0x00200000

#define MAKEFOURCC(ch0, ch1, ch2, ch3) \
  (uint32_t)( \
    (((uint32_t)(uint8_t)(ch3) << 24) & 0xFF000000) | \
    (((uint32_t)(uint8_t)(ch2) << 16) & 0x00FF0000) | \
    (((uint32_t)(uint8_t)(ch1) <<  8) & 0x0000FF00) | \
     ((uint32_t)(uint8_t)(ch0)        & 0x000000FF) )

#define D3DFMT_DXT1     MAKEFOURCC('D','X','T','1')    //  DXT1 compression texture format
#define D3DFMT_DXT2     MAKEFOURCC('D','X','T','2')    //  DXT2 compression texture format
#define D3DFMT_DXT3     MAKEFOURCC('D','X','T','3')    //  DXT3 compression texture format
#define D3DFMT_DXT4     MAKEFOURCC('D','X','T','4')    //  DXT4 compression texture format
#define D3DFMT_DXT5     MAKEFOURCC('D','X','T','5')    //  DXT5 compression texture format
#define D3DFMT_ETC1     MAKEFOURCC('E','T','C','1')    //  Custom (Ericcson Texture Compression)

struct _DDS_pixelformat {
    unsigned int    dwSize;
    unsigned int    dwFlags;
    unsigned int    dwFourCC;
    unsigned int    dwRGBBitCount;
    unsigned int    dwRBitMask;
    unsigned int    dwGBitMask;
    unsigned int    dwBBitMask;
    unsigned int    dwAlphaBitMask;
};

typedef union _DDS_header {
  struct {
    unsigned int    dwMagic;
    unsigned int    dwSize;
    unsigned int    dwFlags;
    unsigned int    dwHeight;
    unsigned int    dwWidth;
    unsigned int    dwPitchOrLinearSize;
    unsigned int    dwDepth;
    unsigned int    dwMipMapCount;
    unsigned int    dwReserved1[ 11 ];

    //  DDPIXELFORMAT
    struct _DDS_pixelformat sPixelFormat;

    //  DDCAPS2
    struct {
      unsigned int    dwCaps1;
      unsigned int    dwCaps2;
      unsigned int    dwDDSX;
      unsigned int    dwReserved;
    }               sCaps;
    unsigned int    dwReserved2;
  };
  char data[ 128 ];
} DDS_header;

typedef struct _DdsLoadInfo {
    bool compressed;
    unsigned int divSize;
    unsigned int blockBytes;
    uint32_t flags;
    int bitcount;
    uint32_t rmask;
    uint32_t gmask;
    uint32_t bmask;
    uint32_t amask;
} DdsLoadInfo;

static DdsLoadInfo loadInfo[] = {
    [FMT_A8R8G8B8] = {false, 1, 4, DDPF_RGB, 32, 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000},
    [FMT_X8R8G8B8] = {false, 1, 4, DDPF_RGB, 32, 0x00ff0000, 0x0000ff00, 0x000000ff, 0x00000000},
    [FMT_R8G8B8]   = {false, 1, 3, DDPF_RGB, 24, 0x00ff0000, 0x0000ff00, 0x000000ff, 0x00000000},
    [FMT_A1R5G5B5] = {false, 1, 2, DDPF_RGB, 16, 0x00007c00, 0x000003e0, 0x0000001f, 0x00008000},
    [FMT_X1R5G5B5] = {false, 1, 2, DDPF_RGB, 16, 0x00007c00, 0x000003e0, 0x0000001f, 0x00000000},
    [FMT_R5G6B5]   = {false, 1, 2, DDPF_RGB, 16, 0x0000f800, 0x000007e0, 0x0000001f, 0x00000000},
    [FMT_A8]       = {false, 1, 1, DDPF_ALPHA, 8,  0x00000000, 0, 0, 0x000000ff},
    [FMT_L8]       = {false, 1, 1, DDPF_LUMINANCE, 8,  0x000000ff, 0, 0, 0x00000000},
    [FMT_A8L8]     = {false, 1, 2, DDPF_LUMINANCE, 16, 0x000000ff, 0, 0, 0x0000ff00},
    [FMT_DXT1]     = {true, 4, 8, DDPF_FOURCC, 0, D3DFMT_DXT1},
    [FMT_DXT3]     = {true, 4, 16, DDPF_FOURCC, 0, D3DFMT_DXT3},
    [FMT_DXT5]     = {true, 4, 16, DDPF_FOURCC, 0, D3DFMT_DXT5},
    [FMT_ETC1]     = {true, 4, 8, DDPF_FOURCC, 0, D3DFMT_ETC1}
};
#define NUM_FORMATS (sizeof(loadInfo) / sizeof(DdsLoadInfo))

static inline int max(int a, int b) { return a > b ? a : b; }
static inline int min(int a, int b) { return a < b ? a : b; }

static int find_fmt(struct _DDS_pixelformat *pf)
{
    int fmt=0;
    for(fmt=0; fmt<NUM_FORMATS; ++fmt)
    {
        DdsLoadInfo *li = &loadInfo[fmt];
        if(pf->dwFlags & DDPF_FOURCC)
        {
            if(li->flags == DDPF_FOURCC && li->bitcount == 0 && li->rmask == pf->dwFourCC)
            {
                return fmt;
            }
        }
        else if(pf->dwFlags & DDPF_RGB)
        {
            if((li->flags == DDPF_RGB) &&
               (li->bitcount == pf->dwRGBBitCount) &&
               (li->rmask == pf->dwRBitMask) && (li->gmask == pf->dwGBitMask) &&
               (li->bmask == pf->dwBBitMask) && (li->amask == ((pf->dwFlags & DDPF_ALPHAPIXELS)?pf->dwAlphaBitMask:0)))
            {
                return fmt;
            }
        }
        else if(pf->dwFlags & DDPF_LUMINANCE)
        {
            bool formatHasAlpha = pf->dwAlphaBitMask != 0;
            bool ddsHasAlpha = (pf->dwFlags & DDPF_ALPHAPIXELS) != 0;
            if(li->flags == DDPF_LUMINANCE &&
               li->bitcount == pf->dwRGBBitCount &&
               formatHasAlpha == ddsHasAlpha)
            {
                return fmt;
            }
        }
        else if(pf->dwFlags & DDPF_ALPHA)
        {
            if(li->flags == DDPF_ALPHA &&
               li->bitcount == pf->dwRGBBitCount)
            {
                return fmt;
            }
        }
    }
    return -1;
}

bool dds_load_file(FILE *f, dds_texture **out)
{
    DDS_header hdr;
    unsigned int x = 0;
    unsigned int y = 0;
    unsigned int mipMapCount = 0;
    if(f == NULL || out == NULL)
        return false;
    fread( &hdr, sizeof( hdr ), 1, f );
    assert( hdr.dwMagic == DDS_MAGIC );
    assert( hdr.dwSize == 124 );

    if( hdr.dwMagic != DDS_MAGIC || hdr.dwSize != 124 ||
      !(hdr.dwFlags & DDSD_PIXELFORMAT) || !(hdr.dwFlags & DDSD_CAPS) )
    {
        goto failure;
    }

    unsigned int xSize = hdr.dwWidth;
    unsigned int ySize = hdr.dwHeight;
    int fmt = find_fmt(&hdr.sPixelFormat);
    if(fmt == -1) {
#ifdef DEBUG
        printf("Unknown format\n");
#endif
        goto failure;
    }
    DdsLoadInfo *li = &loadInfo[fmt];
    dds_texture *rv = (dds_texture*)malloc(sizeof(dds_texture));
    if(!rv)
        goto failure;

    //fixme: do cube maps later
    //fixme: do 3d later
    x = xSize;
    y = ySize;
    mipMapCount = (hdr.dwFlags & DDSD_MIPMAPCOUNT) ? hdr.dwMipMapCount : 1;
#ifdef DEBUG
    printf("Base size: %ix%i, %i mips\n", x, y, mipMapCount);
#endif

    // size of first mip
    size_t size = max(li->divSize, x)/li->divSize * max(li->divSize, y)/li->divSize * li->blockBytes;
    if((hdr.dwFlags & DDSD_LINEARSIZE) && (hdr.dwPitchOrLinearSize != size))
    {
#ifdef DEBUG
        printf("Size mismatch: %i versus %i\n", hdr.dwPitchOrLinearSize, size);
#endif
        goto failure;
    }
    if((hdr.dwFlags & DDSD_PITCH) && (hdr.dwPitchOrLinearSize != x*li->blockBytes))
        goto failure;

    rv->fmt = fmt;
    rv->num_slices = 1;
    rv->num_mipmaps = mipMapCount;
    rv->div_size = li->divSize;
    rv->block_bytes = li->blockBytes;
    rv->slices = malloc(sizeof(dds_mipmap*) * rv->num_slices);
    rv->slices[0] = malloc(sizeof(dds_mipmap) * rv->num_mipmaps);
#ifdef DEBUG
    printf("(format %i, %i mipmaps)\n", rv->fmt, rv->num_mipmaps);
#endif

    /* compute offsets */
    size_t offset = 0;
    for(unsigned int ix = 0; ix < mipMapCount; ++ix)
    {
        dds_mipmap *mip = &rv->slices[0][ix];
        mip->width = x;
        mip->height = y;
        mip->stride = max( li->divSize, x )/li->divSize * li->blockBytes;
        mip->offset = offset;
        mip->size = size;
#ifdef DEBUG
        printf("%08x Loading mipmap %i: %ix%i (%i) stride=%i\n",
                (int)mip->offset, ix, (int)mip->width, (int)mip->height, (int)mip->size,
                (int)mip->stride);
#endif
        offset += size;
        if(x != 1)
            x = x >> 1;
        if(y != 1)
            y = y >> 1;
        size = max( li->divSize, x )/li->divSize * max( li->divSize, y )/li->divSize * li->blockBytes;
    }
    rv->data = malloc(offset);
    rv->size = offset;
    if(!rv->data)
        goto failure;
    fread(rv->data, rv->size, 1, f);
    for(unsigned int ix = 0; ix < mipMapCount; ++ix)
    {
        rv->slices[0][ix].data = (void*)((size_t)rv->data + rv->slices[0][ix].offset);
    }
    *out = rv;

    return true;
failure:
    return false;
}

bool dds_load(const char *filename, dds_texture **out)
{
    FILE *f = fopen(filename, "rb");
    if(f == NULL)
    {
#ifdef DEBUG
        printf("Cannot open texture file\n");
#endif
        return false;
    }
    bool ret = dds_load_file(f, out);
    fclose(f);
    return ret;
}

void dds_free(dds_texture *tex)
{
    if(tex == NULL)
        return;
    free(tex->slices);
    free(tex->data);
    free(tex);
}
#if 0
int main(int argc, char **argv)
{
    dds_texture *tex = 0;
    if(dds_load(argv[1], &tex))
    {
        printf("Succesfully loaded\n");
    }
}
#endif
