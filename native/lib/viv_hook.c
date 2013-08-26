/*
 * Copyright (c) 2012-2013 Wladimir J. van der Laan
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
#include "viv_hook.h"
#include "elf_hook.h"
#include "flightrecorder.h"
/* hooking / logging functionality for Vivante GL driver
 */
#include "gc_abi.h"
#include "gc_hal_base.h"
#include "gc_hal.h"
#include "gc_hal_driver.h"

#ifdef GCABI_HAS_STATE_DELTAS
#include "gc_hal_kernel_buffer.h"
#else /* V2 */
#include "gc_hal_user_context.h"
#endif

#include "gc_hal_types.h"

#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <pthread.h>

flightrec_t _fdr;

static int _galcore_handle = 0;

/* keep track of mapped video memory (not mapped through mmap) */
#define MAX_MAPPINGS 128
typedef struct
{
    void *node;
    void *logical;
    size_t bytes;
} mapping_t;
static mapping_t mappings[MAX_MAPPINGS];

int my_open(const char* path, int flags, ...)
{
    int ret=0;
    if (flags & O_CREAT) 
    {
        int mode=0;
        va_list  args;

        va_start(args, flags);
        mode = (mode_t) va_arg(args, int);
        va_end(args);

        ret = open(path, flags, mode);
    } else {
        ret = open(path, flags);
    }
    
    if(ret >= 0 && (!strcmp(path, "/dev/galcore") || !strcmp(path, "/dev/graphics/galcore")))
    {
        _galcore_handle = ret;
        printf("opened galcore: %i\n", ret);
    }

    return ret;
}

void *my_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    if(_fdr != NULL && fd == _galcore_handle)
    {
        flightrec_event_t evctx = fdr_new_event(_fdr, "MMAP_BEFORE");
        fdr_event_add_parameter(evctx, "addr",(size_t)addr);
        fdr_event_add_parameter(evctx, "length",(size_t)length);
        fdr_event_add_parameter(evctx, "prot",(size_t)prot);
        fdr_event_add_parameter(evctx, "flags",(size_t)flags);
        fdr_event_add_parameter(evctx, "fd",(size_t)fd);
        fdr_event_add_parameter(evctx, "offset",(size_t)offset);
        fdr_event_add_parameter(evctx, "thread", (size_t)pthread_self());
        fdr_log_event(_fdr, evctx);
    }  
    void *ret = mmap(addr, length, prot, flags, fd, offset);
    if(_fdr != NULL && fd == _galcore_handle)
    {
        printf("new mapping %p %d\n", ret, (int)length);
        flightrec_event_t evctx = fdr_new_event(_fdr, "MMAP_AFTER");
        fdr_event_add_parameter(evctx, "addr",(size_t)addr);
        fdr_event_add_parameter(evctx, "length",(size_t)length);
        fdr_event_add_parameter(evctx, "prot",(size_t)prot);
        fdr_event_add_parameter(evctx, "flags",(size_t)flags);
        fdr_event_add_parameter(evctx, "fd",(size_t)fd);
        fdr_event_add_parameter(evctx, "offset",(size_t)offset);
        fdr_event_add_parameter(evctx, "thread", (size_t)pthread_self());
        fdr_event_add_parameter(evctx, "ret", (size_t)ret);
        if(ret)
        {
            /* monitor new mmapped range */
            /* Only small ranges, tracking the 128MB range that vivante maps is waaay
               too slow (and in the current way things are done, needs all memory that 
               the device has to detect changes) */
            /*
                fdr_add_monitored_range(_fdr, ret, length);
            */
        }
        fdr_log_event(_fdr, evctx);
    }
    return ret;
}

int my_munmap(void *addr, size_t length)
{
    if(_fdr != NULL)
    {
        flightrec_event_t evctx = fdr_new_event(_fdr, "MUNMAP_BEFORE");
        fdr_event_add_parameter(evctx, "addr",(size_t)addr);
        fdr_event_add_parameter(evctx, "length",(size_t)length);
        fdr_event_add_parameter(evctx, "thread", (size_t)pthread_self());
        fdr_log_event(_fdr, evctx);
    }

    printf("removed mapping %p %d\n", addr, (int)length);
    //fdr_remove_monitored_range(_fdr, addr, length);
    int ret = munmap(addr, length);
    
    if(_fdr != NULL)
    {
        flightrec_event_t evctx = fdr_new_event(_fdr, "MUNMAP_AFTER");
        fdr_event_add_parameter(evctx, "addr",(size_t)addr);
        fdr_event_add_parameter(evctx, "length",(size_t)length);
        fdr_event_add_parameter(evctx, "thread", (size_t)pthread_self());
        fdr_event_add_parameter(evctx, "ret", (size_t)ret);
        fdr_log_event(_fdr, evctx);
    }
    return ret;
}

