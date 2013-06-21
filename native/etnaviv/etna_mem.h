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
/* etna: memory management functions */
#ifndef H_ETNA_MEM
#define H_ETNA_MEM

#include <etnaviv/viv.h>
#include <stdbool.h>

/* Structure describing a block of video memory */
struct etna_vidmem {
    size_t size;
    enum viv_surf_type type;
    viv_node_t node;
    viv_addr_t address;
    void *logical;
};

/* Structure describing a block of mapped user memory */
struct etna_usermem {
    void *memory;
    size_t size;
    void *info;
    viv_addr_t address;
};

int etna_vidmem_alloc_linear(struct viv_conn *conn, struct etna_vidmem **mem_out, size_t bytes, enum viv_surf_type type, enum viv_pool pool, bool lock);
int etna_vidmem_lock(struct viv_conn *conn, struct etna_vidmem *mem);
int etna_vidmem_unlock(struct viv_conn *conn, struct etna_vidmem *mem);
int etna_vidmem_free(struct viv_conn *conn, struct etna_vidmem *mem);

int etna_usermem_map(struct viv_conn *conn, struct etna_usermem **mem_out, void *memory, size_t size);
int etna_usermem_unmap(struct viv_conn *conn, struct etna_usermem *mem);

#endif

