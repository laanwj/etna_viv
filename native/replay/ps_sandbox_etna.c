/* build and submit low-level command buffer to test shader instructions
 *
 * see etna_test for a more high-level approach using etna_flush / etna_finish instead of manually
 * submitting the command buffers one by one
 */
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

#include <etnaviv/common.xml.h>
#include <etnaviv/state.xml.h>
#include <etnaviv/state_3d.xml.h>
#include <etnaviv/cmdstream.xml.h>
#include "viv_raw.h"
#include "write_bmp.h"
#include "esTransform.h"

/* Print generated command buffer */
//#define CMD_DEBUG
//#define CMD_COMPARE

/* TODO: should actually update context as we go,
   a context switch would currently revert state and likely result in corrupted rendering.
 */
#include "context_cmd.h"
    
float vVertices[] = {
  -1.0f, -1.0f, +0.0f,
  +1.0f, -1.0f, +0.0f,
  -1.0f, +1.0f, +0.0f,
  +1.0f, +1.0f, +0.0f,
};

float vTexCoords[] = {
  +0.0f, +0.0f, 
  +1.0f, +0.0f, 
  +0.0f, +1.0f, 
  +1.0f, +1.0f, 
};

#define NUM_VERTICES (4)

#ifdef CMD_COMPARE
char is_padding[0x8000 / 4];
#endif

/* macro for MASKED() (multiple can be &ed) */
#define VIV_MASKED(NAME, VALUE) (~(NAME ## _MASK | NAME ## __MASK) | ((VALUE)<<(NAME ## __SHIFT)))
/* for boolean bits */
#define VIV_MASKED_BIT(NAME, VALUE) (~(NAME ## _MASK | NAME) | ((VALUE) ? NAME : 0))
/* for inline enum bit fields 
 * XXX in principle headergen could simply generate these fields prepackaged 
 */
#define VIV_MASKED_INL(NAME, VALUE) (~(NAME ## _MASK | NAME ## __MASK) | (NAME ## _ ## VALUE))

/* XXX store state changes
 * group consecutive states 
 * make LOAD_STATE commands, add to current command buffer
 */
inline void etna_set_state(gcoCMDBUF commandBuffer, uint32_t address, uint32_t value)
{
#ifdef CMD_DEBUG
    printf("%05x := %08x\n", address, value);
#endif
    uint32_t *tgt = (uint32_t*)((size_t)commandBuffer->logical + commandBuffer->offset);
    tgt[0] = VIV_FE_LOAD_STATE_HEADER_OP_LOAD_STATE |
            VIV_FE_LOAD_STATE_HEADER_COUNT(1) |
            VIV_FE_LOAD_STATE_HEADER_OFFSET(address >> 2);
    tgt[1] = value;
    commandBuffer->offset += 8;
}

/* this can be inlined, though would likely be even faster to return a pointer and let the client write to
 * the buffer directly */
inline void etna_set_state_multi(gcoCMDBUF commandBuffer, uint32_t base, uint32_t num, uint32_t *values)
{
    uint32_t *tgt = (uint32_t*)((size_t)commandBuffer->logical + commandBuffer->offset);
    tgt[0] = VIV_FE_LOAD_STATE_HEADER_OP_LOAD_STATE |
            VIV_FE_LOAD_STATE_HEADER_COUNT(num & 0x3ff) |
            VIV_FE_LOAD_STATE_HEADER_OFFSET(base >> 2);
#ifdef CMD_DEBUG
    for(uint32_t idx=0; idx<num; ++idx)
    {
        printf("%05x := %08x\n", base, values[idx]);
        base += 4;
    }
#endif
    memcpy(&tgt[1], values, 4*num);
    commandBuffer->offset += 4 + num*4;
    if(commandBuffer->offset & 4) /* PAD */
    {
#ifdef CMD_COMPARE
        is_padding[commandBuffer->offset / 4] = 1;
#endif
        commandBuffer->offset += 4;
    }
}
inline void etna_set_state_f32(gcoCMDBUF commandBuffer, uint32_t address, float value)
{
    union {
        uint32_t i32;
        float f32;
    } x = { .f32 = value };
    etna_set_state(commandBuffer, address, x.i32);
}
inline void etna_set_state_fixp(gcoCMDBUF commandBuffer, uint32_t address, uint32_t value)
{
#ifdef CMD_DEBUG
    printf("%05x := %08x (fixp)\n", address, value);
#endif
    uint32_t *tgt = (uint32_t*)((size_t)commandBuffer->logical + commandBuffer->offset);
    tgt[0] = VIV_FE_LOAD_STATE_HEADER_OP_LOAD_STATE |
            VIV_FE_LOAD_STATE_HEADER_COUNT(1) |
            VIV_FE_LOAD_STATE_HEADER_FIXP |
            VIV_FE_LOAD_STATE_HEADER_OFFSET(address >> 2);
    tgt[1] = value;
    commandBuffer->offset += 8;
}
inline void etna_draw_primitives(gcoCMDBUF cmdPtr, uint32_t primitive_type, uint32_t start, uint32_t count)
{
#ifdef CMD_DEBUG
    printf("draw_primitives %08x %08x %08x %08x\n", 
            VIV_FE_DRAW_PRIMITIVES_HEADER_OP_DRAW_PRIMITIVES,
            primitive_type, start, count);
#endif
    uint32_t *tgt = (uint32_t*)((size_t)cmdPtr->logical + cmdPtr->offset);
    tgt[0] = VIV_FE_DRAW_PRIMITIVES_HEADER_OP_DRAW_PRIMITIVES;
    tgt[1] = primitive_type;
    tgt[2] = start;
    tgt[3] = count;
    cmdPtr->offset += 16;
}

