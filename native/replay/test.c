#include <stdio.h>
#include <stdint.h>
#include "context_cmd.h"
int main()
{
    printf("%08x\n", contextbuf[contextbuf_addr[0].index]);
    printf("%08x\n", sizeof(contextbuf));
}
