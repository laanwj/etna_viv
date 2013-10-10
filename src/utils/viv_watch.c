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
/* Watch GPU debug registers */
/* Important: Needs kernel module compiled with user space register access
 * (gcdREGISTER_ACCESS_FROM_USER=1) */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>

#include <etnaviv/viv.h>
#include "gc_abi.h"

struct viv_conn *conn = 0;

static const char clear_screen[] = {0x1b, '[', 'H',
                                    0x1b, '[', 'J',
                                    0x0};
static const char color_num_zero[] = "\x1b[1;30m";
static const char color_num[] = "\x1b[1;33m";
static const char color_head[] = "\x1b[1;37;100m";
static const char color_reset[] = "\x1b[0m";

static void write_register(uint32_t address, uint32_t data)
{
    gcsHAL_INTERFACE id;
    id.command = gcvHAL_WRITE_REGISTER;
    id.u.WriteRegisterData.address = address;
    id.u.WriteRegisterData.data = data;
    if(viv_invoke(conn, &id) != VIV_STATUS_OK)
    {
        perror("Ioctl error");
        exit(1);
    }
}

static uint32_t read_register(uint32_t address)
{
    gcsHAL_INTERFACE id;
    id.command = gcvHAL_READ_REGISTER;
    id.u.ReadRegisterData.address = address;
    if(viv_invoke(conn, &id) != VIV_STATUS_OK)
    {
        perror("Ioctl error");
        exit(1);
    }
    return id.u.ReadRegisterData.data;
}

struct debug_register
{
    const char *module;
    uint32_t select_reg;
    uint32_t select_shift;
    uint32_t read_reg;
    uint32_t count;
    uint32_t signature;
};

/* XXX possible to select/clear four debug registers at a time? this would
 * avoid writes.
 */
static struct debug_register debug_registers[] =
{
    { "RA", 0x474, 16, 0x448, 16, 0x12344321 },
    { "TX", 0x474, 24, 0x44C, 16, 0x12211221 },
    { "FE", 0x470,  0, 0x450, 16, 0xBABEF00D },
    { "PE", 0x470, 16, 0x454, 16, 0xBABEF00D },
    { "DE", 0x470,  8, 0x458, 16, 0xBABEF00D },
    { "SH", 0x470, 24, 0x45C, 16, 0xDEADBEEF },
    { "PA", 0x474,  0, 0x460, 16, 0x0000AAAA },
    { "SE", 0x474,  8, 0x464, 16, 0x5E5E5E5E },
    { "MC", 0x478,  0, 0x468, 16, 0x12345678 },
    { "HI", 0x478,  8, 0x46C, 16, 0xAAAAAAAA }
};
#define NUM_MODULES (sizeof(debug_registers) / sizeof(struct debug_register))
#define MAX_COUNT 16

int main()
{
    int rv = 0;
    rv = viv_open(VIV_HW_3D, &conn);
    if(rv!=0)
    {
        fprintf(stderr, "Error opening device\n");
        exit(1);
    }

    uint32_t counters[NUM_MODULES][MAX_COUNT] = {{}};
    uint32_t counters_prev[NUM_MODULES][MAX_COUNT] = {{}};
    int interval = 1000000;
    int reset = 0; /* reset counters after read */

    int has_prev = 0;
    while(true)
    {
        printf("%s", clear_screen);
        for(unsigned int rid=0; rid<NUM_MODULES; ++rid)
        {
            struct debug_register *rdesc = &debug_registers[rid];
            for(unsigned int sid=0; sid<15; ++sid)
            {
                write_register(rdesc->select_reg, sid << rdesc->select_shift);
                counters[rid][sid] = read_register(rdesc->read_reg);
            }
            if(reset)
            {
                write_register(rdesc->select_reg, 15 << rdesc->select_shift);
                counters[rid][15] = read_register(rdesc->read_reg);
            }
            write_register(debug_registers[rid].select_reg, 0 << rdesc->select_shift);
        }

        printf("%s  ", color_head);
        for(unsigned int rid=0; rid<NUM_MODULES; ++rid)
        {
            printf("   %-2s    ", debug_registers[rid].module);
        }
        printf("%s\n",color_reset);
        for(unsigned int sid=0; sid<MAX_COUNT ; ++sid)
        {
            printf("%s%01x%s ", color_head, sid, color_reset);
            for(unsigned int rid=0; rid<NUM_MODULES; ++rid)
            {
                const char *color = "";
                if(has_prev && counters[rid][sid] != counters_prev[rid][sid])
                    color = color_num;
                printf("%s%08x%s ", color, counters[rid][sid], color_reset);
            }
            printf("\n");
        }
        usleep(interval);

        for(unsigned int rid=0; rid<NUM_MODULES; ++rid)
            for(unsigned int sid=0; sid<MAX_COUNT; ++sid)
                counters_prev[rid][sid] = counters[rid][sid];
        has_prev = 1;
    }

    viv_close(conn);
    return 0;
}

