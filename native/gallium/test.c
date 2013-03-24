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

/* Beginnings of TGSI->Vivante conversion -- WIP */
/* usage: ./test tgsi_testdata/cube_companion_frag.tgsi */
#include "tgsi/tgsi_text.h"
#include "tgsi/tgsi_iterate.h"
#include "tgsi/tgsi_dump.h"
#include "tgsi/tgsi_parse.h"
#include "tgsi/tgsi_strings.h"
#include "pipe/p_shader_tokens.h"
#include "util/u_memory.h"
#include "util/u_math.h"

#include "etna.h"
#include "etna_util.h"
#include "etna_asm.h"
#include "etna_internal.h"
#include "etna_shader.h"
#include "isa.xml.h"

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

/* load a text file into memory */
static char *
load_text_file(const char *file_name)
{
    char *text = NULL;
    size_t size;
    size_t total_read = 0;
    FILE *fp = fopen(file_name, "rb");

    if (!fp) {
        return NULL;
    }

    fseek(fp, 0L, SEEK_END);
    size = ftell(fp);
    fseek(fp, 0L, SEEK_SET);

    text = (char *) malloc(size + 1);
    if (text != NULL) {
        do {
            size_t bytes = fread(text + total_read,
                         1, size - total_read, fp);
            if (bytes < size - total_read) {
                free(text);
                text = NULL;
                break;
            }

            if (bytes == 0) {
                break;
            }

            total_read += bytes;
        } while (total_read < size);

        text[total_read] = '\0';
    }

    fclose(fp);

    return text;
}

int main(int argc, char **argv)
{
    if(argc < 2)
    {
        fprintf(stderr, "Must pass shader source name as argument\n");
        exit(1);
    }
    const char *text = load_text_file(argv[1]);
    if(!text)
    {
        fprintf(stderr, "Unable to open %s\n", argv[1]);
        exit(1);
    }
    struct tgsi_token tokens[ETNA_MAX_TOKENS];
    if(tgsi_text_translate(text, tokens, Elements(tokens)))
    {
        /* tgsi_dump(tokens, 0); */
#if 0
        union {
            struct tgsi_header h;
            struct tgsi_token t;
        } hdr;

        hdr.t = tokens[0];
        printf("Header size: %i\n", hdr.h.HeaderSize);
        printf("Body size: %i\n", hdr.h.BodySize);
        int totalSize = hdr.h.HeaderSize + hdr.h.BodySize;

        for(int i=0; i<totalSize; ++i)
        {
            printf("%08x ", *((uint32_t*)&tokens[i]));
        }
        printf("\n");
#endif
        struct etna_shader_object *sobj = NULL;
        struct etna_pipe_specs specs = {
            .vertex_sampler_offset = 8,
            .vs_need_z_div = true
        };
        etna_compile_shader_object(&specs, tokens, &sobj);

        etna_dump_shader_object(sobj);

        int fd = creat("shader.bin", 0777);
        write(fd, sobj->code, sobj->code_size*4);
        close(fd);

        etna_destroy_shader_object(sobj);

    } else {
        fprintf(stderr, "Unable to parse %s\n", argv[1]);
    }

    return 0;
}