/* warm up RS on aux render target */
void etna_warm_up_rs(gcoCMDBUF cmdPtr, viv_addr_t aux_rt_physical, viv_addr_t aux_rt_ts_physical)
{
    etna_set_state(cmdPtr, VIVS_TS_COLOR_STATUS_BASE, aux_rt_ts_physical); /* ADDR_G */
    etna_set_state(cmdPtr, VIVS_TS_COLOR_SURFACE_BASE, aux_rt_physical); /* ADDR_F */
    etna_set_state(cmdPtr, VIVS_TS_FLUSH_CACHE, VIVS_TS_FLUSH_CACHE_FLUSH);
    etna_set_state(cmdPtr, VIVS_RS_CONFIG,  /* wut? */
            VIVS_RS_CONFIG_SOURCE_FORMAT(RS_FORMAT_A8R8G8B8) |
            VIVS_RS_CONFIG_SOURCE_TILED |
            VIVS_RS_CONFIG_DEST_FORMAT(RS_FORMAT_R5G6B5) |
            VIVS_RS_CONFIG_DEST_TILED);
    etna_set_state(cmdPtr, VIVS_RS_SOURCE_ADDR, aux_rt_physical); /* ADDR_F */
    etna_set_state(cmdPtr, VIVS_RS_SOURCE_STRIDE, 0x400);
    etna_set_state(cmdPtr, VIVS_RS_DEST_ADDR, aux_rt_physical); /* ADDR_F */
    etna_set_state(cmdPtr, VIVS_RS_DEST_STRIDE, 0x400);
    etna_set_state(cmdPtr, VIVS_RS_WINDOW_SIZE, 
            VIVS_RS_WINDOW_SIZE_HEIGHT(4) |
            VIVS_RS_WINDOW_SIZE_WIDTH(16));
    etna_set_state(cmdPtr, VIVS_RS_CLEAR_CONTROL, VIVS_RS_CLEAR_CONTROL_MODE_DISABLED);
    etna_set_state(cmdPtr, VIVS_RS_KICKER, 0xbeebbeeb);
}

#ifdef CMD_COMPARE
int cmdbuffer_compare(gcoCMDBUF cmdPtr, uint32_t *cmdbuf, uint32_t cmdbuf_size)
{
    /* Count differences between generated and stored command buffer */
    int diff = 0;
    for(int idx=8; idx<cmdbuf_size/4; ++idx)
    {
        uint32_t cmdbuf_word = cmdbuf[idx];
        uint32_t my_word = *(uint32_t*)((size_t)cmdPtr->logical + cmdPtr->startOffset + idx*4);
        printf("/*%03x*/ %08x ref:%08x ", idx, my_word, cmdbuf_word);
        if(is_padding[cmdPtr->startOffset/4 + idx])
        {
            printf("PAD");
        } else if(cmdbuf_word != my_word)
        {
            diff += 1;
            printf("DIFF");
        }
        printf("\n");
    }
    printf("Number of differences: %i\n", diff);
    uint32_t size_cmd = cmdbuf_size/4;
    uint32_t size_my = (cmdPtr->offset - cmdPtr->startOffset)/4;
    printf("Sizes: %i %i\n", size_cmd, size_my);
    return diff != 0 || size_cmd != size_my;
}
#endif

