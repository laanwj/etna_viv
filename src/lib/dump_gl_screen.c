#include "dump_gl_screen.h"
#include "write_bmp.h"

#include <stdlib.h>

void dump_gl_screen(const char *filename, GLsizei width, GLsizei height)
{
    void *data;

    if(width==0 || height==0)
        return;

    data = malloc(width * height * 4);
    if(!data)
        return;
    
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, data);

    bmp_dump32((char*)data, width, height, false, filename);
    free(data);
}

