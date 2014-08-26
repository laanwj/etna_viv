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
#ifndef H_ETNA_BO
#define H_ETNA_BO

#include <stdbool.h>
#include <unistd.h>
#include <stdint.h>

/* bo create flags */
#define DRM_ETNA_GEM_TYPE_CMD        0x00000000 /* Command buffer */
#define DRM_ETNA_GEM_TYPE_IDX        0x00000001 /* Index buffer */
#define DRM_ETNA_GEM_TYPE_VTX        0x00000002 /* Vertex buffer */
#define DRM_ETNA_GEM_TYPE_TEX        0x00000003 /* Texture */
#define DRM_ETNA_GEM_TYPE_RT         0x00000004 /* Color render target */
#define DRM_ETNA_GEM_TYPE_ZS         0x00000005 /* Depth stencil target */
#define DRM_ETNA_GEM_TYPE_HZ         0x00000006 /* Hierarchical depth render target */
#define DRM_ETNA_GEM_TYPE_BMP        0x00000007 /* Bitmap */
#define DRM_ETNA_GEM_TYPE_TS         0x00000008 /* Tile status cache */
#define DRM_ETNA_GEM_TYPE_MASK       0x0000000F

#define DRM_ETNA_GEM_CACHE_NONE      0x00000000
#define DRM_ETNA_GEM_CACHE_WCOMBINE  0x00100000
#define DRM_ETNA_GEM_CACHE_WTHROUGH  0x00200000
#define DRM_ETNA_GEM_CACHE_WBACK     0x00400000
#define DRM_ETNA_GEM_CACHE_WBACKWA   0x00800000
#define DRM_ETNA_GEM_CACHE_MASK      0x00f00000

#define DRM_ETNA_GEM_GPUREADONLY     0x01000000

/* bo access flags */
#define DRM_ETNA_PREP_READ           0x01
#define DRM_ETNA_PREP_WRITE          0x02
#define DRM_ETNA_PREP_NOSYNC         0x04

struct viv_conn;
struct etna_queue;
struct etna_ctx;
struct etna_bo;

/* Allocate linear block of video memory */
struct etna_bo *etna_bo_new(struct viv_conn *conn, size_t bytes, uint32_t flags);

/* Map user memory (which may be write protected) into GPU memory space */
struct etna_bo *etna_bo_from_usermem_prot(struct viv_conn *conn, void *memory, size_t size, int prot);

/* Map user memory into GPU memory space */
struct etna_bo *etna_bo_from_usermem(struct viv_conn *conn, void *memory, size_t size);

/* Buffer object from framebuffer range */
struct etna_bo *etna_bo_from_fbdev(struct viv_conn *conn, int fd, size_t offset, size_t size);

/* Buffer object from flink name */
struct etna_bo *etna_bo_from_name(struct viv_conn *conn, uint32_t name);

/* Buffer object from dmabuf fd */
struct etna_bo *etna_bo_from_dmabuf(struct viv_conn *conn, int fd, int prot);

/* Increase reference count */
struct etna_bo *etna_bo_ref(struct etna_bo *bo);

/* Decrease reference count orfree video memory node */
int etna_bo_del(struct viv_conn *conn, struct etna_bo *mem, struct etna_queue *queue);

/* Return flink name of buffer object */
int etna_bo_get_name(struct etna_bo *bo, uint32_t *name);

/* Return handle of buffer object */
uint32_t etna_bo_handle(struct etna_bo *bo);

/* Return size of buffer object */
uint32_t etna_bo_size(struct etna_bo *bo);

/* Map buffer object into CPU memory and return pointer. If the buffer object
 * is already mapped, return the existing mapping. */
void *etna_bo_map(struct etna_bo *bo);

/* Prepare for CPU access to buffer object */
int etna_bo_cpu_prep(struct etna_bo *bo, struct etna_ctx *pipe, uint32_t op);

/* Finish CPU access to buffer object */
void etna_bo_cpu_fini(struct etna_bo *bo);

/* Temporary: get GPU address of buffer */
uint32_t etna_bo_gpu_address(struct etna_bo *bo);

#endif