/* Log contents of HAL_INTERFACE (input) child pointers.
 * Assumes that the parent structure id was already added. */
static void log_interface_in(flightrec_event_t evctx, gcsHAL_INTERFACE *id)
{
    switch(id->command)
    {
    case gcvHAL_COMMIT:
        fdr_event_add_oneshot_range(evctx, id->u.Commit.commandBuffer, sizeof(struct _gcoCMDBUF));
        //fdr_event_add_oneshot_range(evctx, id->u.Commit.commandBuffer->logical, id->u.Commit.commandBuffer->offset);
#ifndef GCABI_HAS_STATE_DELTAS
        fdr_event_add_oneshot_range(evctx, id->u.Commit.contextBuffer, sizeof(struct _gcoCONTEXT));
        if(id->u.Commit.contextBuffer->map) /* state map */
            fdr_event_add_oneshot_range(evctx, id->u.Commit.contextBuffer->map, id->u.Commit.contextBuffer->stateCount*4);
        if(id->u.Commit.contextBuffer->buffer) /* context command temp buffer */
            fdr_event_add_oneshot_range(evctx, id->u.Commit.contextBuffer->buffer, id->u.Commit.contextBuffer->bufferSize);
#endif
        break;
    case gcvHAL_EVENT_COMMIT: { /* log entire event chain */
            struct _gcsQUEUE *queue = id->u.Event.queue;
            while(queue != NULL)
            {
                fdr_event_add_oneshot_range(evctx, queue, sizeof(struct _gcsQUEUE));
                log_interface_in(evctx, &queue->iface);
                queue = queue->next;
            }
        }
    case gcvHAL_FREE_VIDEO_MEMORY:
        for(int idx=0; idx<MAX_MAPPINGS; ++idx)
        {
            if(mappings[idx].node == id->u.FreeVideoMemory.node)
            {
                mappings[idx].node = 0;
                mappings[idx].bytes = 0;
                mappings[idx].logical = 0;
                break;
            }
        }
        break;
    case gcvHAL_UNLOCK_VIDEO_MEMORY:
        for(int idx=0; idx<MAX_MAPPINGS; ++idx)
        {
            if(mappings[idx].node == id->u.UnlockVideoMemory.node)
            {
                printf("remove_range %p %08x\n", mappings[idx].logical, mappings[idx].bytes);
                fdr_remove_monitored_range(_fdr, mappings[idx].logical, mappings[idx].bytes);
                mappings[idx].logical = 0;
                break;
            }
        }
        break;
    case gcvHAL_FREE_CONTIGUOUS_MEMORY:
        fdr_remove_monitored_range(_fdr, id->u.FreeContiguousMemory.logical, id->u.FreeContiguousMemory.bytes);
        break;
    default:
        break;
    }
}

/* Log contents of HAL_INTERFACE (output) child pointers.
 * Assumes that the parent structure id was already added. */
static void log_interface_out(flightrec_event_t evctx, gcsHAL_INTERFACE *id)
{
    switch(id->command)
    {
    case gcvHAL_ALLOCATE_LINEAR_VIDEO_MEMORY:
        printf("vidalloc %p %08x\n", id->u.AllocateLinearVideoMemory.node, id->u.AllocateLinearVideoMemory.bytes);
        for(int idx=0; idx<MAX_MAPPINGS; ++idx)
        {
            if(mappings[idx].node == NULL)
            {
                mappings[idx].node = id->u.AllocateLinearVideoMemory.node;
                mappings[idx].bytes = id->u.AllocateLinearVideoMemory.bytes;
                mappings[idx].logical = NULL;
                break;
            }
        }
        break;
    case gcvHAL_LOCK_VIDEO_MEMORY:
        for(int idx=0; idx<MAX_MAPPINGS; ++idx)
        {
            if(mappings[idx].node == id->u.LockVideoMemory.node)
            {
                mappings[idx].logical = id->u.LockVideoMemory.memory;
                printf("add_range %p %08x\n", mappings[idx].logical, mappings[idx].bytes);
                fdr_add_monitored_range(_fdr, mappings[idx].logical, mappings[idx].bytes);
                break;
            }
        }
        break;
    case gcvHAL_ALLOCATE_CONTIGUOUS_MEMORY:
        fdr_add_monitored_range(_fdr, id->u.AllocateContiguousMemory.logical, id->u.AllocateContiguousMemory.bytes);
        break;
    default:
        break;
    }
}