inline uint32_t align_up(uint32_t value, uint32_t granularity)
{
    return (value + (granularity-1)) & (~(granularity-1));
}
int main(int argc, char **argv)
{
    int rv;
    int width = 256;
    int height = 256;
    int padded_width = align_up(width, 64);
    int padded_height = align_up(height, 64);
    printf("padded_width %i padded_height %i\n", padded_width, padded_height);
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
    if(viv_alloc_linear_vidmem(conn, padded_width * padded_height * 4, 0x40, gcvSURF_RENDER_TARGET, gcvPOOL_DEFAULT, &rt_node, NULL)!=0)
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
    memset(rt_logical, 0xff, padded_width * padded_height * 4); /* clear previous result just in case, test that clearing works */

    /* allocate tile status for main render target */
    gcuVIDMEM_NODE_PTR rt_ts_node = 0;
    uint32_t rt_ts_size = align_up((padded_width * padded_height * 4)/0x100, 0x100);
    if(viv_alloc_linear_vidmem(conn, rt_ts_size, 0x40, gcvSURF_TILE_STATUS, gcvPOOL_DEFAULT, &rt_ts_node, NULL)!=0)
    {
        fprintf(stderr, "Error allocating render target tile status memory\n");
        exit(1);
    }
    printf("Allocated render target tile status node: node=%08x size=%08x\n", (uint32_t)rt_ts_node, rt_ts_size);
    
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
    if(viv_alloc_linear_vidmem(conn, padded_width * padded_height * 2, 0x40, gcvSURF_DEPTH, gcvPOOL_DEFAULT, &z_node, NULL)!=0)
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
    uint32_t z_ts_size = align_up((padded_width * padded_height * 2)/0x100, 0x100);
    if(viv_alloc_linear_vidmem(conn, z_ts_size, 0x40, gcvSURF_TILE_STATUS, gcvPOOL_DEFAULT, &z_ts_node, NULL)!=0)
    {
        fprintf(stderr, "Error allocating depth memory\n");
        exit(1);
    }
    printf("Allocated depth ts node: node=%08x size=%08x\n", (uint32_t)z_ts_node, z_ts_size);
    
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

    /* Phew, now we got all the memory we need.
     * Write interleaved attribute vertex stream.
     * Unlike the GL example we only do this once, not every time glDrawArrays is called, the same would be accomplished
     * from GL by using a vertex buffer object.
     */
    for(int vert=0; vert<NUM_VERTICES; ++vert)
    {
        int dest_idx = vert * (3 + 2);
        for(int comp=0; comp<3; ++comp)
            ((float*)vtx_logical)[dest_idx+comp+0] = vVertices[vert*3 + comp]; /* 0 */
        for(int comp=0; comp<2; ++comp)
            ((float*)vtx_logical)[dest_idx+comp+3] = vTexCoords[vert*2 + comp]; /* 1 */
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
    struct _gcoCMDBUF *cmdPtr = &commandBuffer;
    
    commandBuffer.free = commandBuffer.bytes - 0x8; /* Always keep 0x8 at end of buffer for kernel driver */
    commandBuffer.startOffset = 0;
    commandBuffer.offset = commandBuffer.startOffset + 8*4;
#ifdef CMD_COMPARE
    memset(is_padding, 0, 0x8000/4); /* keep track of what words in the command buffer are padding; just for debugging / comparing */
#endif

    /* XXX how important is the ordering? I suppose we could group states (except the flushes, kickers, semaphores etc)
     * and simply submit them at once. Especially for consecutive states and masked stated this could be a big win
     * in DMA command buffer size. */

    /* Build first command buffer */
    etna_set_state(cmdPtr, VIVS_GL_VERTEX_ELEMENT_CONFIG, 0x1);
    etna_set_state(cmdPtr, VIVS_RA_CONTROL, 0x1);

    etna_set_state(cmdPtr, VIVS_PA_W_CLIP_LIMIT, 0x34000001);
    etna_set_state(cmdPtr, VIVS_PA_SYSTEM_MODE, 0x11);
    etna_set_state(cmdPtr, VIVS_PA_CONFIG, VIV_MASKED_BIT(VIVS_PA_CONFIG_UNK22, 0));
    etna_set_state(cmdPtr, VIVS_SE_CONFIG, 0x0);
    etna_set_state(cmdPtr, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR);
    etna_set_state(cmdPtr, VIVS_PE_COLOR_FORMAT, 
            VIV_MASKED_BIT(VIVS_PE_COLOR_FORMAT_OVERWRITE, 0));
    etna_set_state(cmdPtr, VIVS_PE_ALPHA_CONFIG, /* can & all these together */
            VIV_MASKED_BIT(VIVS_PE_ALPHA_CONFIG_BLEND_ENABLE_COLOR, 0) &
            VIV_MASKED_BIT(VIVS_PE_ALPHA_CONFIG_BLEND_SEPARATE_ALPHA, 0));
    etna_set_state(cmdPtr, VIVS_PE_ALPHA_CONFIG,
            VIV_MASKED(VIVS_PE_ALPHA_CONFIG_SRC_FUNC_COLOR, BLEND_FUNC_ONE) &
            VIV_MASKED(VIVS_PE_ALPHA_CONFIG_SRC_FUNC_ALPHA, BLEND_FUNC_ONE));
    etna_set_state(cmdPtr, VIVS_PE_ALPHA_CONFIG,
            VIV_MASKED(VIVS_PE_ALPHA_CONFIG_DST_FUNC_COLOR, BLEND_FUNC_ZERO) &
            VIV_MASKED(VIVS_PE_ALPHA_CONFIG_DST_FUNC_ALPHA, BLEND_FUNC_ZERO));
    etna_set_state(cmdPtr, VIVS_PE_ALPHA_CONFIG,
            VIV_MASKED(VIVS_PE_ALPHA_CONFIG_EQ_COLOR, BLEND_EQ_ADD) &
            VIV_MASKED(VIVS_PE_ALPHA_CONFIG_EQ_ALPHA, BLEND_EQ_ADD));
    etna_set_state(cmdPtr, VIVS_PE_ALPHA_BLEND_COLOR, 
            VIVS_PE_ALPHA_BLEND_COLOR_B(0) | 
            VIVS_PE_ALPHA_BLEND_COLOR_G(0) | 
            VIVS_PE_ALPHA_BLEND_COLOR_R(0) | 
            VIVS_PE_ALPHA_BLEND_COLOR_A(0));
    
    etna_set_state(cmdPtr, VIVS_PE_ALPHA_OP, VIV_MASKED_BIT(VIVS_PE_ALPHA_OP_ALPHA_TEST, 0));
    etna_set_state(cmdPtr, VIVS_PA_CONFIG, VIV_MASKED_INL(VIVS_PA_CONFIG_CULL_FACE_MODE, OFF));
    etna_set_state(cmdPtr, VIVS_PE_DEPTH_CONFIG, VIV_MASKED_BIT(VIVS_PE_DEPTH_CONFIG_WRITE_ENABLE, 0));
    etna_set_state(cmdPtr, VIVS_PE_STENCIL_CONFIG, VIV_MASKED(VIVS_PE_STENCIL_CONFIG_REF_FRONT, 0));
    etna_set_state(cmdPtr, VIVS_PE_STENCIL_OP, VIV_MASKED(VIVS_PE_STENCIL_OP_FUNC_FRONT, COMPARE_FUNC_ALWAYS));
    etna_set_state(cmdPtr, VIVS_PE_STENCIL_OP, VIV_MASKED(VIVS_PE_STENCIL_OP_FUNC_BACK, COMPARE_FUNC_ALWAYS));
    etna_set_state(cmdPtr, VIVS_PE_STENCIL_CONFIG, VIV_MASKED(VIVS_PE_STENCIL_CONFIG_MASK_FRONT, 0xff));
    etna_set_state(cmdPtr, VIVS_PE_STENCIL_CONFIG, VIV_MASKED(VIVS_PE_STENCIL_CONFIG_WRITE_MASK, 0xff));
    etna_set_state(cmdPtr, VIVS_PE_STENCIL_OP, VIV_MASKED(VIVS_PE_STENCIL_OP_FAIL_FRONT, STENCIL_OP_KEEP));
    etna_set_state(cmdPtr, VIVS_PE_DEPTH_CONFIG, VIV_MASKED_BIT(VIVS_PE_DEPTH_CONFIG_EARLY_Z, 0));
    etna_set_state(cmdPtr, VIVS_PE_STENCIL_OP, VIV_MASKED(VIVS_PE_STENCIL_OP_FAIL_BACK, STENCIL_OP_KEEP));
    etna_set_state(cmdPtr, VIVS_PE_DEPTH_CONFIG, VIV_MASKED_BIT(VIVS_PE_DEPTH_CONFIG_EARLY_Z, 0));
    etna_set_state(cmdPtr, VIVS_PE_STENCIL_OP, VIV_MASKED(VIVS_PE_STENCIL_OP_DEPTH_FAIL_FRONT, STENCIL_OP_KEEP));
    etna_set_state(cmdPtr, VIVS_PE_DEPTH_CONFIG, VIV_MASKED_BIT(VIVS_PE_DEPTH_CONFIG_EARLY_Z, 0));
    etna_set_state(cmdPtr, VIVS_PE_STENCIL_OP, VIV_MASKED(VIVS_PE_STENCIL_OP_DEPTH_FAIL_BACK, STENCIL_OP_KEEP));
    etna_set_state(cmdPtr, VIVS_PE_DEPTH_CONFIG, VIV_MASKED_BIT(VIVS_PE_DEPTH_CONFIG_EARLY_Z, 0));
    etna_set_state(cmdPtr, VIVS_PE_STENCIL_OP, VIV_MASKED(VIVS_PE_STENCIL_OP_PASS_FRONT, STENCIL_OP_KEEP));
    etna_set_state(cmdPtr, VIVS_PE_DEPTH_CONFIG, VIV_MASKED_BIT(VIVS_PE_DEPTH_CONFIG_EARLY_Z, 0));
    etna_set_state(cmdPtr, VIVS_PE_STENCIL_OP, VIV_MASKED(VIVS_PE_STENCIL_OP_PASS_BACK, STENCIL_OP_KEEP));
    etna_set_state(cmdPtr, VIVS_PE_DEPTH_CONFIG, VIV_MASKED_BIT(VIVS_PE_DEPTH_CONFIG_EARLY_Z, 0));
    etna_set_state(cmdPtr, VIVS_PE_COLOR_FORMAT, VIV_MASKED(VIVS_PE_COLOR_FORMAT_COMPONENTS, 0xf));

    etna_set_state(cmdPtr, VIVS_SE_DEPTH_SCALE, 0x0);
    etna_set_state(cmdPtr, VIVS_SE_DEPTH_BIAS, 0x0);
    
    etna_set_state(cmdPtr, VIVS_PA_CONFIG, VIV_MASKED_INL(VIVS_PA_CONFIG_FILL_MODE, SOLID));
    etna_set_state(cmdPtr, VIVS_PA_CONFIG, VIV_MASKED_INL(VIVS_PA_CONFIG_SHADE_MODEL, SMOOTH));
    etna_set_state(cmdPtr, VIVS_PE_COLOR_FORMAT, 
            VIV_MASKED(VIVS_PE_COLOR_FORMAT_FORMAT, RS_FORMAT_A8R8G8B8) &
            VIV_MASKED_BIT(VIVS_PE_COLOR_FORMAT_SUPER_TILED, 1));

    etna_set_state(cmdPtr, VIVS_PE_COLOR_ADDR, rt_physical); /* ADDR_A */
    etna_set_state(cmdPtr, VIVS_PE_COLOR_STRIDE, padded_width * 4); 
    etna_set_state(cmdPtr, VIVS_GL_MULTI_SAMPLE_CONFIG, 
            VIV_MASKED_INL(VIVS_GL_MULTI_SAMPLE_CONFIG_MSAA_SAMPLES, NONE) &
            VIV_MASKED(VIVS_GL_MULTI_SAMPLE_CONFIG_MSAA_ENABLES, 0xf) &
            VIV_MASKED(VIVS_GL_MULTI_SAMPLE_CONFIG_UNK12, 0x0) &
            VIV_MASKED(VIVS_GL_MULTI_SAMPLE_CONFIG_UNK16, 0x0)
            ); 
    etna_set_state(cmdPtr, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR);
    etna_set_state(cmdPtr, VIVS_PE_COLOR_FORMAT, VIV_MASKED_BIT(VIVS_PE_COLOR_FORMAT_OVERWRITE, 1));
    etna_set_state(cmdPtr, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR);
    etna_set_state(cmdPtr, VIVS_TS_COLOR_CLEAR_VALUE, 0);
    etna_set_state(cmdPtr, VIVS_TS_COLOR_STATUS_BASE, rt_ts_physical); /* ADDR_B */
    etna_set_state(cmdPtr, VIVS_TS_COLOR_SURFACE_BASE, rt_physical); /* ADDR_A */
    etna_set_state(cmdPtr, VIVS_TS_MEM_CONFIG, VIVS_TS_MEM_CONFIG_COLOR_FAST_CLEAR); /* ADDR_A */

    etna_set_state(cmdPtr, VIVS_PE_DEPTH_CONFIG, 
            VIV_MASKED_INL(VIVS_PE_DEPTH_CONFIG_DEPTH_FORMAT, D16) &
            VIV_MASKED_BIT(VIVS_PE_DEPTH_CONFIG_SUPER_TILED, 1)
            );
    etna_set_state(cmdPtr, VIVS_PE_DEPTH_ADDR, z_physical); /* ADDR_C */
    etna_set_state(cmdPtr, VIVS_PE_DEPTH_STRIDE, padded_width * 2);
    etna_set_state(cmdPtr, VIVS_PE_STENCIL_CONFIG, VIV_MASKED_INL(VIVS_PE_STENCIL_CONFIG_MODE, DISABLED));
    etna_set_state(cmdPtr, VIVS_PE_HDEPTH_CONTROL, VIVS_PE_HDEPTH_CONTROL_FORMAT_DISABLED);
    etna_set_state_f32(cmdPtr, VIVS_PE_DEPTH_NORMALIZE, 65535.0);
    etna_set_state(cmdPtr, VIVS_PE_DEPTH_CONFIG, VIV_MASKED_BIT(VIVS_PE_DEPTH_CONFIG_EARLY_Z, 0));
    etna_set_state(cmdPtr, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_DEPTH);

    etna_set_state(cmdPtr, VIVS_TS_DEPTH_CLEAR_VALUE, 0xffffffff);
    etna_set_state(cmdPtr, VIVS_TS_DEPTH_STATUS_BASE, z_ts_physical); /* ADDR_D */
    etna_set_state(cmdPtr, VIVS_TS_DEPTH_SURFACE_BASE, z_physical); /* ADDR_C */
    etna_set_state(cmdPtr, VIVS_TS_MEM_CONFIG, 
            VIVS_TS_MEM_CONFIG_DEPTH_FAST_CLEAR |
            VIVS_TS_MEM_CONFIG_COLOR_FAST_CLEAR |
            VIVS_TS_MEM_CONFIG_DEPTH_16BPP | 
            VIVS_TS_MEM_CONFIG_DEPTH_COMPRESSION);
    etna_set_state(cmdPtr, VIVS_PE_DEPTH_CONFIG, VIV_MASKED_BIT(VIVS_PE_DEPTH_CONFIG_EARLY_Z, 1)); /* flip-flopping once again */

    /* Warm up RS on aux render target */
    etna_set_state(cmdPtr, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR | VIVS_GL_FLUSH_CACHE_DEPTH);
    etna_warm_up_rs(cmdPtr, aux_rt_physical, aux_rt_ts_physical);

    /* Phew, now that's one hell of a setup; the serious rendering starts now */
    etna_set_state(cmdPtr, VIVS_TS_COLOR_STATUS_BASE, rt_ts_physical); /* ADDR_B */
    etna_set_state(cmdPtr, VIVS_TS_COLOR_SURFACE_BASE, rt_physical); /* ADDR_A */

    /* ... or so we thought */
    etna_set_state(cmdPtr, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR | VIVS_GL_FLUSH_CACHE_DEPTH);
    etna_warm_up_rs(cmdPtr, aux_rt_physical, aux_rt_ts_physical);

    /* maybe now? */
    etna_set_state(cmdPtr, VIVS_TS_COLOR_STATUS_BASE, rt_ts_physical); /* ADDR_B */
    etna_set_state(cmdPtr, VIVS_TS_COLOR_SURFACE_BASE, rt_physical); /* ADDR_A */
    etna_set_state(cmdPtr, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR | VIVS_GL_FLUSH_CACHE_DEPTH);
   
    /* nope, not really... */ 
    etna_set_state(cmdPtr, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR | VIVS_GL_FLUSH_CACHE_DEPTH);
    etna_warm_up_rs(cmdPtr, aux_rt_physical, aux_rt_ts_physical);
    etna_set_state(cmdPtr, VIVS_TS_COLOR_STATUS_BASE, rt_ts_physical); /* ADDR_B */
    etna_set_state(cmdPtr, VIVS_TS_COLOR_SURFACE_BASE, rt_physical); /* ADDR_A */

    /* semaphore time */
    etna_set_state(cmdPtr, VIVS_GL_SEMAPHORE_TOKEN, 
            VIVS_GL_SEMAPHORE_TOKEN_FROM(SYNC_RECIPIENT_RA)|
            VIVS_GL_SEMAPHORE_TOKEN_TO(SYNC_RECIPIENT_PE)
            );
    etna_set_state(cmdPtr, VIVS_GL_STALL_TOKEN, 
            VIVS_GL_STALL_TOKEN_FROM(SYNC_RECIPIENT_RA)|
            VIVS_GL_STALL_TOKEN_TO(SYNC_RECIPIENT_PE)
            );

    /* Set up the resolve to clear tile status for main render target 
     * What the blob does is regard the TS as an image of width N, height 4, with 4 bytes per pixel
     * Looks like the height always stays the same. I don't think it matters as long as the entire memory are is covered.
     * XXX need to clear the depth ts too.
     * */
    etna_set_state(cmdPtr, VIVS_RS_CONFIG,
            VIVS_RS_CONFIG_SOURCE_FORMAT(RS_FORMAT_A8R8G8B8) |
            VIVS_RS_CONFIG_DEST_FORMAT(RS_FORMAT_A8R8G8B8)
            );
    etna_set_state_multi(cmdPtr, VIVS_RS_DITHER(0), 2, (uint32_t[]){0xffffffff, 0xffffffff});
    etna_set_state(cmdPtr, VIVS_RS_DEST_ADDR, rt_ts_physical); /* ADDR_B */
    etna_set_state(cmdPtr, VIVS_RS_DEST_STRIDE, 0x100); /* 0x100 iso 0x40! seems it uses a width of 256 if width divisible by 256 */
    etna_set_state(cmdPtr, VIVS_RS_WINDOW_SIZE, 
            VIVS_RS_WINDOW_SIZE_HEIGHT(rt_ts_size/0x100) |
            VIVS_RS_WINDOW_SIZE_WIDTH(64));
    etna_set_state(cmdPtr, VIVS_RS_FILL_VALUE(0), 0x55555555);
    etna_set_state(cmdPtr, VIVS_RS_CLEAR_CONTROL, 
            VIVS_RS_CLEAR_CONTROL_MODE_ENABLED1 |
            VIVS_RS_CLEAR_CONTROL_BITS(0xffff));
    etna_set_state(cmdPtr, VIVS_RS_EXTRA_CONFIG, 
            0); /* no AA, no endian switch */
    etna_set_state(cmdPtr, VIVS_RS_KICKER, 
            0xbeebbeeb);
    
    etna_set_state(cmdPtr, VIVS_TS_COLOR_CLEAR_VALUE, 0xff7f7f7f);
    etna_set_state(cmdPtr, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR);
    etna_set_state(cmdPtr, VIVS_TS_COLOR_CLEAR_VALUE, 0xff7f7f7f);
    etna_set_state(cmdPtr, VIVS_TS_COLOR_STATUS_BASE, rt_ts_physical); /* ADDR_B */
    etna_set_state(cmdPtr, VIVS_TS_COLOR_SURFACE_BASE, rt_physical); /* ADDR_A */
    etna_set_state(cmdPtr, VIVS_TS_MEM_CONFIG, 
            VIVS_TS_MEM_CONFIG_DEPTH_FAST_CLEAR |
            VIVS_TS_MEM_CONFIG_COLOR_FAST_CLEAR |
            VIVS_TS_MEM_CONFIG_DEPTH_16BPP | 
            VIVS_TS_MEM_CONFIG_DEPTH_COMPRESSION);
    //etna_set_state(cmdPtr, VIVS_PA_CONFIG, VIV_MASKED_INL(VIVS_PA_CONFIG_CULL_FACE_MODE, CCW));
    etna_set_state(cmdPtr, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR | VIVS_GL_FLUSH_CACHE_DEPTH);

    etna_set_state(cmdPtr, VIVS_PE_DEPTH_CONFIG, VIV_MASKED_BIT(VIVS_PE_DEPTH_CONFIG_WRITE_ENABLE, 0));
    etna_set_state(cmdPtr, VIVS_PE_DEPTH_CONFIG, VIV_MASKED_INL(VIVS_PE_DEPTH_CONFIG_DEPTH_MODE, NONE));
    etna_set_state(cmdPtr, VIVS_PE_DEPTH_CONFIG, VIV_MASKED_BIT(VIVS_PE_DEPTH_CONFIG_WRITE_ENABLE, 0));
    etna_set_state(cmdPtr, VIVS_PE_DEPTH_CONFIG, VIV_MASKED(VIVS_PE_DEPTH_CONFIG_DEPTH_FUNC, COMPARE_FUNC_ALWAYS));
    etna_set_state(cmdPtr, VIVS_PE_DEPTH_CONFIG, VIV_MASKED_INL(VIVS_PE_DEPTH_CONFIG_DEPTH_MODE, Z));
    etna_set_state_f32(cmdPtr, VIVS_PE_DEPTH_NEAR, 0.0);
    etna_set_state_f32(cmdPtr, VIVS_PE_DEPTH_FAR, 1.0);
    etna_set_state_f32(cmdPtr, VIVS_PE_DEPTH_NORMALIZE, 65535.0);

    /* set up primitive assembly */
    etna_set_state_f32(cmdPtr, VIVS_PA_VIEWPORT_OFFSET_Z, 0.0);
    etna_set_state_f32(cmdPtr, VIVS_PA_VIEWPORT_SCALE_Z, 1.0);
    etna_set_state(cmdPtr, VIVS_PE_DEPTH_CONFIG, VIV_MASKED_BIT(VIVS_PE_DEPTH_CONFIG_ONLY_DEPTH, 0));
    etna_set_state_fixp(cmdPtr, VIVS_PA_VIEWPORT_OFFSET_X, width << 15);
    etna_set_state_fixp(cmdPtr, VIVS_PA_VIEWPORT_OFFSET_Y, height << 15);
    etna_set_state_fixp(cmdPtr, VIVS_PA_VIEWPORT_SCALE_X, width << 15);
    etna_set_state_fixp(cmdPtr, VIVS_PA_VIEWPORT_SCALE_Y, height << 15);
    etna_set_state_fixp(cmdPtr, VIVS_SE_SCISSOR_LEFT, 0);
    etna_set_state_fixp(cmdPtr, VIVS_SE_SCISSOR_TOP, 0);
    etna_set_state_fixp(cmdPtr, VIVS_SE_SCISSOR_RIGHT, (width << 16) | 5);
    etna_set_state_fixp(cmdPtr, VIVS_SE_SCISSOR_BOTTOM, (height << 16) | 5);

    /* Now load the shader itself */
    uint32_t vs[] = {
        0x02001001, 0x2a800800, 0x00000000, 0x003fc008,
        0x02001003, 0x2a800800, 0x00000040, 0x00000002,
    };
    uint32_t vs_size = sizeof(vs);
    uint32_t *ps;
    uint32_t ps_size;
    if(argc < 2)
    {
        perror("provide shader on command line");
        exit(1);
    }
    int fd = open(argv[1], O_RDONLY);
    if(fd == -1)
    {
        perror("opening shader");
        exit(1);
    }
    ps_size = lseek(fd, 0, SEEK_END);
    ps = malloc(ps_size);
    lseek(fd, 0, SEEK_SET);
    if(ps_size == 0 || read(fd, ps, ps_size) != ps_size)
    {
        perror("empty or unreadable shader");
        exit(1);
    }
    close(fd);

    /* shader setup */
    etna_set_state(cmdPtr, VIVS_VS_END_PC, vs_size/16);
    etna_set_state_multi(cmdPtr, VIVS_VS_INPUT_COUNT, 3, (uint32_t[]){
            /* VIVS_VS_INPUT_COUNT */ (1<<8) | 2,
            /* VIVS_VS_TEMP_REGISTER_CONTROL */ VIVS_VS_TEMP_REGISTER_CONTROL_NUM_TEMPS(2),
            /* VIVS_VS_OUTPUT(0) */ 0x100});
    etna_set_state(cmdPtr, VIVS_VS_START_PC, 0x0);
    etna_set_state_f32(cmdPtr, VIVS_VS_UNIFORMS(0), 0.5); /* u0.x */

    etna_set_state_multi(cmdPtr, VIVS_VS_INST_MEM(0), vs_size/4, vs);
    etna_set_state(cmdPtr, VIVS_RA_CONTROL, 0x3); /* huh, this is 1 for the cubes */
    etna_set_state_multi(cmdPtr, VIVS_PS_END_PC, 2, (uint32_t[]){
            /* VIVS_PS_END_PC */ ps_size/16,
            /* VIVS_PS_OUTPUT_REG */ 0x1});
    etna_set_state(cmdPtr, VIVS_PS_START_PC, 0x0);
    etna_set_state(cmdPtr, VIVS_PA_SHADER_ATTRIBUTES(0), 0x200);
    etna_set_state(cmdPtr, VIVS_GL_VARYING_NUM_COMPONENTS,  /* one varying, with four components */
            VIVS_GL_VARYING_NUM_COMPONENTS_VAR0(2)
            );
    etna_set_state_multi(cmdPtr, VIVS_GL_VARYING_COMPONENT_USE(0), 2, (uint32_t[]){ /* one varying, with four components */
            VIVS_GL_VARYING_COMPONENT_USE_COMP0(VARYING_COMPONENT_USE_USED) |
            VIVS_GL_VARYING_COMPONENT_USE_COMP1(VARYING_COMPONENT_USE_USED) |
            VIVS_GL_VARYING_COMPONENT_USE_COMP2(VARYING_COMPONENT_USE_UNUSED) |
            VIVS_GL_VARYING_COMPONENT_USE_COMP3(VARYING_COMPONENT_USE_UNUSED)
            , 0
            });
    etna_set_state_f32(cmdPtr, VIVS_PS_UNIFORMS(0), 0.0); /* u0.x */
    etna_set_state_f32(cmdPtr, VIVS_PS_UNIFORMS(1), 1.0); /* u0.y */
    etna_set_state_f32(cmdPtr, VIVS_PS_UNIFORMS(2), 0.5); /* u0.z */
    etna_set_state_f32(cmdPtr, VIVS_PS_UNIFORMS(3), 2.0); /* u0.w */
    etna_set_state_f32(cmdPtr, VIVS_PS_UNIFORMS(4), 1/256.0); /* u1.x */
    etna_set_state_f32(cmdPtr, VIVS_PS_UNIFORMS(5), 16.0); /* u1.y */
    etna_set_state_f32(cmdPtr, VIVS_PS_UNIFORMS(6), 10.0); /* u1.z */
    etna_set_state_multi(cmdPtr, VIVS_PS_INST_MEM(0), ps_size/4, ps);
    etna_set_state(cmdPtr, VIVS_PS_INPUT_COUNT, (31<<8)|2);
    etna_set_state(cmdPtr, VIVS_PS_TEMP_REGISTER_CONTROL, 
            VIVS_PS_TEMP_REGISTER_CONTROL_NUM_TEMPS(4));
    etna_set_state(cmdPtr, VIVS_PS_CONTROL, 
            VIVS_PS_CONTROL_UNK1
            );
    etna_set_state(cmdPtr, VIVS_PA_ATTRIBUTE_ELEMENT_COUNT, 0x100);
    etna_set_state(cmdPtr, VIVS_GL_VARYING_TOTAL_COMPONENTS,  /* one varying, with two components, must be 
                                                            changed together with GL_VARYING_NUM_COMPONENTS */
            VIVS_GL_VARYING_TOTAL_COMPONENTS_NUM(2)
            );
    etna_set_state(cmdPtr, VIVS_VS_LOAD_BALANCING, 0xf3f0582);
    etna_set_state(cmdPtr, VIVS_VS_OUTPUT_COUNT, 2);
    etna_set_state(cmdPtr, VIVS_PA_CONFIG, VIV_MASKED_BIT(VIVS_PA_CONFIG_POINT_SIZE_ENABLE, 0));
    
    etna_set_state(cmdPtr, VIVS_FE_VERTEX_STREAM_BASE_ADDR, vtx_physical); /* ADDR_E */
    etna_set_state(cmdPtr, VIVS_FE_VERTEX_STREAM_CONTROL, 
            VIVS_FE_VERTEX_STREAM_CONTROL_VERTEX_STRIDE(0x14));
    etna_set_state(cmdPtr, VIVS_FE_VERTEX_ELEMENT_CONFIG(0), 
            VIVS_FE_VERTEX_ELEMENT_CONFIG_TYPE_FLOAT |
            VIVS_FE_VERTEX_ELEMENT_CONFIG_ENDIAN(ENDIAN_MODE_NO_SWAP) |
            VIVS_FE_VERTEX_ELEMENT_CONFIG_STREAM(0) |
            VIVS_FE_VERTEX_ELEMENT_CONFIG_NUM(3) |
            VIVS_FE_VERTEX_ELEMENT_CONFIG_NORMALIZE_OFF |
            VIVS_FE_VERTEX_ELEMENT_CONFIG_START(0x0) |
            VIVS_FE_VERTEX_ELEMENT_CONFIG_END(0xc));
    etna_set_state(cmdPtr, VIVS_FE_VERTEX_ELEMENT_CONFIG(1), 
            VIVS_FE_VERTEX_ELEMENT_CONFIG_TYPE_FLOAT |
            VIVS_FE_VERTEX_ELEMENT_CONFIG_ENDIAN(ENDIAN_MODE_NO_SWAP) |
            VIVS_FE_VERTEX_ELEMENT_CONFIG_NONCONSECUTIVE |
            VIVS_FE_VERTEX_ELEMENT_CONFIG_STREAM(0) |
            VIVS_FE_VERTEX_ELEMENT_CONFIG_NUM(2) |
            VIVS_FE_VERTEX_ELEMENT_CONFIG_NORMALIZE_OFF |
            VIVS_FE_VERTEX_ELEMENT_CONFIG_START(0xc) |
            VIVS_FE_VERTEX_ELEMENT_CONFIG_END(0x14));
    etna_set_state(cmdPtr, VIVS_VS_INPUT(0), 0x00100); /* 0x20000 in etna_cube */
    etna_set_state(cmdPtr, VIVS_PA_CONFIG, VIV_MASKED_BIT(VIVS_PA_CONFIG_POINT_SPRITE_ENABLE, 0));
    etna_draw_primitives(cmdPtr, PRIMITIVE_TYPE_TRIANGLE_STRIP, 0, 2);

    etna_set_state(cmdPtr, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR | VIVS_GL_FLUSH_CACHE_DEPTH);

#ifdef CMD_COMPARE
    /* Set addresses in first command buffer */
    cmdbuf1[0x57] = cmdbuf1[0x67] = cmdbuf1[0x9f] = cmdbuf1[0xbb] = cmdbuf1[0xd9] = cmdbuf1[0xfb] = rt_physical; /* ADDR_A */
    cmdbuf1[0x65] = cmdbuf1[0x9d] = cmdbuf1[0xb9] = cmdbuf1[0xd7] = cmdbuf1[0xe5] = cmdbuf1[0xf9] = rt_ts_physical; /* ADDR_B */
    cmdbuf1[0x6d] = cmdbuf1[0x7f] = z_physical; /* ADDR_C */
    cmdbuf1[0x7d] = z_ts_physical; /* ADDR_D */
    cmdbuf1[0x165] = vtx_physical; /* ADDR_E */
    cmdbuf1[0x89] = cmdbuf1[0x8f] = cmdbuf1[0x93] = cmdbuf1[0xa5] = cmdbuf1[0xab] = cmdbuf1[0xaf] = cmdbuf1[0xc3] = cmdbuf1[0xc9] = cmdbuf1[0xcd] = aux_rt_physical; /* ADDR_F */
    cmdbuf1[0x87] = cmdbuf1[0xa3] = cmdbuf1[0xc1] = aux_rt_ts_physical; /* ADDR_G */
    
    /* Must exactly match */
    if(cmdbuffer_compare(cmdPtr, cmdbuf1, sizeof(cmdbuf1)))
        exit(1);
#endif

    /* Submit first command buffer */
    commandBuffer.free = commandBuffer.bytes - commandBuffer.offset - 0x8;
    printf("[1] startOffset=%08x, offset=%08x, free=%08x\n", (uint32_t)commandBuffer.startOffset, (uint32_t)commandBuffer.offset, (uint32_t)commandBuffer.free);
    if(viv_commit(conn, &commandBuffer, &contextBuffer) != 0)
    {
        fprintf(stderr, "Error committing first command buffer\n");
        exit(1);
    }
    // printf("[1] Contextbuffer used %i\n", *contextBuffer.inUse);

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

    /* Build second command buffer */
    commandBuffer.startOffset = commandBuffer.offset + 0x18;
    commandBuffer.offset = commandBuffer.startOffset + 8*4;

    etna_set_state(cmdPtr, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR | VIVS_GL_FLUSH_CACHE_DEPTH);
    etna_set_state(cmdPtr, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR | VIVS_GL_FLUSH_CACHE_DEPTH);
    etna_set_state(cmdPtr, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR | VIVS_GL_FLUSH_CACHE_DEPTH);
    etna_set_state(cmdPtr, VIVS_RS_CONFIG,
            VIVS_RS_CONFIG_SOURCE_FORMAT(RS_FORMAT_A8R8G8B8) |
            VIVS_RS_CONFIG_SOURCE_TILED |
            VIVS_RS_CONFIG_DEST_FORMAT(RS_FORMAT_A8R8G8B8) |
            VIVS_RS_CONFIG_DEST_TILED);
    etna_set_state(cmdPtr, VIVS_RS_SOURCE_STRIDE, (padded_width * 4 * 4) | VIVS_RS_SOURCE_STRIDE_TILING);
    etna_set_state(cmdPtr, VIVS_RS_DEST_STRIDE, (padded_width * 4 * 4) | VIVS_RS_DEST_STRIDE_TILING);
    etna_set_state(cmdPtr, VIVS_RS_DITHER(0), 0xffffffff);
    etna_set_state(cmdPtr, VIVS_RS_DITHER(1), 0xffffffff);
    etna_set_state(cmdPtr, VIVS_RS_CLEAR_CONTROL, VIVS_RS_CLEAR_CONTROL_MODE_DISABLED);
    etna_set_state(cmdPtr, VIVS_RS_EXTRA_CONFIG, 0); /* no AA, no endian switch */
    etna_set_state(cmdPtr, VIVS_RS_SOURCE_ADDR, rt_physical); /* ADDR_A */
    etna_set_state(cmdPtr, VIVS_RS_DEST_ADDR, rt_physical); /* ADDR_A */
    etna_set_state(cmdPtr, VIVS_RS_WINDOW_SIZE, 
            VIVS_RS_WINDOW_SIZE_HEIGHT(padded_height) |
            VIVS_RS_WINDOW_SIZE_WIDTH(padded_width));
    etna_set_state(cmdPtr, VIVS_RS_KICKER, 0xbeebbeeb);

    commandBuffer.free = commandBuffer.bytes - commandBuffer.offset - 0x8;
    printf("[2] startOffset=%08x, offset=%08x, free=%08x\n", (uint32_t)commandBuffer.startOffset, (uint32_t)commandBuffer.offset, (uint32_t)commandBuffer.free);
    if(viv_commit(conn, &commandBuffer, &contextBuffer) != 0)
    {
        fprintf(stderr, "Error committing second command buffer\n");
        exit(1);
    }
    printf("[2] Contextbuffer used %i\n", *contextBuffer.inUse);

    /* Build third command buffer 
     * Third command buffer does some cache flush trick?
     * It can be left out without any visible harm.
     **/
    commandBuffer.startOffset = commandBuffer.offset + 0x18;
    commandBuffer.offset = commandBuffer.startOffset + 8*4;
    etna_warm_up_rs(cmdPtr, aux_rt_physical, aux_rt_ts_physical);

    etna_set_state(cmdPtr, VIVS_TS_COLOR_STATUS_BASE, rt_ts_physical); /* ADDR_B */
    etna_set_state(cmdPtr, VIVS_TS_COLOR_SURFACE_BASE, rt_physical); /* ADDR_A */
    etna_set_state(cmdPtr, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR);
    etna_set_state(cmdPtr, VIVS_TS_MEM_CONFIG, 
            VIVS_TS_MEM_CONFIG_DEPTH_FAST_CLEAR |
            VIVS_TS_MEM_CONFIG_DEPTH_16BPP | 
            VIVS_TS_MEM_CONFIG_DEPTH_COMPRESSION);
    etna_set_state(cmdPtr, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR);
    etna_set_state(cmdPtr, VIVS_PE_COLOR_FORMAT, 
            VIV_MASKED_BIT(VIVS_PE_COLOR_FORMAT_OVERWRITE, 0));

    /* Submit third command buffer */
    commandBuffer.free = commandBuffer.bytes - commandBuffer.offset - 0x8;
    printf("[3] startOffset=%08x, offset=%08x, free=%08x\n", (uint32_t)commandBuffer.startOffset, (uint32_t)commandBuffer.offset, (uint32_t)commandBuffer.free);
    if(viv_commit(conn, &commandBuffer, &contextBuffer) != 0)
    {
        fprintf(stderr, "Error committing third command buffer\n");
        exit(1);
    }
    printf("[3] Contextbuffer used %i\n", *contextBuffer.inUse);

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
    if(viv_alloc_linear_vidmem(conn, width*height*4, 0x40, gcvSURF_BITMAP, gcvPOOL_DEFAULT, &bmp_node, NULL)!=0)
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
    memset(bmp_logical, 0xff, width*height*4); /* clear previous result */
    printf("Locked bmp: phys=%08x log=%08x\n", (uint32_t)bmp_physical, (uint32_t)bmp_logical);

    /* Start building fourth command buffer
     * Fourth command buffer copies render result to bitmap, detiling along the way. 
     */
    commandBuffer.startOffset = commandBuffer.offset + 0x18;
    commandBuffer.offset = commandBuffer.startOffset + 8*4;
    etna_set_state(cmdPtr, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR | VIVS_GL_FLUSH_CACHE_DEPTH);
    etna_set_state(cmdPtr, VIVS_RS_CONFIG,
            VIVS_RS_CONFIG_SOURCE_FORMAT(RS_FORMAT_A8R8G8B8) |
            VIVS_RS_CONFIG_SOURCE_TILED |
            VIVS_RS_CONFIG_DEST_FORMAT(RS_FORMAT_A8R8G8B8) /*|
            VIVS_RS_CONFIG_SWAP_RB*/);
    etna_set_state(cmdPtr, VIVS_RS_SOURCE_STRIDE, (padded_width * 4 * 4) | VIVS_RS_SOURCE_STRIDE_TILING);
    etna_set_state(cmdPtr, VIVS_RS_DEST_STRIDE, width * 4);
    etna_set_state(cmdPtr, VIVS_RS_DITHER(0), 0xffffffff);
    etna_set_state(cmdPtr, VIVS_RS_DITHER(1), 0xffffffff);
    etna_set_state(cmdPtr, VIVS_RS_CLEAR_CONTROL, VIVS_RS_CLEAR_CONTROL_MODE_DISABLED);
    etna_set_state(cmdPtr, VIVS_RS_EXTRA_CONFIG, 
            0); /* no AA, no endian switch */
    etna_set_state(cmdPtr, VIVS_RS_SOURCE_ADDR, rt_physical); /* ADDR_A */
    etna_set_state(cmdPtr, VIVS_RS_DEST_ADDR, bmp_physical); /* ADDR_J */
    etna_set_state(cmdPtr, VIVS_RS_WINDOW_SIZE, 
            VIVS_RS_WINDOW_SIZE_HEIGHT(height) |
            VIVS_RS_WINDOW_SIZE_WIDTH(width));
    etna_set_state(cmdPtr, VIVS_RS_KICKER, 0xbeebbeeb);

    /* Submit fourth command buffer
     */
    commandBuffer.free = commandBuffer.bytes - commandBuffer.offset - 0x8;
    printf("[4] startOffset=%08x, offset=%08x, free=%08x\n", (uint32_t)commandBuffer.startOffset, (uint32_t)commandBuffer.offset, (uint32_t)commandBuffer.free);
    if(viv_commit(conn, &commandBuffer, &contextBuffer) != 0)
    {
        fprintf(stderr, "Error committing fourth command buffer\n");
        exit(1);
    }
    printf("[4] Contextbuffer used %i\n", *contextBuffer.inUse);

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
    if(argc>2)
        bmp_dump32(bmp_logical, width, height, true, argv[2]);
    /* Unlock video memory */
    if(viv_unlock_vidmem(conn, bmp_node, gcvSURF_BITMAP, 1) != 0)
    {
        fprintf(stderr, "Cannot unlock vidmem\n");
        exit(1);
    }

    viv_close(conn);
    return 0;
}

