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

#include "empty_screen_cmd.h"
/* TODO: should actually update context as we go,
   a context switch would currently revert state and likely result in corrupted rendering.
 */
#include "context_cmd.h"


//#define COMPONENTS_PER_VERTEX (3)
//#define NUM_VERTICES (6*4)

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
    if(viv_alloc_contiguous(conn, 0x20000, &buf0_physical, &buf0_logical, NULL)!=0)
    {
        fprintf(stderr, "Error allocating host memory\n");
        exit(1);
    }
    printf("Allocated buffer: phys=%08x log=%08x\n", (uint32_t)buf0_physical, (uint32_t)buf0_logical);

    /* allocate main render target */
    gcuVIDMEM_NODE_PTR color_surface_node = 0;
    if(viv_alloc_linear_vidmem(conn, 0x73000, 0x40, gcvSURF_RENDER_TARGET, gcvPOOL_SYSTEM /*why?*/, &color_surface_node, NULL)!=0)
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
    if(viv_alloc_linear_vidmem(conn, 0x800, 0x40, gcvSURF_TILE_STATUS, gcvPOOL_DEFAULT, &color_status_node, NULL)!=0)
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
    if(viv_alloc_linear_vidmem(conn, 0x45000, 0x40, gcvSURF_DEPTH, gcvPOOL_DEFAULT, &depth_surface_node, NULL)!=0)
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
    if(viv_alloc_linear_vidmem(conn, 0x500, 0x40, gcvSURF_TILE_STATUS, gcvPOOL_DEFAULT, &depth_status_node, NULL)!=0)
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

    /* allocate tile status for aux render target */
    gcuVIDMEM_NODE_PTR rs_dest_node = 0;
    if(viv_alloc_linear_vidmem(conn, 0x70000, 0x40, gcvSURF_TILE_STATUS, gcvPOOL_DEFAULT, &rs_dest_node, NULL)!=0)
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


    struct _gcoCMDBUF commandBuffer = {
        .object = {
            .type = gcvOBJ_COMMANDBUFFER
        },
        //.os = (_gcoOS*)0xbf7488,
        //.hardware = (_gcoHARDWARE*)0x402694e0,
        .physical = (void*)buf0_physical,
        .logical = (void*)buf0_logical,
        .bytes = 0x20000,
        .startOffset = 0x0,
        //.offset = 0xac0,
        //.free = 0x7520,
        //.hintTable = (unsigned int*)0x0, // Used when gcdSECURE
        //.hintIndex = (unsigned int*)0x58,  // Used when gcdSECURE
        //.hintCommit = (unsigned int*)0xffffffff // Used when gcdSECURE
    };
    
    //(struct viv_conn *conn, gckCONTEXT *vctx)
    gcsHAL_INTERFACE id = {};
    id.command = gcvHAL_ATTACH;
    if((viv_invoke(conn, &id)) != gcvSTATUS_OK)
    {
        #ifdef DEBUG
        fprintf(stderr, "Error attaching to GPU\n");
        #endif
        exit(1);
    }
    else
    {
        fprintf(stderr, "gcvHAL_ATTACHed to GPU\n");
    }
    gckCONTEXT context = id.u.Attach.context;
    
    /* Set addresses in first command buffer */
    commandBuffer.free = commandBuffer.bytes - 0x8; /* Always keep 0x8 at end of buffer for kernel driver */
    cmdbuf1[37] = cmdbuf1[87] = cmdbuf1[109] = color_status_physical; //ADDR_H */       0x800 gcvSURF_TILE_STATUS
    cmdbuf1[38] = cmdbuf1[110] = cmdbuf1[141] = cmdbuf1[143] = color_surface_physical; //ADDR_G */      0x73000 gcvSURF_RENDER_TARGET
    cmdbuf1[47] = depth_status_physical; //ADDR_J */   0x500 gcvSURF_TILE_STATUS
    cmdbuf1[48] = depth_surface_physical; //DDR_I */       0x45000 gcvSURF_DEPTH
    cmdbuf1[111] = 0xffff0000; // color ABGR - second command buffer swaps red and blue

    /* Submit first command buffer */
    commandBuffer.startOffset = 0;
    memcpy((void*)((size_t)commandBuffer.logical + commandBuffer.startOffset), cmdbuf1, sizeof(cmdbuf1));
    commandBuffer.offset = commandBuffer.startOffset + sizeof(cmdbuf1);
    commandBuffer.free -= sizeof(cmdbuf1) + 0x18;
    printf("[1] startOffset=%08x, offset=%08x, free=%08x\n", (uint32_t)commandBuffer.startOffset, (uint32_t)commandBuffer.offset, (uint32_t)commandBuffer.free);
    
    if(viv_commit(conn, &commandBuffer, context) != 0)
    {
        fprintf(stderr, "Error committing first command buffer\n");
        exit(1);
    }
    else
    {
        fprintf(stderr, "Committed first command buffer\n");
    }
    
    
    /*Second command buffer - SWAP_RB=1 - swaps red and blue*/
    cmdbuf2[35] = color_surface_physical;
    cmdbuf2[37] = rs_dest_physical;
    commandBuffer.startOffset = commandBuffer.offset + 0x18;
    memcpy((void*)((size_t)commandBuffer.logical + commandBuffer.startOffset), cmdbuf2, sizeof(cmdbuf2));
    commandBuffer.offset = commandBuffer.startOffset + sizeof(cmdbuf2);
    commandBuffer.free -= sizeof(cmdbuf2) + 0x18;
    if(viv_commit(conn, &commandBuffer, context) != 0)
    {
        fprintf(stderr, "Error committing second command buffer\n");
        exit(1);
    }
    else
    {
        fprintf(stderr, "Committed second command buffer\n");
    }

    /* Submit event queue with SIGNAL, fromWhere=gcvKERNEL_PIXEL (wait for pixel engine to finish) */
    int sig_id = 0;
    if(viv_user_signal_create(conn, 0, &sig_id) != 0) /* automatic resetting signal */
    {
        fprintf(stderr, "Cannot create user signal\n");
        exit(1);
    }
    printf("Created user signal %i\n", sig_id);
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

    /* Allocate video memory for BITMAP, lock */
    gcuVIDMEM_NODE_PTR bmp_node = 0;
    if(viv_alloc_linear_vidmem(conn, 0x70000, 0x40, gcvSURF_BITMAP, gcvPOOL_DEFAULT, &bmp_node, NULL)!=0)
    {
        fprintf(stderr, "Error allocating bitmap status memory\n");
        exit(1);
    }
    printf("Allocated bitmap node: node=%08x\n", (uint32_t)bmp_node);
    
    viv_addr_t bmp_physical = 0;
    void *bmp_logical = 0;
    if(viv_lock_vidmem(conn, bmp_node, &bmp_physical, &bmp_logical)!=0)
    {
        fprintf(stderr, "Error locking bmp memory\n");
        exit(1);
    }
    memset(bmp_logical, 0xff, 0x5dc00); /* clear previous result */
    printf("Locked bmp: phys=%08x log=%08x\n", (uint32_t)bmp_physical, (uint32_t)bmp_logical);

    
    /* Submit third command buffer, updating context.
     * Fourth command buffer copies render result to bitmap, detiling along the way. 
     */
    //color_surface_physical - first commandBuffer result, rs_dest_physical - second commandBuffer;
    cmdbuf3[0x19] = rs_dest_physical;
    cmdbuf3[0x1b] = bmp_physical;
    commandBuffer.startOffset = commandBuffer.offset + 0x18;
    memcpy((void*)((size_t)commandBuffer.logical + commandBuffer.startOffset), cmdbuf3, sizeof(cmdbuf3));
    commandBuffer.offset = commandBuffer.startOffset + sizeof(cmdbuf3);
    commandBuffer.free -= sizeof(cmdbuf3) + 0x18;
    printf("[4] startOffset=%08x, offset=%08x, free=%08x\n", (uint32_t)commandBuffer.startOffset, (uint32_t)commandBuffer.offset, (uint32_t)commandBuffer.free);
    if(viv_commit(conn, &commandBuffer, context) != 0)
    {
        fprintf(stderr, "Error committing fourth command buffer\n");
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
    bmp_dump32(bmp_logical, 400, 240, false, "/home/linaro/replay.bmp");
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
//    printf("Contextbuffer used %i\n", *contextBuffer.inUse);

    viv_close(conn);
    return 0;
}

