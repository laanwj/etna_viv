/* Watch usage of Vivante GPU live.
 * Needs profiling support built-in (build kernel and etnaviv with VIVANTE_PROFILER=1).
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
#include <sys/time.h>

#include <etnaviv/viv.h>
#include <etnaviv/viv_profile.h>

static const char *bars[] = {
	" ",
	"▏",
	"▎",
	"▍",
	"▌",
	"▋",
	"▊",
	"▉",
	"█"
};

static const char clear_screen[] = {0x1b, '[', 'H',
                                    0x1b, '[', 'J',
                                    0x0};

static const char color_num_zero[] = "\x1b[1;30m";
static const char color_num[] = "\x1b[1;33m";
static const char color_reset[] = "\x1b[0m";

#define STATS_LEN (20)
#define PERCENTAGE_BAR_END      (79 - STATS_LEN)

static void print_percentage_bar(float percent, int cur_line_len)
{
    int bar_avail_len = (PERCENTAGE_BAR_END - cur_line_len - 1) * 8;
    int bar_len = bar_avail_len * (percent + .5) / 100.0;
    int i;

    for (i = bar_len; i >= 8; i -= 8) {
        printf("%s", bars[8]);
        cur_line_len++;
    }
    if (i) {
        printf("%s", bars[i]);
        cur_line_len++;
    }

    /* NB: We can't use a field width with utf8 so we manually
    * guarantee a field with of 45 chars for any bar. */
    printf("%*s", PERCENTAGE_BAR_END - cur_line_len, "");
}

static unsigned long gettime(void)
{
    struct timeval t;
    gettimeofday(&t, NULL);
    return (t.tv_usec + (t.tv_sec * 1000000));
}

/* Return number of lines in the terminal */
static int get_screen_lines(void)
{
    struct winsize ws;
    if (ioctl(0, TIOCGWINSZ, &ws) != -1)
        return ws.ws_row;
    else
        return 25; /* default */
}

/* Format unsigned 64 bit number with thousands separators.
 * Result is always nul-terminated within outsz characters. */
static void format_number(char *out, int outsz, uint64_t num)
{
    char temp[100];
    int len;
    int groups, group_size;
    int in_ptr, out_ptr;
    if(outsz == 0)
        return;
    snprintf(temp, sizeof(temp), "%llu", num);
    len = strlen(temp);
    /* group digits per three */
    groups = (len+2) / 3;
    group_size = len - (groups - 1) * 3; /* First group */
    in_ptr = out_ptr = 0;
    outsz -= 1;
    for(int i=0; i<groups && out_ptr < outsz; ++i)
    {
        for(int j=0; j<group_size && out_ptr < outsz; ++j)
            out[out_ptr++] = temp[in_ptr++];
        if(i != (groups-1) && out_ptr < outsz)
            out[out_ptr++] = ',';
        group_size = 3;
    }
    out[out_ptr] = 0;
}

/****************************************************************************/

/* Counter sorting record */
struct counter_rec
{
    uint32_t id;
    uint64_t events_per_s;
};

/* Sort counters descending */
static int counter_rec_compar(const void *a, const void *b)
{
    uint64_t ca = ((const struct counter_rec*)a)->events_per_s;
    uint64_t cb = ((const struct counter_rec*)b)->events_per_s;
    if(ca < cb)
        return 1;
    else if(cb > ca)
        return -1;
    else return 0;
}

