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
#include <etnaviv/etna_bo.h>
#include <etnaviv/etna.h>
#include <etnaviv/etna_queue.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>

#include "gc_abi.h"

//#define DEBUG
#define ETNA_VIDMEM_ALIGNMENT (0x40)

enum etna_bo_type {
    ETNA_BO_TYPE_VIDMEM,    /* Main vidmem */
    ETNA_BO_TYPE_VIDMEM_EXTERNAL, /* Main vidmem, external handle */
    ETNA_BO_TYPE_USERMEM,   /* Mapped user memory */
    ETNA_BO_TYPE_CONTIGUOUS,/* Contiguous memory */
    ETNA_BO_TYPE_PHYSICAL,  /* Mmap-ed physical memory */
    ETNA_BO_TYPE_DMABUF     /* dmabuf memory */
};

/* Structure describing a block of video or user memory */
struct etna_bo {
    enum etna_bo_type bo_type;
    size_t size;
    enum viv_surf_type type;
    viv_node_t node;
    viv_addr_t address;
    void *logical;
    viv_usermem_t usermem_info;
};

#ifdef DEBUG
static const char *etna_bo_surf_type(struct etna_bo *mem)
{
    const char *ret = NULL;

    switch (mem->type) {
        case VIV_SURF_UNKNOWN:
            ret = "VIV_SURF_UNKNOWN";
        break;

        case VIV_SURF_INDEX:
            ret = "VIV_SURF_INDEX";
        break;

        case VIV_SURF_VERTEX:
            ret = "VIV_SURF_VERTEX";
        break;

        case VIV_SURF_TEXTURE:
            ret = "VIV_SURF_TEXTURE";
        break;

        case VIV_SURF_RENDER_TARGET:
            ret = "VIV_SURF_RENDER_TARGET";
        break;

        case VIV_SURF_DEPTH:
            ret = "VIV_SURF_DEPTH";
        break;

        case VIV_SURF_BITMAP:
            ret = "VIV_SURF_BITMAP";
        break;

        case VIV_SURF_TILE_STATUS:
            ret = "VIV_SURF_TILE_STATUS";
        break;

        case VIV_SURF_IMAGE:
            ret = "VIV_SURF_IMAGE";
        break;

        case VIV_SURF_MASK:
            ret = "VIV_SURF_MASK";
        break;

        case VIV_SURF_SCISSOR:
            ret = "VIV_SURF_SCISSOR";
        break;

        case VIV_SURF_HIERARCHICAL_DEPTH:
            ret = "VIV_SURF_HIERARCHICAL_DEPTH";
        break;

        default:
           ret = "hmmmm?";
        break;
    }
    return ret;
}
#endif

/* Lock (map) memory into both CPU and GPU memory space. */
static int etna_bo_lock(struct viv_conn *conn, struct etna_bo *mem)
{
    if(mem == NULL) return ETNA_INVALID_ADDR;
    if(mem->logical != NULL) return ETNA_ALREADY_LOCKED;

    if(viv_lock_vidmem(conn, mem->node, &mem->address, &mem->logical)!=0)
    {
#ifdef DEBUG
        fprintf(stderr, "Error locking render target memory\n");
#endif
        return ETNA_INTERNAL_ERROR;
    }
#ifdef DEBUG
    fprintf(stderr, "Locked: phys=%08x log=%08x\n", (uint32_t)mem->address, (uint32_t)mem->logical);
#endif

    return ETNA_OK;
}

/* Unlock memory from both CPU and GPU memory space */
static int etna_bo_unlock(struct viv_conn *conn, struct etna_bo *mem, struct etna_queue *queue)
{
    if(mem == NULL) return ETNA_INVALID_ADDR;
    int async = 0;
    /* Unlocking video memory seems to be a two-step process. First try it synchronously
     * then the kernel can request an asynchronous component. Just queueing it asynchronously
     * in the first place will not free the virtual memory on v4 */
    if(viv_unlock_vidmem(conn, mem->node, mem->type, false, &async) != ETNA_OK)
    {
        return ETNA_INTERNAL_ERROR;
    }
    if(async)
    {
        if(queue)
        {
            /* If a queue is passed, add the async part at the end of the queue, to be submitted
             * with next flush.
             */
            if(etna_queue_unlock_vidmem(queue, mem->node, mem->type) != ETNA_OK)
            {
                return ETNA_INTERNAL_ERROR;
            }
        } else { /* No queue, need to submit async part directly as event */
            if(viv_unlock_vidmem(conn, mem->node, mem->type, true, &async) != ETNA_OK)
            {
                return ETNA_INTERNAL_ERROR;
            }
        }
    }
    mem->logical = NULL;
    mem->address = 0;
    return ETNA_OK;
}

