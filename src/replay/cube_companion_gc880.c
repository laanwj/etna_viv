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

#include "companion_cmd_gc880.h"


/* TODO: should actually update context as we go,
   a context switch would currently revert state and likely result in corrupted rendering.
 */
//#include "context_cmd.h"


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
    
    gcsHAL_INTERFACE id = {};
    id.command = gcvHAL_ATTACH;
    if((viv_invoke(conn, &id)) != gcvSTATUS_OK)
    {
        #ifdef DEBUG
        fprintf(stderr, "Error attaching to GPU\n");
        #endif
        exit(1);
    }
    gckCONTEXT context = id.u.Attach.context;

    /* allocate command buffer (blob uses four command buffers, but we don't even fill one) */
    viv_addr_t buf0_physical = 0;
    void *buf0_logical = 0;
    if(viv_alloc_contiguous(conn, 0x20000, &buf0_physical, &buf0_logical, NULL)!=0)
    {
        fprintf(stderr, "Error allocating host memory\n");
        exit(1);
    }
    printf("Allocated buffer: phys=%08x log=%08x\n", (uint32_t)buf0_physical, (uint32_t)buf0_logical);

    /* allocate main render target */
    gcuVIDMEM_NODE_PTR color_surface_node = 0;
    if(viv_alloc_linear_vidmem(conn, 0x1ab000, 0x40, gcvSURF_RENDER_TARGET, gcvPOOL_DEFAULT, &color_surface_node, NULL)!=0)
    {
        fprintf(stderr, "Error allocating render target buffer memory\n");
        exit(1);
    }
    printf("Allocated render target node: node=%08x\n", (uint32_t)color_surface_node);
    viv_addr_t color_surface_physical = 0;
    void *color_surface_logical = 0;
    if(viv_lock_vidmem(conn, color_surface_node, &color_surface_physical, &color_surface_logical)!=0)
    {
        fprintf(stderr, "Error locking render target memory\n");
        exit(1);
    }
    printf("Locked render target: phys=%08x log=%08x\n", (uint32_t)color_surface_physical, (uint32_t)color_surface_logical);

    /* allocate tile status for main render target */
    gcuVIDMEM_NODE_PTR color_status_node = 0;
    if(viv_alloc_linear_vidmem(conn, 0x1b00, 0x40, gcvSURF_TILE_STATUS, gcvPOOL_DEFAULT, &color_status_node, NULL)!=0)
    {
        fprintf(stderr, "Error allocating render target tile status memory\n");
        exit(1);
    }
    printf("Allocated render target tile status node: node=%08x\n", (uint32_t)color_status_node);
    viv_addr_t color_status_physical = 0;
    void *color_status_logical = 0;
    if(viv_lock_vidmem(conn, color_status_node, &color_status_physical, &color_status_logical)!=0)
    {
        fprintf(stderr, "Error locking render target memory\n");
        exit(1);
    }
    printf("Locked render target ts: phys=%08x log=%08x\n", (uint32_t)color_status_physical, (uint32_t)color_status_logical);

    /* allocate depth for main render target */
    gcuVIDMEM_NODE_PTR depth_surface_node = 0;
    if(viv_alloc_linear_vidmem(conn, 0xd1000, 0x40, gcvSURF_DEPTH, gcvPOOL_DEFAULT, &depth_surface_node, NULL)!=0)
    {
        fprintf(stderr, "Error allocating depth memory\n");
        exit(1);
    }
    printf("Allocated depth node: node=%08x\n", (uint32_t)depth_surface_node);
    viv_addr_t depth_surface_physical = 0;
    void *depth_surface_logical = 0;
    if(viv_lock_vidmem(conn, depth_surface_node, &depth_surface_physical, &depth_surface_logical)!=0)
    {
        fprintf(stderr, "Error locking depth target memory\n");
        exit(1);
    }
    printf("Locked depth target: phys=%08x log=%08x\n", (uint32_t)depth_surface_physical, (uint32_t)depth_surface_logical);
    
    /* allocate depth ts for main render target */
    gcuVIDMEM_NODE_PTR depth_status_node = 0;
    if(viv_alloc_linear_vidmem(conn, 0xe00, 0x40, gcvSURF_TILE_STATUS, gcvPOOL_DEFAULT, &depth_status_node, NULL)!=0)
    {
        fprintf(stderr, "Error allocating depth memory\n");
        exit(1);
    }
    printf("Allocated depth ts node: node=%08x\n", (uint32_t)depth_status_node);
    viv_addr_t depth_status_physical = 0;
    void *depth_status_logical = 0;
    if(viv_lock_vidmem(conn, depth_status_node, &depth_status_physical, &depth_status_logical)!=0)
    {
        fprintf(stderr, "Error locking depth target ts memory\n");
        exit(1);
    }
    printf("Locked depth ts target: phys=%08x log=%08x\n", (uint32_t)depth_status_physical, (uint32_t)depth_status_logical);

    /* allocate vertex buffer */
    gcuVIDMEM_NODE_PTR vtx_node = 0;
    if(viv_alloc_linear_vidmem(conn, 0x100000, 0x40, gcvSURF_VERTEX, gcvPOOL_DEFAULT, &vtx_node, NULL)!=0)
    {
        fprintf(stderr, "Error allocating vertex memory\n");
        exit(1);
    }
    printf("Allocated vertex node: node=%08x\n", (uint32_t)vtx_node);
    viv_addr_t vtx_physical = 0;
    void *vtx_logical = 0;
    if(viv_lock_vidmem(conn, vtx_node, &vtx_physical, &vtx_logical)!=0)
    {
        fprintf(stderr, "Error locking vertex memory\n");
        exit(1);
    }
    printf("Locked vertex memory: phys=%08x log=%08x\n", (uint32_t)vtx_physical, (uint32_t)vtx_logical);

    /* allocate tile status for aux render target */
    gcuVIDMEM_NODE_PTR rs_dest_node = 0;
    if(viv_alloc_linear_vidmem(conn, 0x1a0000, 0x40, gcvSURF_BITMAP, gcvPOOL_DEFAULT, &rs_dest_node, NULL)!=0)
    {
        fprintf(stderr, "Error allocating aux render target tile status memory\n");
        exit(1);
    }
    printf("Allocated aux render target tile status node: node=%08x\n", (uint32_t)rs_dest_node);
    viv_addr_t rs_dest_physical = 0;
    void *rs_dest_logical = 0;
    if(viv_lock_vidmem(conn, rs_dest_node, &rs_dest_physical, &rs_dest_logical)!=0)
    {
        fprintf(stderr, "Error locking aux ts render target memory\n");
        exit(1);
    }
    printf("Locked aux render target ts: phys=%08x log=%08x\n", (uint32_t)rs_dest_physical, (uint32_t)rs_dest_logical);
    
    int texture_size[] = {0x100000, 0x040000, 0x010000, 0x004000, 0x001000, 0x000400, 0x000200, 0x000100, 0x000100, 0x000100};
    gcuVIDMEM_NODE_PTR text_lod[10];
    viv_addr_t text_lod_physical[10];
    void* text_lod_logical[10];
    for(int idx=0; idx<10; idx++)
    {
        if (viv_alloc_linear_vidmem(conn, texture_size[idx], 0x40, gcvSURF_TEXTURE, gcvPOOL_DEFAULT, &text_lod[idx], NULL) != 0)
        {
            fprintf(stderr, "Error locking texture nr %d\n", idx);
            exit(1);
        }
        if(viv_lock_vidmem(conn, text_lod[idx], &text_lod_physical[idx], &text_lod_logical[idx]) != 0)
        {
            fprintf(stderr, "Error locking texture memory\n");
            exit(1);
        }
        printf("Locked texture target nr %d: phys=%08x log=%08x\n", idx, (uint32_t)text_lod_physical[idx], (uint32_t)text_lod_logical[idx]);
    }
    
    /* Interleave companion cube vertex data into ADDR_I */
    //memset(vtx_logical, 0, 0x5ef80);
    float *vertices_array = companion_vertices_array();
    float *texture_coordinates_array = companion_texture_coordinates_array();
    float *normals_array = companion_normals_array();
    int dest_idx = 0;
    for(int vert=0; vert<COMPANION_ARRAY_COUNT*3; ++vert)
    {
        ((float*)vtx_logical)[dest_idx] = vertices_array[vert];
        dest_idx++;
    }
    for(int vert=0; vert<COMPANION_ARRAY_COUNT*3; ++vert)
    {
        ((float*)vtx_logical)[dest_idx] = normals_array[vert];
        dest_idx++;
    }
    for(int vert=0; vert<COMPANION_ARRAY_COUNT*2; ++vert)
    {
        ((float*)vtx_logical)[dest_idx] = texture_coordinates_array[vert];
        dest_idx++;
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
                    
                    ((uint32_t*)text_lod_logical[0])[ofs] = ((a&0xFF) << 24) | ((b&0xFF) << 16) | ((g&0xFF) << 8) | (r&0xFF);
                    //((uint32_t*)text_lod_logical[0])[ofs] = 0xff00ff00;
                    ofs += 1;
                }
            }
        }
    }
    #endif
    #if 0
    int texfd = open("/data/mine/texture.raw", O_RDONLY); 
    read(texfd, text_lod_logical[0], 512*512*4);
    close(texfd);
    #endif
    
    struct _gcoCMDBUF commandBuffer = {
        .object = {
            .type = gcvOBJ_COMMANDBUFFER
        },
        .physical = (void*)buf0_physical,
        .logical = (void*)buf0_logical,
        .bytes = 0x20000,
        .startOffset = 0x0,
    };

    commandBuffer.free = commandBuffer.bytes - 0x8; /* Always keep 0x8 at end of buffer for kernel driver */
    /* Set addresses in first command buffer */
    cmdbuf1[31] = cmdbuf1[81] = cmdbuf1[103] = color_status_physical; //H
    cmdbuf1[32] = cmdbuf1[104] = color_surface_physical; //G
    cmdbuf1[41] = cmdbuf1[135] = cmdbuf1[155] = depth_status_physical; //J
    cmdbuf1[42] = cmdbuf1[156] = depth_surface_physical; //I
    cmdbuf1[191] = text_lod_physical[0]; //L - base bitmap
    cmdbuf1[193] = text_lod_physical[1]; //M

    /* Submit first command buffer */
    commandBuffer.startOffset = 0;
    memcpy((void*)((size_t)commandBuffer.logical + commandBuffer.startOffset), cmdbuf1, sizeof(cmdbuf1));
    commandBuffer.offset = commandBuffer.startOffset + sizeof(cmdbuf1);
    commandBuffer.free -= sizeof(cmdbuf1) + 0x08;
    printf("[1] startOffset=%08x, offset=%08x, free=%08x\n", (uint32_t)commandBuffer.startOffset, (uint32_t)commandBuffer.offset, (uint32_t)commandBuffer.free);
    if(viv_commit(conn, &commandBuffer, context) != 0)
    {
        fprintf(stderr, "Error committing first command buffer\n");
        exit(1);
    }

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

    //generate LOD
    unsigned stride[] = {0x1000, 0x800, 0x400, 0x200, 0x100, 0x100, 0x100, 0x100, 0x100};
    unsigned height[] = { 256, 128,  64,  32,  16,   8,   8,   8};
    unsigned width[] =  { 256, 128,  64,  32,  32,  32,  32,  32};
    /* 33 - stride src
     35 - stride dst  *
     39 - pad but allways different - garbage?
     45 - source addr
     47 - dest addr
     49 - height width*/
    for (int idx=0; idx<8; idx++)
    {
        /* Submit command buffer 2 */
        cmdbuf2[33] = stride[idx];
        cmdbuf2[35] = stride[idx + 1];
        cmdbuf2[45] = text_lod_physical[idx + 1]; //idx 0 - 1 done in first cmdbuf
        cmdbuf2[47] = text_lod_physical[idx + 2];
        cmdbuf2[49] = (height[idx] << 16) | width[idx];

        commandBuffer.startOffset = commandBuffer.offset + 0x08; /* Make space for LINK */
        memcpy((void*)((size_t)commandBuffer.logical + commandBuffer.startOffset), cmdbuf2, sizeof(cmdbuf2));
        commandBuffer.offset = commandBuffer.startOffset + sizeof(cmdbuf2);
        commandBuffer.free -= sizeof(cmdbuf2) + 0x08;
        printf("[2,%d] startOffset=%08x, offset=%08x, free=%08x\n", idx, (uint32_t)commandBuffer.startOffset, (uint32_t)commandBuffer.offset, (uint32_t)commandBuffer.free);
        if(viv_commit(conn, &commandBuffer, context) != 0)
        {
            fprintf(stderr, "Error committing second command buffer\n");
            exit(1);
        }
    }

    /* Submit command buffer 3 */
    commandBuffer.startOffset = commandBuffer.offset + 0x08;
    memcpy((void*)((size_t)commandBuffer.logical + commandBuffer.startOffset), cmdbuf3, sizeof(cmdbuf3));
    commandBuffer.offset = commandBuffer.startOffset + sizeof(cmdbuf3);
    commandBuffer.free -= sizeof(cmdbuf3) + 0x08;
    printf("[3] startOffset=%08x, offset=%08x, free=%08x\n", (uint32_t)commandBuffer.startOffset, (uint32_t)commandBuffer.offset, (uint32_t)commandBuffer.free);
    if(viv_commit(conn, &commandBuffer, context) != 0)
    {
        fprintf(stderr, "Error committing third command buffer\n");
        exit(1);
    }

    /* Submit command buffer 4 */
    cmdbuf4[67] = vtx_physical; //A
    cmdbuf4[68] = vtx_physical+0x239d0; //A
    cmdbuf4[69] = vtx_physical+(2*0x239d0); //A
    cmdbuf4[87] = text_lod_physical[0];
    cmdbuf4[89] = text_lod_physical[1];
    cmdbuf4[91] = text_lod_physical[2];
    cmdbuf4[93] = text_lod_physical[3];
    cmdbuf4[95] = text_lod_physical[4];
    cmdbuf4[97] = text_lod_physical[5];
    cmdbuf4[99] = text_lod_physical[6];
    cmdbuf4[101] = text_lod_physical[7];
    cmdbuf4[103] = text_lod_physical[8];
    cmdbuf4[105] = text_lod_physical[9];
    cmdbuf4[153] = cmdbuf4[155] = color_surface_physical;//G
    cmdbuf4[165] = cmdbuf4[167] = depth_surface_physical;//I    

    commandBuffer.startOffset = commandBuffer.offset + 0x08;
    memcpy((void*)((size_t)commandBuffer.logical + commandBuffer.startOffset), cmdbuf4, sizeof(cmdbuf4));
    commandBuffer.offset = commandBuffer.startOffset + sizeof(cmdbuf4);
    commandBuffer.free -= sizeof(cmdbuf4) + 0x08;
    printf("[4] startOffset=%08x, offset=%08x, free=%08x\n", (uint32_t)commandBuffer.startOffset, (uint32_t)commandBuffer.offset, (uint32_t)commandBuffer.free);
    if(viv_commit(conn, &commandBuffer, context) != 0)
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

    /* Submit command buffer 5 */
    cmdbuf5[35] = cmdbuf5[37] = color_surface_physical;
    
    commandBuffer.startOffset = commandBuffer.offset + 0x08;
    memcpy((void*)((size_t)commandBuffer.logical + commandBuffer.startOffset), cmdbuf5, sizeof(cmdbuf5));
    commandBuffer.offset = commandBuffer.startOffset + sizeof(cmdbuf5);
    commandBuffer.free -= sizeof(cmdbuf5) + 0x08;
    printf("[5] startOffset=%08x, offset=%08x, free=%08x\n", (uint32_t)commandBuffer.startOffset, (uint32_t)commandBuffer.offset, (uint32_t)commandBuffer.free);
    if(viv_commit(conn, &commandBuffer, context) != 0)
    {
        fprintf(stderr, "Error committing command buffer 5\n");
        exit(1);
    }
    
    
    
    cmdbuf6[35] = color_surface_physical;
    cmdbuf6[37] = rs_dest_physical;
    
    commandBuffer.startOffset = commandBuffer.offset + 0x08;
    memcpy((void*)((size_t)commandBuffer.logical + commandBuffer.startOffset), cmdbuf6, sizeof(cmdbuf6));
    commandBuffer.offset = commandBuffer.startOffset + sizeof(cmdbuf6);
    commandBuffer.free -= sizeof(cmdbuf6) + 0x08;
    printf("[6] startOffset=%08x, offset=%08x, free=%08x\n", (uint32_t)commandBuffer.startOffset, (uint32_t)commandBuffer.offset, (uint32_t)commandBuffer.free);
    if(viv_commit(conn, &commandBuffer, context) != 0)
    {
        fprintf(stderr, "Error committing command buffer 5\n");
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
    viv_addr_t bmp_physical = 0; // ADDR_J 
    void *bmp_logical = 0;
    if(viv_lock_vidmem(conn, bmp_node, &bmp_physical, &bmp_logical)!=0)
    {
        fprintf(stderr, "Error locking bmp memory\n");
        exit(1);
    }
    memset(bmp_logical, 0xff, 0x177000); // clear previous result
    printf("Locked bmp: phys=%08x log=%08x\n", (uint32_t)bmp_physical, (uint32_t)bmp_logical);
     
     
    /* Submit command buffer 7 */
    cmdbuf7[0x19] = rs_dest_physical; //color_surface_physical or rs_dest_physical
    cmdbuf7[0x1b] = bmp_physical;

    commandBuffer.startOffset = commandBuffer.offset + 0x08;
    memcpy((void*)((size_t)commandBuffer.logical + commandBuffer.startOffset), cmdbuf7, sizeof(cmdbuf7));
    commandBuffer.offset = commandBuffer.startOffset + sizeof(cmdbuf7);
    commandBuffer.free -= sizeof(cmdbuf7) + 0x08;
    printf("[7] startOffset=%08x, offset=%08x, free=%08x\n", (uint32_t)commandBuffer.startOffset, (uint32_t)commandBuffer.offset, (uint32_t)commandBuffer.free);
    if(viv_commit(conn, &commandBuffer, context) != 0)
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

    bmp_dump32(bmp_logical, 800, 480, false, "/home/linaro/replay.bmp");
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
    //printf("Contextbuffer used %i\n", *contextBuffer.inUse);
    viv_close(conn);
    return 0;
}

