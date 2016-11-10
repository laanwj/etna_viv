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

#include "cube_cmd_gc880.h"
/* TODO: should actually update context as we go,
 *   a context switch would currently revert state and likely result in corrupted rendering.
 */
#include "context_cmd.h"

float vVertices[] = {
    // front
    -1.0f, -1.0f, +1.0f, // point blue
    +1.0f, -1.0f, +1.0f, // point magenta
    -1.0f, +1.0f, +1.0f, // point cyan
    +1.0f, +1.0f, +1.0f, // point white
    // back
    +1.0f, -1.0f, -1.0f, // point red
    -1.0f, -1.0f, -1.0f, // point black
    +1.0f, +1.0f, -1.0f, // point yellow
    -1.0f, +1.0f, -1.0f, // point green
    // right
    +1.0f, -1.0f, +1.0f, // point magenta
    +1.0f, -1.0f, -1.0f, // point red
    +1.0f, +1.0f, +1.0f, // point white
    +1.0f, +1.0f, -1.0f, // point yellow
    // left
    -1.0f, -1.0f, -1.0f, // point black
    -1.0f, -1.0f, +1.0f, // point blue
    -1.0f, +1.0f, -1.0f, // point green
    -1.0f, +1.0f, +1.0f, // point cyan
    // top
    -1.0f, +1.0f, +1.0f, // point cyan
    +1.0f, +1.0f, +1.0f, // point white
    -1.0f, +1.0f, -1.0f, // point green
    +1.0f, +1.0f, -1.0f, // point yellow
    // bottom
    -1.0f, -1.0f, -1.0f, // point black
    +1.0f, -1.0f, -1.0f, // point red
    -1.0f, -1.0f, +1.0f, // point blue
    +1.0f, -1.0f, +1.0f  // point magenta
};

float vColors[] = {
    // front
    0.0f,  0.0f,  1.0f, // blue
    1.0f,  0.0f,  1.0f, // magenta
    0.0f,  1.0f,  1.0f, // cyan
    1.0f,  1.0f,  1.0f, // white
    // back
    1.0f,  0.0f,  0.0f, // red
    0.0f,  0.0f,  0.0f, // black
    1.0f,  1.0f,  0.0f, // yellow
    0.0f,  1.0f,  0.0f, // green
    // right
    1.0f,  0.0f,  1.0f, // magenta
    1.0f,  0.0f,  0.0f, // red
    1.0f,  1.0f,  1.0f, // white
    1.0f,  1.0f,  0.0f, // yellow
    // left
    0.0f,  0.0f,  0.0f, // black
    0.0f,  0.0f,  1.0f, // blue
    0.0f,  1.0f,  0.0f, // green
    0.0f,  1.0f,  1.0f, // cyan
    // top
    0.0f,  1.0f,  1.0f, // cyan
    1.0f,  1.0f,  1.0f, // white
    0.0f,  1.0f,  0.0f, // green
    1.0f,  1.0f,  0.0f, // yellow
    // bottom
    0.0f,  0.0f,  0.0f, // black
    1.0f,  0.0f,  0.0f, // red
    0.0f,  0.0f,  1.0f, // blue
    1.0f,  0.0f,  1.0f  // magenta
};

