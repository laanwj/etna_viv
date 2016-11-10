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
/* Automatic buffer swapping */
#ifndef H_ETNA_BUFSWAP
#define H_ETNA_BUFSWAP

#include <etnaviv/etna.h>
#include <etnaviv/etna_rs.h>

#include <pthread.h>
#include <stdbool.h>

/* maximum number of buffers supported (triple) */
#define ETNA_BSWAP_NUM_BUFFERS 3

struct etna_bswap_buffer {
    pthread_mutex_t available_mutex;
    pthread_cond_t available_cond;
    bool is_available;
    int sig_id_ready;
};

typedef int (*etna_set_buffer_cb_t)(void *, int);
typedef int (*etna_copy_buffer_cb_t)(void *, struct etna_ctx *, int);

struct etna_bswap_buffers {
    struct viv_conn *conn;
    struct etna_ctx *ctx;
    pthread_t thread;
    int num_buffers;
    int backbuffer, frontbuffer;
    bool terminate;

    etna_set_buffer_cb_t set_buffer;
    etna_copy_buffer_cb_t copy_buffer;
    void *userptr;
    struct etna_bswap_buffer buf[ETNA_BSWAP_NUM_BUFFERS];
};

int etna_bswap_create(struct etna_ctx *ctx, struct etna_bswap_buffers **bufs_out, 
        int num_buffers,
        etna_set_buffer_cb_t set_buffer, 
        etna_copy_buffer_cb_t copy_buffer,
        void *userptr);

int etna_bswap_free(struct etna_bswap_buffers *bufs);

int etna_bswap_wait_available(struct etna_bswap_buffers *bufs);

int etna_bswap_queue_swap(struct etna_bswap_buffers *bufs);

/* analogous to eglSwapBuffers */
int etna_swap_buffers(struct etna_bswap_buffers *bufs);

#endif

