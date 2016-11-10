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
/* Dump all reachable GPU registers */
/*
 * Important: Needs kernel module compiled with user space register access
 * (gcdREGISTER_ACCESS_FROM_USER=1)
 */
/*
 * Warning: this utility can result in crashes inside the kernel such as (on ARM),
 *
 *     Unhandled fault: external abort on non-linefetch (0x1028) at 0xfe641000
 *     Internal error: : 1028 [#1] PREEMPT ARM
 *
 * It looks as if the actually accessible registers differ per SoC.
 * When a non-accessible register is loaded, a fault happens. So expect crashes
 * when using this utility.
 */
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

int main()
{
    struct viv_conn *conn = 0;
    int rv = 0;
    rv = viv_open(VIV_HW_3D, &conn);
    if(rv!=0)
    {
        fprintf(stderr, "Error opening device\n");
        exit(1);
    }

    gcsHAL_INTERFACE id = {};
    memset((void*)&id, 0, sizeof(id));

    // 0x000..0x200 ok
    // 0x200..0x400 crash
    // 0x400..0x800 ok
    // 0x800..0xa00 crash
    // 0xa00..0xc00 crash
    // 0xc00..0xe00 crash
    // 0xe00..0x1000 crash
    // everything above that I've tested: crash
    for(int address=0x000; address<0x800; address+=4)
    {
        if(address >= 0x200 && address < 0x400)
            continue; /* reading causes CPU(!) crash */
        id.command = gcvHAL_READ_REGISTER;
        id.u.ReadRegisterData.address = address;
        if(viv_invoke(conn, &id) != VIV_STATUS_OK)
        {
            perror("Ioctl error");
            exit(1);
        }
        printf("%05x %08x\n", address, id.u.ReadRegisterData.data);
        fflush(stdout);
    }

    viv_close(conn);
    return 0;
}