float vNormals[] = {
    // front
    +0.0f, +0.0f, +1.0f, // forward
    +0.0f, +0.0f, +1.0f, // forward
    +0.0f, +0.0f, +1.0f, // forward
    +0.0f, +0.0f, +1.0f, // forward
    // back
    +0.0f, +0.0f, -1.0f, // backbard
    +0.0f, +0.0f, -1.0f, // backbard
    +0.0f, +0.0f, -1.0f, // backbard
    +0.0f, +0.0f, -1.0f, // backbard
    // right
    +1.0f, +0.0f, +0.0f, // right
    +1.0f, +0.0f, +0.0f, // right
    +1.0f, +0.0f, +0.0f, // right
    +1.0f, +0.0f, +0.0f, // right
    // left
    -1.0f, +0.0f, +0.0f, // left
    -1.0f, +0.0f, +0.0f, // left
    -1.0f, +0.0f, +0.0f, // left
    -1.0f, +0.0f, +0.0f, // left
    // top
    +0.0f, +1.0f, +0.0f, // up
    +0.0f, +1.0f, +0.0f, // up
    +0.0f, +1.0f, +0.0f, // up
    +0.0f, +1.0f, +0.0f, // up
    // bottom
    +0.0f, -1.0f, +0.0f, // down
    +0.0f, -1.0f, +0.0f, // down
    +0.0f, -1.0f, +0.0f, // down
    +0.0f, -1.0f, +0.0f  // down
};
#define COMPONENTS_PER_VERTEX (3)
#define NUM_VERTICES (6*4)
#define VERTICES_PER_DRAW 4
#define DRAW_COUNT 6

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
    if(viv_alloc_linear_vidmem(conn, 0x70000, 0x40, gcvSURF_BITMAP, gcvPOOL_DEFAULT, &rs_dest_node, NULL)!=0)
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
    
    /* Phew, now we got all the memory we need.
     * Write interleaved attribute vertex stream.
     * Unlike the GL example we only do this once, not every time glDrawArrays is called, the same would be accomplished
     * from GL by using a vertex buffer object.
     */
    int dest_idx = 0;
    int v_src_idx = 0;
    int n_src_idx = 0;
    int c_src_idx = 0;
    for(int jj=0; jj<DRAW_COUNT; jj++)
    {
        for(int vert=0; vert<VERTICES_PER_DRAW*3; ++vert)
        {
            ((float*)vtx_logical)[dest_idx] = vVertices[v_src_idx];
            dest_idx++;
            v_src_idx++;
        }
        for(int vert=0; vert<VERTICES_PER_DRAW*3; ++vert)
        {
            ((float*)vtx_logical)[dest_idx] = vNormals[n_src_idx];
            dest_idx++;
            n_src_idx++;
        }
        for(int vert=0; vert<VERTICES_PER_DRAW*3; ++vert)
        {
            ((float*)vtx_logical)[dest_idx] = vColors[c_src_idx];
            dest_idx++;
            c_src_idx++;
        }
    }
    
    /*
     *    for(int idx=0; idx<NUM_VERTICES*3*3; ++idx)
     *    {
     *        printf("%i %f\n", idx, ((float*)vtx_logical)[idx]);
}*/
    
    /* Load the command buffer and send the commit command. */
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
    
    commandBuffer.free = commandBuffer.bytes - 0x8; /* Always keep 0x8 at end of buffer for kernel driver */
    /* Set addresses in first command buffer */
    cmdbuf1[37] = cmdbuf1[87] = cmdbuf1[109] = color_status_physical;
    cmdbuf1[38] = cmdbuf1[110] = cmdbuf1[213] = cmdbuf1[215] = color_surface_physical;
    cmdbuf1[47] = depth_status_physical; //ADDR_J */   0x500 gcvSURF_TILE_STATUS
    cmdbuf1[48] = cmdbuf1[225] = cmdbuf1[227] = depth_surface_physical; //DDR_I */       0x45000 gcvSURF_DEPTH
    cmdbuf1[169] = vtx_physical;
    cmdbuf1[170] = vtx_physical + 0x030;
    cmdbuf1[171] = vtx_physical + 0x060;
    
    cmdbuf1[413] = vtx_physical + 0x060;
    cmdbuf1[414] = vtx_physical + 0x090;
    cmdbuf1[415] = vtx_physical + 0x0c0;
    
    cmdbuf1[435] = vtx_physical + 0x0c0;
    cmdbuf1[436] = vtx_physical + 0x0f0;
    cmdbuf1[437] = vtx_physical + 0x120;
    
    cmdbuf1[457] = vtx_physical + 0x120;
    cmdbuf1[458] = vtx_physical + 0x150;
    cmdbuf1[459] = vtx_physical + 0x180;
    
    cmdbuf1[479] = vtx_physical + 0x180;
    cmdbuf1[480] = vtx_physical + 0x1b0;
    cmdbuf1[481] = vtx_physical + 0x1e0;
    
    cmdbuf1[501] = vtx_physical + 0x1e0;
    cmdbuf1[502] = vtx_physical + 0x210;
    cmdbuf1[503] = vtx_physical + 0x240;
    
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
    
    /*
     * What does it do? Can be skipped.
     */
    cmdbuf2[35] = color_surface_physical;
    cmdbuf2[37] = color_surface_physical;
    commandBuffer.startOffset = commandBuffer.offset + 0x08; /* Make space for LINK */
    memcpy((void*)((size_t)commandBuffer.logical + commandBuffer.startOffset), cmdbuf2, sizeof(cmdbuf2));
    commandBuffer.offset = commandBuffer.startOffset + sizeof(cmdbuf2);
    commandBuffer.free -= sizeof(cmdbuf2) + 0x08;
    
    printf("[2] startOffset=%08x, offset=%08x, free=%08x\n", (uint32_t)commandBuffer.startOffset, (uint32_t)commandBuffer.offset, (uint32_t)commandBuffer.free);
    if(viv_commit(conn, &commandBuffer, context) != 0)
    {
        fprintf(stderr, "Error committing second command buffer\n");
        exit(1);
    }
    
    /* Submit third command buffer - SWAP_RB=1 - swaps red and blue
     **/
    cmdbuf3[35] = color_surface_physical;
    cmdbuf3[37] = rs_dest_physical;
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
    if(viv_alloc_linear_vidmem(conn, 0x5dc00, 0x40, gcvSURF_BITMAP, gcvPOOL_DEFAULT, &bmp_node, NULL)!=0)
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
    
    /* Submit fourth command buffer, updating context.
     * Fourth command buffer copies render result to bitmap, detiling along the way. 
     */
    /* color_surface_physical = cmdbuf2 or cmdbuf1 result, rs_dest_physical - cmdbuf3 result
     * FIXME rs_dest_physical result is bad... why?
     * turning off source tilling in cmdbuf4 helps but don't solve problem. */
    cmdbuf4[0x19] = rs_dest_physical; //color_surface_physical rs_dest_physical
    cmdbuf4[0x1b] = bmp_physical;
    commandBuffer.startOffset = commandBuffer.offset + 0x08;
    memcpy((void*)((size_t)commandBuffer.logical + commandBuffer.startOffset), cmdbuf4, sizeof(cmdbuf4));
    commandBuffer.offset = commandBuffer.startOffset + sizeof(cmdbuf4);
    commandBuffer.free -= sizeof(cmdbuf4) + 0x08;
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
     *    for(int x=0; x<0x700; ++x)
     *    {
     *        uint32_t value = ((uint32_t*)rt_ts_logical)[x];
     *        printf("Sample ts: %x %08x\n", x*4, value);
}*/
    //printf("Contextbuffer used %i\n", *contextBuffer.inUse);
    
    viv_close(conn);
    return 0;
}