int my_ioctl(int d, int request, void *ptr_)
{
    vivante_ioctl_data_t *ptr = (vivante_ioctl_data_t*) ptr_;
#if 0 /* UGH, this handle gets passed in some other way instead of open() for i.mx6 blobster */
    if(d != _galcore_handle)
    {
        printf("unhandled ioctl %08x on fd %i\n", request, d);
        return -1;
    }
#endif
    int ret=0;
    if(_fdr != NULL)
    {
        if(request == IOCTL_GCHAL_INTERFACE)
        {
            flightrec_event_t evctx = fdr_new_event(_fdr, "IOCTL_BEFORE");
            fdr_event_add_parameter(evctx, "d", (size_t)d);
            fdr_event_add_parameter(evctx, "request",(size_t)request);
            fdr_event_add_parameter(evctx, "ptr",(size_t)ptr);
            fdr_event_add_parameter(evctx, "thread", (size_t)pthread_self());
            fdr_event_add_oneshot_range(evctx, ptr, sizeof(vivante_ioctl_data_t));
            fdr_event_add_oneshot_range(evctx, ptr->in_buf, ptr->in_buf_size);
            log_interface_in(evctx, ptr->in_buf);
            fdr_log_event(_fdr, evctx);
        } else {
            printf("unhandled ioctl %08x on fd %i\n", request, d);
            return -1;
        }
    }
    ret = ioctl(d, request, ptr);
    if(_fdr != NULL)
    {
        if(request == IOCTL_GCHAL_INTERFACE)
        {
            flightrec_event_t evctx = fdr_new_event(_fdr, "IOCTL_AFTER");
            fdr_event_add_parameter(evctx, "d", (size_t)d);
            fdr_event_add_parameter(evctx, "request",(size_t)request);
            fdr_event_add_parameter(evctx, "ptr",(size_t)ptr);
            fdr_event_add_parameter(evctx, "thread", (size_t)pthread_self());
            fdr_event_add_parameter(evctx, "ret", (size_t)ret);
            fdr_event_add_oneshot_range(evctx, ptr, sizeof(vivante_ioctl_data_t));
            fdr_event_add_oneshot_range(evctx, ptr->out_buf, ptr->out_buf_size);
            log_interface_out(evctx, ptr->out_buf);
            fdr_log_event(_fdr, evctx);
        }
    }
    return ret;
}

void hook_start_logging(const char *filename)
{
    if(_fdr == NULL)
    {
        printf("viv_hook: logging to %s\n", filename);
        _fdr = fdr_open(filename);
    }
}

void the_hook(const char *filename)
{
    char *mali_path = NULL; // path to libMali.so
    void *mali_base = NULL;
    char *gal_names[] = {"libGAL.so", "libGAL-fb.so", 0};

    hook_start_logging(filename);

    int i=0;
    while (gal_names[i] != 0)
    {
        if(parse_maps(gal_names[i], &mali_path, &mali_base) == 0)
            break;
        i++;
    }
    
    if (gal_names[i] == 0)
    {
        fprintf(stderr, "Could not find libGAL library in process map\n");
        return;
    }
    
    printf("Hooking %s at %p\n", mali_path, mali_base);
    if(elf_hook(mali_path, mali_base, "open", &my_open) == NULL)
    {
        fprintf(stderr, "Hooking open failed\n");
        return;
    }
    if(elf_hook(mali_path, mali_base, "ioctl", &my_ioctl) == NULL)
    {
        fprintf(stderr, "Hooking ioctl failed\n");
        return;
    }
    if(elf_hook(mali_path, mali_base, "mmap", &my_mmap) == NULL)
    {
        fprintf(stderr, "Hooking mmap failed\n");
        return;
    }
    if(elf_hook(mali_path, mali_base, "munmap", &my_munmap) == NULL)
    {
        fprintf(stderr, "Hooking munmap failed\n");
        return;
    }
    printf("Hook succeeded!\n"); 
}

void close_hook()
{
    fdr_close(_fdr);
    _fdr = 0;
}

