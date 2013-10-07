/* Get info about vivante device */
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
#include <etnaviv/viv_profile.h>

int main()
{
    struct viv_conn *conn = 0;
    int rv;
    rv = viv_open(VIV_HW_3D, &conn);
    if(rv!=0)
    {
        fprintf(stderr, "Error opening device\n");
        exit(1);
    }
    uint32_t num_profile_counters = viv_get_num_profile_counters();

    uint32_t *counter_data = calloc(num_profile_counters, 4);
    if(viv_read_profile_counters_3d(conn, counter_data) != 0)
    {
        fprintf(stderr, "Error querying counters (probably unsupported with this kernel, or not built into libetnaviv)\n");
        exit(1);
    }
    for(uint32_t c=0; c<num_profile_counters; ++c)
    {
        struct viv_profile_counter_info *info = viv_get_profile_counter_info(c);
        printf("[%d] %s %u\n", c, info->name, counter_data[c]);
    }

    return 0;
}

