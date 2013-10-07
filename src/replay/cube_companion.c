#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <string.h>

#include "write_bmp.h"
#include "viv_raw.h"
#include "companion.h"

#include "companion_cmd.h"
/* TODO: should actually update context as we go,
   a context switch would currently revert state and likely result in corrupted rendering.
 */
#include "context_cmd.h"


int main(int argc, char **argv)
{
    int rv;
    struct viv_conn *conn = 0;
    rv = viv_open(VIV_HW_3D, &conn);
    if(rv!=0)
    {
        fprintf(stderr, "Error opening device\n");
        exit(1);
    }
    printf("Succesfully opened device\n");
    viv_show_chip_info(conn);

    /* allocate command buffer (blob uses four command buffers, but we don't even fill one) */
    viv_addr_t buf0_physical = 0;
    void *buf0_logical = 0;
    if(viv_alloc_contiguous(conn, 0x8000, &buf0_physical, &buf0_logical, NULL)!=0)
    {
        fprintf(stderr, "Error allocating host memory\n");
        exit(1);
    }
    printf("Allocated buffer: phys=%08x log=%08x\n", (uint32_t)buf0_physical, (uint32_t)buf0_logical);

    /* allocate main render target */
    gcuVIDMEM_NODE_PTR rt_node = 0;
    if(viv_alloc_linear_vidmem(conn, 0x1a0000, 0x40, gcvSURF_RENDER_TARGET, gcvPOOL_DEFAULT, &rt_node, NULL)!=0)
    {
        fprintf(stderr, "Error allocating render target buffer memory\n");
        exit(1);
    }
    printf("Allocated render target node: node=%08x\n", (uint32_t)rt_node);
    viv_addr_t rt_physical = 0; /* ADDR_A */
    void *rt_logical = 0;
    if(viv_lock_vidmem(conn, rt_node, &rt_physical, &rt_logical)!=0)
    {
        fprintf(stderr, "Error locking render target memory\n");
        exit(1);
    }
    printf("Locked render target: phys=%08x log=%08x\n", (uint32_t)rt_physical, (uint32_t)rt_logical);
    memset(rt_logical, 0xff, 0x1a0000); /* clear previous result just in case, test that clearing works */

    /* allocate tile status for main render target */
    gcuVIDMEM_NODE_PTR rt_ts_node = 0;
    if(viv_alloc_linear_vidmem(conn, 0x1a00, 0x40, gcvSURF_TILE_STATUS, gcvPOOL_DEFAULT, &rt_ts_node, NULL)!=0)
    {
        fprintf(stderr, "Error allocating render target tile status memory\n");
        exit(1);
    }
    printf("Allocated render target tile status node: node=%08x\n", (uint32_t)rt_ts_node);
    viv_addr_t rt_ts_physical = 0; /* ADDR_B */
    void *rt_ts_logical = 0;
    if(viv_lock_vidmem(conn, rt_ts_node, &rt_ts_physical, &rt_ts_logical)!=0)
    {
        fprintf(stderr, "Error locking render target memory\n");
        exit(1);
    }
    printf("Locked render target ts: phys=%08x log=%08x\n", (uint32_t)rt_ts_physical, (uint32_t)rt_ts_logical);

    /* allocate depth for main render target */
    gcuVIDMEM_NODE_PTR z_node = 0;
    if(viv_alloc_linear_vidmem(conn, 0xd0000, 0x40, gcvSURF_DEPTH, gcvPOOL_DEFAULT, &z_node, NULL)!=0)
    {
        fprintf(stderr, "Error allocating depth memory\n");
        exit(1);
    }
    printf("Allocated depth node: node=%08x\n", (uint32_t)z_node);
    viv_addr_t z_physical = 0; /* ADDR_C */
    void *z_logical = 0;
    if(viv_lock_vidmem(conn, z_node, &z_physical, &z_logical)!=0)
    {
        fprintf(stderr, "Error locking depth target memory\n");
        exit(1);
    }
    printf("Locked depth target: phys=%08x log=%08x\n", (uint32_t)z_physical, (uint32_t)z_logical);

    /* allocate depth ts for main render target */
    gcuVIDMEM_NODE_PTR z_ts_node = 0;
    if(viv_alloc_linear_vidmem(conn, 0xd00, 0x40, gcvSURF_TILE_STATUS, gcvPOOL_DEFAULT, &z_ts_node, NULL)!=0)
    {
        fprintf(stderr, "Error allocating depth memory\n");
        exit(1);
    }
    printf("Allocated depth ts node: node=%08x\n", (uint32_t)z_ts_node);
    viv_addr_t z_ts_physical = 0; /* ADDR_D */
    void *z_ts_logical = 0;
    if(viv_lock_vidmem(conn, z_ts_node, &z_ts_physical, &z_ts_logical)!=0)
    {
        fprintf(stderr, "Error locking depth target ts memory\n");
        exit(1);
    }
    printf("Locked depth ts target: phys=%08x log=%08x\n", (uint32_t)z_ts_physical, (uint32_t)z_ts_logical);

    /* allocate vertex buffer */
    gcuVIDMEM_NODE_PTR vtx_node = 0;
    if(viv_alloc_linear_vidmem(conn, 0x60000, 0x40, gcvSURF_VERTEX, gcvPOOL_DEFAULT, &vtx_node, NULL)!=0)
    {
        fprintf(stderr, "Error allocating vertex memory\n");
        exit(1);
    }
    printf("Allocated vertex node: node=%08x\n", (uint32_t)vtx_node);
    viv_addr_t vtx_physical = 0; /* ADDR_E */
    void *vtx_logical = 0;
    if(viv_lock_vidmem(conn, vtx_node, &vtx_physical, &vtx_logical)!=0)
    {
        fprintf(stderr, "Error locking vertex memory\n");
        exit(1);
    }
    printf("Locked vertex memory: phys=%08x log=%08x\n", (uint32_t)vtx_physical, (uint32_t)vtx_logical);

    /* allocate aux render target */
    gcuVIDMEM_NODE_PTR aux_rt_node = 0;
    if(viv_alloc_linear_vidmem(conn, 0x4000, 0x40, gcvSURF_RENDER_TARGET, gcvPOOL_SYSTEM /*why?*/, &aux_rt_node, NULL)!=0)
    {
        fprintf(stderr, "Error allocating aux render target buffer memory\n");
        exit(1);
    }
    printf("Allocated aux render target node: node=%08x\n", (uint32_t)aux_rt_node);
    viv_addr_t aux_rt_physical = 0; /* ADDR_F */
    void *aux_rt_logical = 0;
    if(viv_lock_vidmem(conn, aux_rt_node, &aux_rt_physical, &aux_rt_logical)!=0)
    {
        fprintf(stderr, "Error locking aux render target memory\n");
        exit(1);
    }
    printf("Locked aux render target: phys=%08x log=%08x\n", (uint32_t)aux_rt_physical, (uint32_t)aux_rt_logical);

    /* allocate tile status for aux render target */
    gcuVIDMEM_NODE_PTR aux_rt_ts_node = 0;
    if(viv_alloc_linear_vidmem(conn, 0x100, 0x40, gcvSURF_TILE_STATUS, gcvPOOL_DEFAULT, &aux_rt_ts_node, NULL)!=0)
    {
        fprintf(stderr, "Error allocating aux render target tile status memory\n");
        exit(1);
    }
    printf("Allocated aux render target tile status node: node=%08x\n", (uint32_t)aux_rt_ts_node);
    viv_addr_t aux_rt_ts_physical = 0; /* ADDR_G */
    void *aux_rt_ts_logical = 0;
    if(viv_lock_vidmem(conn, aux_rt_ts_node, &aux_rt_ts_physical, &aux_rt_ts_logical)!=0)
    {
        fprintf(stderr, "Error locking aux ts render target memory\n");
        exit(1);
    }
    printf("Locked aux render target ts: phys=%08x log=%08x\n", (uint32_t)aux_rt_ts_physical, (uint32_t)aux_rt_ts_logical);

    /* Submit command buffer 1 */
    /* First build context state map */
    size_t stateCount = 0x1d00;
    uint32_t *contextMap = malloc(stateCount * 4);
    memset(contextMap, 0, stateCount*4);
    for(int idx=0; idx<sizeof(contextbuf_addr)/sizeof(address_index_t); ++idx)
    {
        contextMap[contextbuf_addr[idx].address / 4] = contextbuf_addr[idx].index;
    }

    struct _gcoCMDBUF commandBuffer = {
        .object = {
            .type = gcvOBJ_COMMANDBUFFER
        },
        .physical = (void*)buf0_physical,
        .logical = (void*)buf0_logical,
        .bytes = 0x8000,
        .startOffset = 0x0,
    };
    struct _gcoCONTEXT contextBuffer = {
        .object = {
            .type = gcvOBJ_CONTEXT
        },
        .id = 0x0, // Actual ID will be returned here
        .map = contextMap,
        .stateCount = stateCount,
        .buffer = contextbuf,
        .pipe3DIndex = 0x2d6, // XXX should not be hardcoded
        .pipe2DIndex = 0x106e,
        .linkIndex = 0x1076,
        .inUseIndex = 0x1078,
        .bufferSize = 0x41e4,
        .bytes = 0x0, // Number of bytes at physical, logical
        .physical = (void*)0x0,
        .logical = (void*)0x0,
        .link = (void*)0x0, // Logical address of link
        .initialPipe = 0x1,
        .entryPipe = 0x0,
        .currentPipe = 0x0,
        .postCommit = 1,
        .inUse = (int*)0x0, // Logical address of inUse
        .lastAddress = 0xffffffff, // Not used by kernel
        .lastSize = 0x2, // Not used by kernel
        .lastIndex = 0x106a, // Not used by kernel
        .lastFixed = 0, // Not used by kernel
    };
    commandBuffer.free = commandBuffer.bytes - 0x8; /* Always keep 0x8 at end of buffer for kernel driver */
    /* Set addresses in first command buffer */
    cmdbuf1[0x57] = cmdbuf1[0x67] = cmdbuf1[0x9f] = cmdbuf1[0xbb] = cmdbuf1[0xd9] = cmdbuf1[0xfb] = cmdbuf1[0x119] = cmdbuf1[0x135] = cmdbuf1[0x153] = rt_physical;
    cmdbuf1[0x65] = cmdbuf1[0x9d] = cmdbuf1[0xb9] = cmdbuf1[0xd7] = cmdbuf1[0xe5] = cmdbuf1[0xf9] = cmdbuf1[0x117] = cmdbuf1[0x133] = cmdbuf1[0x151] = rt_ts_physical;
    cmdbuf1[0x6d] = cmdbuf1[0x7f] = cmdbuf1[0x175] = z_physical;
    cmdbuf1[0x7d] = cmdbuf1[0x15f] = cmdbuf1[0x173] = z_ts_physical;
    cmdbuf1[0x89] = cmdbuf1[0x8f] = cmdbuf1[0x93] = cmdbuf1[0xa5] = cmdbuf1[0xab] = cmdbuf1[0xaf] = cmdbuf1[0xc3] = cmdbuf1[0xc9] = cmdbuf1[0xcd] = cmdbuf1[0x103] = cmdbuf1[0x109] = cmdbuf1[0x10d] = cmdbuf1[0x11f] = cmdbuf1[0x125] = cmdbuf1[0x129] = cmdbuf1[0x13d] = cmdbuf1[0x143] = cmdbuf1[0x147] = aux_rt_physical;
    cmdbuf1[0x87] = cmdbuf1[0xa3] = cmdbuf1[0xc1] = cmdbuf1[0x101] = cmdbuf1[0x11d] = cmdbuf1[0x13b] = aux_rt_ts_physical;

    /* Submit first command buffer */
    commandBuffer.startOffset = 0;
    memcpy((void*)((size_t)commandBuffer.logical + commandBuffer.startOffset), cmdbuf1, sizeof(cmdbuf1));
    commandBuffer.offset = commandBuffer.startOffset + sizeof(cmdbuf1);
    commandBuffer.free -= sizeof(cmdbuf1) + 0x18;
    printf("[1] startOffset=%08x, offset=%08x, free=%08x\n", (uint32_t)commandBuffer.startOffset, (uint32_t)commandBuffer.offset, (uint32_t)commandBuffer.free);
    if(viv_commit(conn, &commandBuffer, &contextBuffer) != 0)
    {
        fprintf(stderr, "Error committing first command buffer\n");
        exit(1);
    }
    
    /* After the first COMMIT, allocate contiguous memory for context and set
     * bytes, physical, logical, link, inUse */
    printf("Context assigned index: %i\n", (uint32_t)contextBuffer.id);
    viv_addr_t cbuf0_physical = 0;
    void *cbuf0_logical = 0;
    size_t cbuf0_bytes = 0;
    if(viv_alloc_contiguous(conn, contextBuffer.bufferSize, &cbuf0_physical, &cbuf0_logical, &cbuf0_bytes)!=0)
    {
        fprintf(stderr, "Error allocating contiguous host memory for context\n");
        exit(1);
    }
    printf("Allocated buffer (size 0x%x) for context: phys=%08x log=%08x\n", (int)cbuf0_bytes, (int)cbuf0_physical, (int)cbuf0_logical);
    contextBuffer.bytes = cbuf0_bytes; /* actual size of buffer */
    contextBuffer.physical = (void*)cbuf0_physical;
    contextBuffer.logical = cbuf0_logical;
    contextBuffer.link = ((uint32_t*)cbuf0_logical) + contextBuffer.linkIndex;
    contextBuffer.inUse = (int*)(((uint32_t*)cbuf0_logical) + contextBuffer.inUseIndex);

    /* Create signal */
    int sig_id = 0;
    if(viv_user_signal_create(conn, 0, &sig_id) != 0) /* automatic resetting signal */
    {
        fprintf(stderr, "Cannot create user signal\n");
        exit(1);
    }
    printf("Created user signal %i\n", sig_id);
    
    /* Queue and wait for signal */
    if(viv_event_queue_signal(conn, sig_id, gcvKERNEL_PIXEL) != 0)
    {
        fprintf(stderr, "Cannot queue GPU signal\n");
        exit(1);
    }
    if(viv_user_signal_wait(conn, sig_id, VIV_WAIT_INDEFINITE) != 0)
    {
        fprintf(stderr, "Cannot wait for signal\n");
        exit(1);
    }

    /* Allocate and map texture memory (ADDR_H) */
    gcuVIDMEM_NODE_PTR tex_node = 0;
    if(viv_alloc_linear_vidmem(conn, 0x100000, 0x40, gcvSURF_TEXTURE, gcvPOOL_DEFAULT, &tex_node, NULL)!=0)
    {
        fprintf(stderr, "Error allocating tex memory\n");
        exit(1);
    }
    printf("Allocated tex: node=%08x\n", (uint32_t)tex_node);
    viv_addr_t tex_physical = 0; /* ADDR_H */
    void *tex_logical = 0;
    if(viv_lock_vidmem(conn, tex_node, &tex_physical, &tex_logical)!=0)
    {
        fprintf(stderr, "Error locking tex memory\n");
        exit(1);
    }
    printf("Locked tex: phys=%08x log=%08x\n", (uint32_t)tex_physical, (uint32_t)tex_logical);

    /* Allocate and map more vertex memory (ADDR_I), ADDR_E is unused */
    gcuVIDMEM_NODE_PTR vtx2_node = 0;
    if(viv_alloc_linear_vidmem(conn, 0x5ef80, 0x8, gcvSURF_VERTEX, gcvPOOL_DEFAULT, &vtx2_node, NULL)!=0)
    {
        fprintf(stderr, "Error allocating vtx2 memory\n");
        exit(1);
    }
    printf("Allocated vtx2: node=%08x\n", (uint32_t)vtx2_node);
    viv_addr_t vtx2_physical = 0; /* ADDR_I */
    void *vtx2_logical = 0;
    if(viv_lock_vidmem(conn, vtx2_node, &vtx2_physical, &vtx2_logical)!=0)
    {
        fprintf(stderr, "Error locking vtx2 memory\n");
        exit(1);
    }
    printf("Locked vtx2: phys=%08x log=%08x\n", (uint32_t)vtx2_physical, (uint32_t)vtx2_logical);

    /* Interleave companion cube vertex data into ADDR_I */
    memset(vtx2_logical, 0, 0x5ef80);
    float *vertices_array = companion_vertices_array();
    float *texture_coordinates_array =
            companion_texture_coordinates_array();
    float *normals_array = companion_normals_array();
    for(int vert=0; vert<COMPANION_ARRAY_COUNT; ++vert)
    {
        int dest_idx = vert * (3 + 3 + 2);
        for(int comp=0; comp<3; ++comp)
            ((float*)vtx2_logical)[dest_idx+comp+0] = vertices_array[vert*3 + comp]; /* 0 */
        for(int comp=0; comp<3; ++comp)
            ((float*)vtx2_logical)[dest_idx+comp+3] = normals_array[vert*3 + comp]; /* 1 */
        for(int comp=0; comp<2; ++comp)
            ((float*)vtx2_logical)[dest_idx+comp+6] = texture_coordinates_array[vert*2 + comp]; /* 2 */
    }
    
    /* Fill in texture (convert from RGB linear to tiled) */
#if 1
#define TILE_WIDTH (4)
#define TILE_HEIGHT (4)
#define TILE_WORDS (TILE_WIDTH*TILE_HEIGHT)
    unsigned ytiles = COMPANION_TEXTURE_HEIGHT / TILE_HEIGHT;
    unsigned xtiles = COMPANION_TEXTURE_WIDTH / TILE_WIDTH;
    unsigned dst_stride = xtiles * TILE_WORDS;

    for(unsigned ty=0; ty<ytiles; ++ty)
    {
        for(unsigned tx=0; tx<xtiles; ++tx)
        {
            unsigned ofs = ty * dst_stride + tx * TILE_WORDS;
            for(unsigned y=0; y<TILE_HEIGHT; ++y)
            {
                for(unsigned x=0; x<TILE_WIDTH; ++x)
                {
                    unsigned srcy = ty*TILE_HEIGHT + y;
                    unsigned srcx = tx*TILE_WIDTH + x;
                    unsigned src_ofs = (srcy*COMPANION_TEXTURE_WIDTH+srcx)*3;
                    unsigned r,g,b,a;
                    r = ((uint8_t*)companion_texture)[src_ofs+0];
                    g = ((uint8_t*)companion_texture)[src_ofs+1];
                    b = ((uint8_t*)companion_texture)[src_ofs+2];
                    a = 255;

                    ((uint32_t*)tex_logical)[ofs] = ((a&0xFF) << 24) | ((b&0xFF) << 16) | ((g&0xFF) << 8) | (r&0xFF);
                    ofs += 1;
                }
            }
        }
    }
#endif
#if 0
    int texfd = open("/data/mine/texture.raw", O_RDONLY); 
    read(texfd, tex_logical, 512*512*4);
    close(texfd);
#endif

    /* Submit command buffer 2 */
    cmdbuf2[0x3b] = tex_physical;
    cmdbuf2[0x125] = vtx2_physical;

    commandBuffer.startOffset = commandBuffer.offset + 0x18; /* Make space for LINK */
    memcpy((void*)((size_t)commandBuffer.logical + commandBuffer.startOffset), cmdbuf2, sizeof(cmdbuf2));
    commandBuffer.offset = commandBuffer.startOffset + sizeof(cmdbuf2);
    commandBuffer.free -= sizeof(cmdbuf2) + 0x18;
    printf("[2] startOffset=%08x, offset=%08x, free=%08x\n", (uint32_t)commandBuffer.startOffset, (uint32_t)commandBuffer.offset, (uint32_t)commandBuffer.free);
    if(viv_commit(conn, &commandBuffer, &contextBuffer) != 0)
    {
        fprintf(stderr, "Error committing second command buffer\n");
        exit(1);
    }

    /* Submit command buffer 3 */
    cmdbuf3[0x1d] = cmdbuf3[0x1f] = rt_physical;

    commandBuffer.startOffset = commandBuffer.offset + 0x18;
    memcpy((void*)((size_t)commandBuffer.logical + commandBuffer.startOffset), cmdbuf3, sizeof(cmdbuf3));
    commandBuffer.offset = commandBuffer.startOffset + sizeof(cmdbuf3);
    commandBuffer.free -= sizeof(cmdbuf3) + 0x18;
    printf("[3] startOffset=%08x, offset=%08x, free=%08x\n", (uint32_t)commandBuffer.startOffset, (uint32_t)commandBuffer.offset, (uint32_t)commandBuffer.free);
    if(viv_commit(conn, &commandBuffer, &contextBuffer) != 0)
    {
        fprintf(stderr, "Error committing third command buffer\n");
        exit(1);
    }

    /* Submit command buffer 4 */
    cmdbuf4[0x9] = aux_rt_ts_physical;
    cmdbuf4[0xb] = cmdbuf4[0x11] = cmdbuf4[0x15] = aux_rt_physical;
    cmdbuf4[0x21] = rt_physical;
    cmdbuf4[0x1f] = rt_ts_physical;

    commandBuffer.startOffset = commandBuffer.offset + 0x18;
    memcpy((void*)((size_t)commandBuffer.logical + commandBuffer.startOffset), cmdbuf4, sizeof(cmdbuf4));
    commandBuffer.offset = commandBuffer.startOffset + sizeof(cmdbuf4);
    commandBuffer.free -= sizeof(cmdbuf4) + 0x18;
    printf("[4] startOffset=%08x, offset=%08x, free=%08x\n", (uint32_t)commandBuffer.startOffset, (uint32_t)commandBuffer.offset, (uint32_t)commandBuffer.free);
    if(viv_commit(conn, &commandBuffer, &contextBuffer) != 0)
    {
        fprintf(stderr, "Error committing command buffer 4\n");
        exit(1);
    }

    /* Submit event, and wait */
    if(viv_event_queue_signal(conn, sig_id, gcvKERNEL_PIXEL) != 0)
    {
        fprintf(stderr, "Cannot queue GPU signal\n");
        exit(1);
    }
    if(viv_user_signal_wait(conn, sig_id, VIV_WAIT_INDEFINITE) != 0)
    {
        fprintf(stderr, "Cannot wait for signal\n");
        exit(1);
    }

    /* Allocate bitmap memory, map */
    gcuVIDMEM_NODE_PTR bmp_node = 0;
    if(viv_alloc_linear_vidmem(conn, 0x177000, 0x40, gcvSURF_BITMAP, gcvPOOL_DEFAULT, &bmp_node, NULL)!=0)
    {
        fprintf(stderr, "Error allocating bitmap status memory\n");
        exit(1);
    }
    printf("Allocated bitmap node: node=%08x\n", (uint32_t)bmp_node);
    viv_addr_t bmp_physical = 0; /* ADDR_J */
    void *bmp_logical = 0;
    if(viv_lock_vidmem(conn, bmp_node, &bmp_physical, &bmp_logical)!=0)
    {
        fprintf(stderr, "Error locking bmp memory\n");
        exit(1);
    }
    memset(bmp_logical, 0xff, 0x177000); /* clear previous result */
    printf("Locked bmp: phys=%08x log=%08x\n", (uint32_t)bmp_physical, (uint32_t)bmp_logical);

    /* Submit command buffer 5 */
    cmdbuf5[0x19] = rt_physical;
    cmdbuf5[0x1b] = bmp_physical;

    commandBuffer.startOffset = commandBuffer.offset + 0x18;
    memcpy((void*)((size_t)commandBuffer.logical + commandBuffer.startOffset), cmdbuf5, sizeof(cmdbuf5));
    commandBuffer.offset = commandBuffer.startOffset + sizeof(cmdbuf5);
    commandBuffer.free -= sizeof(cmdbuf5) + 0x18;
    printf("[5] startOffset=%08x, offset=%08x, free=%08x\n", (uint32_t)commandBuffer.startOffset, (uint32_t)commandBuffer.offset, (uint32_t)commandBuffer.free);
    if(viv_commit(conn, &commandBuffer, &contextBuffer) != 0)
    {
        fprintf(stderr, "Error committing command buffer 5\n");
        exit(1);
    }

    /* Submit event queue with SIGNAL, fromWhere=gcvKERNEL_PIXEL */
    if(viv_event_queue_signal(conn, sig_id, gcvKERNEL_PIXEL) != 0)
    {
        fprintf(stderr, "Cannot queue GPU signal\n");
        exit(1);
    }
    /* Wait for signal */
    if(viv_user_signal_wait(conn, sig_id, VIV_WAIT_INDEFINITE) != 0)
    {
        fprintf(stderr, "Cannot wait for signal\n");
        exit(1);
    }

    bmp_dump32(bmp_logical, 800, 480, false, "/mnt/sdcard/replay.bmp");
    /* Unlock video memory */
    if(viv_unlock_vidmem(conn, bmp_node, gcvSURF_BITMAP, 1) != 0)
    {
        fprintf(stderr, "Cannot unlock vidmem\n");
        exit(1);
    }
    /*
    for(int x=0; x<0x700; ++x)
    {
        uint32_t value = ((uint32_t*)rt_ts_logical)[x];
        printf("Sample ts: %x %08x\n", x*4, value);
    }*/
    printf("Contextbuffer used %i\n", *contextBuffer.inUse);
    viv_close(conn);
    return 0;
}