enum display_mode
{
    MODE_ALL = 0,
    MODE_SORTED = 1
};

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

    /* XXX parameter parsing */
    int samples_per_second = 100;
    bool interactive = true;
    int mode = MODE_ALL;
    //int mode = MODE_SORTED;
    bool color = true;

    uint32_t *counter_data = calloc(num_profile_counters, 4);
    uint32_t *counter_data_last = calloc(num_profile_counters, 4);
    uint64_t *events_per_s = calloc(num_profile_counters, 8);
    struct counter_rec *sorted = calloc(num_profile_counters, sizeof(struct counter_rec));
    /* reset counters and initial values */
    if(viv_read_profile_counters_3d(conn, counter_data_last) != 0)
    {
        fprintf(stderr, "Error querying counters (probably unsupported with this kernel, or not built into libetnaviv)\n");
        exit(1);
    }
    uint32_t begin_time = gettime();
    useconds_t interval = 1000000 / samples_per_second;
    while(true)
    {
        /* Scale counters by real elapsed time */
        for(int c=0; c<num_profile_counters; ++c)
        {
            events_per_s[c] = 0;
        }

        for(int sample=0; sample<samples_per_second; ++sample)
        {
            if(viv_read_profile_counters_3d(conn, counter_data) != 0)
            {
                fprintf(stderr, "Error querying counters (probably unsupported with this kernel, or not built into libetnaviv)\n");
                exit(1);
            }
            for(int c=0; c<num_profile_counters; ++c)
            {
                /* some counters don't reset when read */
                if(c == VIV_PROF_PS_INST_COUNTER ||
                   c == VIV_PROF_VS_INST_COUNTER ||
                   c == VIV_PROF_RENDERED_PIXEL_COUNTER ||
                   c == VIV_PROF_RENDERED_VERTICE_COUNTER ||
                   c == VIV_PROF_PXL_TEXLD_INST_COUNTER ||
                   c == VIV_PROF_PXL_BRANCH_INST_COUNTER ||
                   c == VIV_PROF_VTX_TEXLD_INST_COUNTER ||
                   c == VIV_PROF_VTX_BRANCH_INST_COUNTER ||
                   c == VIV_PROF_SE_CULLED_TRIANGLE_COUNT ||
                   c == VIV_PROF_SE_CULLED_LINES_COUNT)
                    events_per_s[c] += counter_data[c] - counter_data_last[c];
                else
                    events_per_s[c] += counter_data[c];
            }
            for(int c=0; c<num_profile_counters; ++c)
                counter_data_last[c] = counter_data[c];

            usleep(interval);
        }
        uint32_t end_time = gettime();
        uint32_t diff_time = end_time - begin_time;

        /* Scale counters by real elapsed time */
        for(int c=0; c<num_profile_counters; ++c)
        {
            events_per_s[c] = events_per_s[c] * 1000000LL / (uint64_t)diff_time;
        }

        /* Sort counters descending */
        for(int c=0; c<num_profile_counters; ++c)
        {
            sorted[c].id = c;
            sorted[c].events_per_s = events_per_s[c];
        }
        qsort(sorted, num_profile_counters, sizeof(struct counter_rec), &counter_rec_compar);

        if(interactive)
        {
            int line = 0; /* current screen line */
            printf("%s", clear_screen);
            int max_lines = get_screen_lines() - line - 1;
            if(mode == MODE_SORTED)
            {
                int count = (num_profile_counters > max_lines) ? max_lines : num_profile_counters;
                for(int c=0; c<count; ++c)
                {
                    char num[100];
                    struct viv_profile_counter_info *info = viv_get_profile_counter_info(sorted[c].id);
                    format_number(num, sizeof(num), sorted[c].events_per_s);
                    if(color)
                        printf("%s", sorted[c].events_per_s == 0 ? color_num_zero : color_num);
                    printf("%15.15s", num);
                    if(color)
                        printf("%s", color_reset);
                    printf(" ");
                    printf("%-30.30s", info->name);
                    printf("\n");
                }
            } else if(mode == MODE_ALL)
            {
                /* XXX check that width doesn't exceed screen width */
                for(int l=0; l<max_lines; ++l)
                {
                    int c = VIV_PROF_GPU_CYCLES_COUNTER + l;
                    while(c < num_profile_counters)
                    {
                        char num[100];
                        struct viv_profile_counter_info *info = viv_get_profile_counter_info(c);
                        format_number(num, sizeof(num), events_per_s[c]);
                        if(color)
                            printf("%s", events_per_s[c] == 0 ? color_num_zero : color_num);
                        printf("%15.15s", num);
                        if(color)
                            printf("%s", color_reset);
                        printf(" ");
                        printf("%-30.30s", info->name);
                        printf("  ");
                        c += max_lines;
                    }
                    printf("\n");
                }
            }
        }
        begin_time = end_time;
    }
    /*
     * XXX define new mode MODE_OCCUPANCY and some derived percentage bars:
     * - [PA] Number of primitives per vertex (max 1)
     * - [PA] % of primitives culled
     * - VS -> PA -> SE -> RA primitives/vertices in each stage
     * - RA -> PS -> PE pixels/quads in each stage
     * - Pixels per PS inst
     * - Vertices per VS inst
     * - % of texture requests trilinear/bilinear
     * - overdraw (killed by depth)
     */

    return 0;
}

