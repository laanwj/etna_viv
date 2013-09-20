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
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <unistd.h>
#include <string.h>
#include <inttypes.h>

#include "write_bmp.h"
#include "viv.h"
#include "etna_fb.h"

#include "../replay/cube_cmd.h"
#include "../replay/context_cmd.h"

float vVertices[] = {
  // front
  -1.0f, -1.0f, +1.0f, // point blue
  //-0.5f, -0.6f, +0.5f, // point blue
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

int main(int argc, char **argv)
{
    int rv;
    struct fb_info fb;
    rv = fb_open(0, &fb);
    if(rv!=0)
    {
        exit(1);
    }
    fb_set_buffer(&fb, 0);
    struct viv_conn *conn = 0;
    rv = viv_open(VIV_HW_3D, &conn);
    if(rv!=0)
    {
        fprintf(stderr, "Error opening device\n");
        exit(1);
    }
    printf("Succesfully opened device\n");

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
    if(viv_alloc_linear_vidmem(conn, 0x70000, 0x40, gcvSURF_RENDER_TARGET, gcvPOOL_DEFAULT, &rt_node, NULL)!=0)
    {
        fprintf(stderr, "Error allocating render target buffer memory\n");
        exit(1);
    }
    printf("Allocated render target node: node=%08x\n", (uint32_t)rt_node);
    
    viv_addr_t rt_physical = 0;
    void *rt_logical = 0;
    if(viv_lock_vidmem(conn, rt_node, &rt_physical, &rt_logical)!=0)
    {
        fprintf(stderr, "Error locking render target memory\n");
        exit(1);
    }
    printf("Locked render target: phys=%08x log=%08x\n", (uint32_t)rt_physical, (uint32_t)rt_logical);
    memset(rt_logical, 0xff, 0x70000); /* clear previous result just in case, test that clearing works */

    /* allocate tile status for main render target */
    gcuVIDMEM_NODE_PTR rt_ts_node = 0;
    if(viv_alloc_linear_vidmem(conn, 0x700, 0x40, gcvSURF_TILE_STATUS, gcvPOOL_DEFAULT, &rt_ts_node, NULL)!=0)
    {
        fprintf(stderr, "Error allocating render target tile status memory\n");
        exit(1);
    }
    printf("Allocated render target tile status node: node=%08x\n", (uint32_t)rt_ts_node);
    
    viv_addr_t rt_ts_physical = 0;
    void *rt_ts_logical = 0;
    if(viv_lock_vidmem(conn, rt_ts_node, &rt_ts_physical, &rt_ts_logical)!=0)
    {
        fprintf(stderr, "Error locking render target memory\n");
        exit(1);
    }
    printf("Locked render target ts: phys=%08x log=%08x\n", (uint32_t)rt_ts_physical, (uint32_t)rt_ts_logical);

    /* allocate depth for main render target */
    gcuVIDMEM_NODE_PTR z_node = 0;
    if(viv_alloc_linear_vidmem(conn, 0x38000, 0x40, gcvSURF_DEPTH, gcvPOOL_DEFAULT, &z_node, NULL)!=0)
    {
        fprintf(stderr, "Error allocating depth memory\n");
        exit(1);
    }
    printf("Allocated depth node: node=%08x\n", (uint32_t)z_node);
    
    viv_addr_t z_physical = 0;
    void *z_logical = 0;
    if(viv_lock_vidmem(conn, z_node, &z_physical, &z_logical)!=0)
    {
        fprintf(stderr, "Error locking depth target memory\n");
        exit(1);
    }
    printf("Locked depth target: phys=%08x log=%08x\n", (uint32_t)z_physical, (uint32_t)z_logical);

    /* allocate depth ts for main render target */
    gcuVIDMEM_NODE_PTR z_ts_node = 0;
    if(viv_alloc_linear_vidmem(conn, 0x400, 0x40, gcvSURF_TILE_STATUS, gcvPOOL_DEFAULT, &z_ts_node, NULL)!=0)
    {
        fprintf(stderr, "Error allocating depth memory\n");
        exit(1);
    }
    printf("Allocated depth ts node: node=%08x\n", (uint32_t)z_ts_node);
    
    viv_addr_t z_ts_physical = 0;
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
    
    viv_addr_t vtx_physical = 0;
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
    
    viv_addr_t aux_rt_physical = 0;
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
    
    viv_addr_t aux_rt_ts_physical = 0;
    void *aux_rt_ts_logical = 0;
    if(viv_lock_vidmem(conn, aux_rt_ts_node, &aux_rt_ts_physical, &aux_rt_ts_logical)!=0)
    {
        fprintf(stderr, "Error locking aux ts render target memory\n");
        exit(1);
    }
    printf("Locked aux render target ts: phys=%08x log=%08x\n", (uint32_t)aux_rt_ts_physical, (uint32_t)aux_rt_ts_logical);

    /* Phew, now we got all the memory we need.
     * Write interleaved attribute vertex stream.
     * Unlike the GL example we only do this once, not every time glDrawArrays is called, the same would be accomplished
     * from GL by using a vertex buffer object.
     */
    for(int vert=0; vert<NUM_VERTICES; ++vert)
    {
        int src_idx = vert * COMPONENTS_PER_VERTEX;
        int dest_idx = vert * COMPONENTS_PER_VERTEX * 3;
        for(int comp=0; comp<COMPONENTS_PER_VERTEX; ++comp)
        {
            ((float*)vtx_logical)[dest_idx+comp+0] = vVertices[src_idx + comp]; /* 0 */
            ((float*)vtx_logical)[dest_idx+comp+3] = vNormals[src_idx + comp]; /* 1 */
            ((float*)vtx_logical)[dest_idx+comp+6] = vColors[src_idx + comp]; /* 2 */
        }
    }
    /*
    for(int idx=0; idx<NUM_VERTICES*3*3; ++idx)
    {
        printf("%i %f\n", idx, ((float*)vtx_logical)[idx]);
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
        .bytes = 0x8000,
        .startOffset = 0x0,
        //.offset = 0xac0,
        //.free = 0x7520,
        //.hintTable = (unsigned int*)0x0, // Used when gcdSECURE
        //.hintIndex = (unsigned int*)0x58,  // Used when gcdSECURE
        //.hintCommit = (unsigned int*)0xffffffff // Used when gcdSECURE
    };
    struct _gcoCONTEXT contextBuffer = {
        .object = {
            .type = gcvOBJ_CONTEXT
        },
        //.os = (_gcoOS*)0xbf7488,
        //.hardware = (_gcoHARDWARE*)0x402694e0,
        .id = 0x0, // Actual ID will be returned here
        .map = contextMap,
        .stateCount = stateCount,
        //.hint = (unsigned char*)0x0, // Used when gcdSECURE
        //.hintValue = 2, // Used when gcdSECURE
        //.hintCount = 0xca, // Used when gcdSECURE
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
        //.hintArray = (unsigned int*)0x0, // Used when gcdSECURE
        //.hintIndex = (unsigned int*)0x0  // Used when gcdSECURE
    };
    commandBuffer.free = commandBuffer.bytes - 0x8; /* Always keep 0x8 at end of buffer for kernel driver */
    /* Set addresses in first command buffer */
    cmdbuf1[0x57] = cmdbuf1[0x67] = cmdbuf1[0x9f] = cmdbuf1[0xbb] = cmdbuf1[0xd9] = cmdbuf1[0xfb] = rt_physical;
    cmdbuf1[0x65] = cmdbuf1[0x9d] = cmdbuf1[0xb9] = cmdbuf1[0xd7] = cmdbuf1[0xe5] = cmdbuf1[0xf9] = rt_ts_physical;
    cmdbuf1[0x6d] = cmdbuf1[0x7f] = z_physical;
    cmdbuf1[0x7d] = z_ts_physical;
    cmdbuf1[0x87] = cmdbuf1[0xa3] = cmdbuf1[0xc1] = aux_rt_ts_physical;
    cmdbuf1[0x89] = cmdbuf1[0x8f] = cmdbuf1[0x93] 
        = cmdbuf1[0xa5] = cmdbuf1[0xab] = cmdbuf1[0xaf] 
        = cmdbuf1[0xc3] = cmdbuf1[0xc9] = cmdbuf1[0xcd] = aux_rt_physical;
    cmdbuf1[0x1f3] = cmdbuf1[0x215] = cmdbuf1[0x237] 
        = cmdbuf1[0x259] = cmdbuf1[0x27b] = cmdbuf1[0x29d] = vtx_physical;

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
    *contextBuffer.inUse = 0;

    /* Submit second command buffer, with updated context.
     * Second command buffer fills the background.
     */
    cmdbuf2[0x1d] = cmdbuf2[0x1f] = rt_physical;
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

    /* Submit third command buffer, with updated context
     * Third command buffer does some cache flush trick?
     * It can be left out without any visible harm.
     **/
    cmdbuf3[0x9] = aux_rt_ts_physical;
    cmdbuf3[0xb] = cmdbuf3[0x11] = cmdbuf3[0x15] = aux_rt_physical;
    cmdbuf3[0x1f] = rt_ts_physical;
    cmdbuf3[0x21] = rt_physical;
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
    cmdbuf4[0x0f] = fb.fb_fix.line_length;
    cmdbuf4[0x19] = rt_physical;
    cmdbuf4[0x1b] = fb.physical[0];
    fb_set_buffer(&fb, 0);
    /* XXX gcvHAL_MAP_USER_MEMORY to get dma-able address, or does this work as-is? */
    commandBuffer.startOffset = commandBuffer.offset + 0x18;
    memcpy((void*)((size_t)commandBuffer.logical + commandBuffer.startOffset), cmdbuf4, sizeof(cmdbuf4));
    commandBuffer.offset = commandBuffer.startOffset + sizeof(cmdbuf4);
    commandBuffer.free -= sizeof(cmdbuf4) + 0x18;
    printf("[4] startOffset=%08x, offset=%08x, free=%08x\n", (uint32_t)commandBuffer.startOffset, (uint32_t)commandBuffer.offset, (uint32_t)commandBuffer.free);
    if(viv_commit(conn, &commandBuffer, &contextBuffer) != 0)
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
    bmp_dump32(bmp_logical, 400, 240, false, "/mnt/sdcard/replay.bmp");
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