struct etna_bo* etna_bo_new(struct viv_conn *conn, size_t bytes, uint32_t flags)
{
    struct etna_bo *mem = ETNA_CALLOC_STRUCT(etna_bo);
    if(mem == NULL) return NULL;

    if((flags & DRM_ETNA_GEM_TYPE_MASK) == DRM_ETNA_GEM_TYPE_CMD)
    {
        mem->bo_type = ETNA_BO_TYPE_CONTIGUOUS;
        /* Command buffers must be allocated with viv_alloc_contiguous */
        if(viv_alloc_contiguous(conn, bytes,
                    &mem->address,
                    &mem->logical,
                    &mem->size)!=0)
        {
            ETNA_FREE(mem);
            return NULL;
        }
    } else {
        enum viv_surf_type type = VIV_SURF_UNKNOWN;
        enum viv_pool pool = VIV_POOL_DEFAULT;
        /* Convert GEM bits to surface type */
        switch(flags & DRM_ETNA_GEM_TYPE_MASK)
        {
        case DRM_ETNA_GEM_TYPE_IDX: type = VIV_SURF_INDEX; break;
        case DRM_ETNA_GEM_TYPE_VTX: type = VIV_SURF_VERTEX; break;
        case DRM_ETNA_GEM_TYPE_TEX: type = VIV_SURF_TEXTURE; break;
        case DRM_ETNA_GEM_TYPE_RT:  type = VIV_SURF_RENDER_TARGET; break;
        case DRM_ETNA_GEM_TYPE_ZS:  type = VIV_SURF_DEPTH; break;
        case DRM_ETNA_GEM_TYPE_HZ:  type = VIV_SURF_HIERARCHICAL_DEPTH; break;
        case DRM_ETNA_GEM_TYPE_BMP: type = VIV_SURF_BITMAP; break;
        case DRM_ETNA_GEM_TYPE_TS:  type = VIV_SURF_TILE_STATUS; break;
        default: /* Invalid type */
            ETNA_FREE(mem);
            return NULL;
            break;
        }

        mem->bo_type = ETNA_BO_TYPE_VIDMEM;
        mem->type = type;
        if(viv_alloc_linear_vidmem(conn, bytes, ETNA_VIDMEM_ALIGNMENT, type, pool, &mem->node, &mem->size)!=0)
        {
#ifdef DEBUG
            fprintf(stderr, "Error allocating memory\n");
#endif
            return NULL;
        }
#ifdef DEBUG
        fprintf(stderr, "Allocated: type:%s mem=%p node=%08x size=%08x\n", etna_bo_surf_type(mem), mem, (uint32_t)mem->node, mem->size);
#endif
        int status = etna_bo_lock(conn, mem);
        if(status != ETNA_OK)
        {
            etna_bo_del(conn, mem, NULL);
            return NULL;
        }
    }
    return mem;
}

struct etna_bo *etna_bo_from_usermem_prot(struct viv_conn *conn, void *memory, size_t size, int prot)
{
    struct etna_bo *mem = ETNA_CALLOC_STRUCT(etna_bo);
    if(mem == NULL) return NULL;

    mem->bo_type = ETNA_BO_TYPE_USERMEM;
    mem->logical = memory;
    mem->size = size;

    if(viv_map_user_memory_prot(conn, memory, size, prot, &mem->usermem_info, &mem->address)!=0)
    {
        ETNA_FREE(mem);
        return NULL;
    }

    return mem;
}

struct etna_bo *etna_bo_from_usermem(struct viv_conn *conn, void *memory, size_t size)
{
    struct etna_bo *mem = ETNA_CALLOC_STRUCT(etna_bo);
    if(mem == NULL) return NULL;

    mem->bo_type = ETNA_BO_TYPE_USERMEM;
    mem->logical = memory;
    mem->size = size;

    if(viv_map_user_memory(conn, memory, size, &mem->usermem_info, &mem->address)!=0)
    {
        ETNA_FREE(mem);
        return NULL;
    }

    return mem;
}

struct etna_bo *etna_bo_from_fbdev(struct viv_conn *conn, int fd, size_t offset, size_t size)
{
    struct fb_fix_screeninfo finfo;
    struct etna_bo *mem = ETNA_CALLOC_STRUCT(etna_bo);
    if(mem == NULL) return NULL;

    if(ioctl(fd, FBIOGET_FSCREENINFO, &finfo))
        goto error;

    mem->bo_type = ETNA_BO_TYPE_PHYSICAL;
    if((mem->logical = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, offset)) == NULL)
        goto error;
    mem->address = finfo.smem_start + offset;
    mem->size = size;
    return mem;
