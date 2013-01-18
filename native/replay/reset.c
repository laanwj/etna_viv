/* Reset GPU, useful in case it hangs */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdarg.h>

#include "viv.h"

int main(int argc, char **argv)
{
    int rv;
    rv = viv_open();
    if(rv!=0)
    {
        fprintf(stderr, "Error opening device\n");
        exit(1);
    }
    printf("Succesfully opened device, resetting\n");
    viv_reset();
    viv_close();
    return 0;
}

