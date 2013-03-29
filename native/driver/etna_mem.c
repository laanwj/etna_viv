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
#include "etna_mem.h"
#include "etna.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

//#define DEBUG
#define ETNA_VIDMEM_ALIGNMENT (0x40) 

int etna_vidmem_alloc_linear(struct viv_conn *conn, struct etna_vidmem **mem_out, size_t bytes, gceSURF_TYPE type, gcePOOL pool, bool lock)
{
    if(mem_out == NULL) return ETNA_INVALID_ADDR;
    struct etna_vidmem *mem = ETNA_NEW(struct etna_vidmem);
    if(mem == NULL) return ETNA_OUT_OF_MEMORY;

    mem->type = type;

    if(viv_alloc_linear_vidmem(conn, bytes, ETNA_VIDMEM_ALIGNMENT, type, pool, &mem->node, &mem->size)!=0)
    {
#ifdef DEBUG
        fprintf(stderr, "Error allocating render target tile status memory\n");
#endif
        return ETNA_OUT_OF_MEMORY;
    }
#ifdef DEBUG
    printf("Allocated: node=%08x size=%08x\n", (uint32_t)mem->node, mem->size);
#endif
    if(lock)
    {
        int status = etna_vidmem_lock(conn, mem);
        if(status != ETNA_OK)
        {
            etna_vidmem_free(conn, mem);
            return status;
        }
    }
    *mem_out = mem;
    return ETNA_OK;
}

int etna_vidmem_lock(struct viv_conn *conn, struct etna_vidmem *mem)
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
    printf("Locked: phys=%08x log=%08x\n", (uint32_t)mem->address, (uint32_t)mem->logical);
#endif

    return ETNA_OK;
}

int etna_vidmem_unlock(struct viv_conn *conn, struct etna_vidmem *mem)
{
    if(mem == NULL) return ETNA_INVALID_ADDR;

    if(viv_unlock_vidmem(conn, mem->node, mem->type, 1) != ETNA_OK)
    {
        return ETNA_INTERNAL_ERROR;
    }
    mem->logical = NULL;
    return ETNA_OK;
}

int etna_vidmem_free(struct viv_conn *conn, struct etna_vidmem *mem)
{
    if(mem == NULL) return ETNA_INVALID_ADDR;
    viv_free_vidmem(conn, mem->node);
    free(mem);
    return ETNA_OK;
}

int etna_usermem_map(struct viv_conn *conn, struct etna_usermem **mem_out, void *memory, size_t size)
{
    if(mem_out == NULL) return ETNA_INVALID_ADDR;
    struct etna_usermem *mem = ETNA_NEW(struct etna_usermem);

    mem->memory = memory;
    mem->size = size;

    if(viv_map_user_memory(conn, memory, size, &mem->info, &mem->address)!=0)
    {
        return ETNA_INTERNAL_ERROR;
    }

    *mem_out = mem;
    return ETNA_OK;
}

int etna_usermem_unmap(struct viv_conn *conn, struct etna_usermem *mem)
{
    if(mem == NULL) return ETNA_INVALID_ADDR;
    viv_unmap_user_memory(conn, mem->memory, mem->size, mem->info, mem->address);
    free(mem);
    return ETNA_OK;
}