error:
    ETNA_FREE(mem);
    return NULL;
}

struct etna_bo *etna_bo_from_name(struct viv_conn *conn, uint32_t name)
{
    struct etna_bo *mem = ETNA_CALLOC_STRUCT(etna_bo);
    if(mem == NULL) return NULL;

    mem->bo_type = ETNA_BO_TYPE_VIDMEM_EXTERNAL;
    mem->node = (viv_node_t)name;

    /* Lock to this address space */
    int status = etna_bo_lock(conn, mem);
    if(status != ETNA_OK)
    {
        free(mem);
        return NULL;
    }
    return mem;
}

struct etna_bo *etna_bo_from_dmabuf(struct viv_conn *conn, int fd, int prot)
{
    struct etna_bo *mem = ETNA_CALLOC_STRUCT(etna_bo);
    if(mem == NULL) return NULL;

    mem->bo_type = ETNA_BO_TYPE_DMABUF;

    if(viv_map_dmabuf(conn, fd, &mem->usermem_info, &mem->address, prot)!=0)
    {
        ETNA_FREE(mem);
        return NULL;
    }

    return mem;
}

struct etna_bo *etna_bo_ref(struct etna_bo *bo)
{
    /* TODO */
    return bo;
}

int etna_bo_del(struct viv_conn *conn, struct etna_bo *mem, struct etna_queue *queue)
{
    int rv = ETNA_OK;
    if(mem == NULL) return ETNA_OK;
    switch(mem->bo_type)
    {
    case ETNA_BO_TYPE_VIDMEM:
        if(mem->logical != NULL)
        {
            if((rv = etna_bo_unlock(conn, mem, queue)) != ETNA_OK)
            {
                fprintf(stderr, "etna: Warning: could not unlock memory\n");
            }
        }
        if(queue)
        {
            if((rv = etna_queue_free_vidmem(queue, mem->node)) != ETNA_OK)
            {
                fprintf(stderr, "etna: Warning: could not queue free video memory\n");
            }
        } else {
            if((rv = viv_free_vidmem(conn, mem->node, true)) != ETNA_OK)
            {
                fprintf(stderr, "etna: Warning: could not free video memory\n");
            }
        }
        break;
    case ETNA_BO_TYPE_VIDMEM_EXTERNAL:
        if((rv = etna_bo_unlock(conn, mem, queue)) != ETNA_OK)
        {
            fprintf(stderr, "etna: Warning: could not unlock memory\n");
        }
        break;
    case ETNA_BO_TYPE_USERMEM:
        if(queue)
        {
            rv = etna_queue_unmap_user_memory(queue, mem->logical, mem->size, mem->usermem_info, mem->address);
        } else
        {
            rv = viv_unmap_user_memory(conn, mem->logical, mem->size, mem->usermem_info, mem->address);
        }
        break;
    case ETNA_BO_TYPE_CONTIGUOUS:
        if(queue)
        {
            rv = etna_queue_free_contiguous(queue, mem->size, mem->address, mem->logical);
        } else {
            rv = viv_free_contiguous(conn, mem->size, mem->address, mem->logical);
        }
        break;
    case ETNA_BO_TYPE_PHYSICAL:
        if(munmap(mem->logical, mem->size) < 0)
        {
            rv = ETNA_OUT_OF_MEMORY;
        }
        break;
    case ETNA_BO_TYPE_DMABUF:
        if(queue)
        {
            rv = etna_queue_unmap_user_memory(queue, (void *)1, 1, mem->usermem_info, mem->address);
        } else {
            rv = viv_unmap_user_memory(conn, (void *)1, 1, mem->usermem_info, mem->address);
        }
        break;
    }
    ETNA_FREE(mem);
    return rv;
}

int etna_bo_get_name(struct etna_bo *bo, uint32_t *name)
{
    *name = (uint32_t)bo->node;
    return 0;
}

uint32_t etna_bo_handle(struct etna_bo *bo)
{
    return (uint32_t)bo->node;
}

uint32_t etna_bo_size(struct etna_bo *bo)
{
    return bo->size;
}

void *etna_bo_map(struct etna_bo *bo)
{
    return bo->logical;
}

int etna_bo_cpu_prep(struct etna_bo *bo, struct etna_ctx *pipe, uint32_t op)
{
    /* TODO */
    return 0;
}

void etna_bo_cpu_fini(struct etna_bo *bo)
{
    /* No-op */
}

uint32_t etna_bo_gpu_address(struct etna_bo *bo)
{
    return bo->address;
}

