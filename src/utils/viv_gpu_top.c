/* Watch usage of Vivante GPU live.
 * Needs profiling support built-in (build kernel and etnaviv with VIVANTE_PROFILER=1).
 */

/* Uncomment if the platform has the clock_gettime call, to use a monotonic
 * clock */
/* #define HAVE_CLOCK */

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
#include <getopt.h>
#ifdef HAVE_CLOCK
#include <time.h>
#else
#include <sys/time.h>
#endif

#include <etnaviv/viv.h>
#include <etnaviv/viv_profile.h>
#include <etnaviv/state_hi.xml.h>
#include <etnaviv/state.xml.h>

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

static const char color_num_max[] = "\x1b[1;37m";
static const char color_num_zero[] = "\x1b[1;30m";
static const char color_num[] = "\x1b[1;33m";
static const char color_reset[] = "\x1b[0m";
static const char color_percentage_1[] = "\x1b[38;5;154;48;5;236m";
static const char color_percentage_2[] = "\x1b[38;5;112;48;5;236m";
static const char color_title[] = "\x1b[38;5;249m";

static void print_percentage_bar(float percent, int bar_width)
{
    int bar_avail_len = bar_width * 8;
    int bar_len = bar_avail_len * (percent + .5) / 100.0;
    int cur_line_len = 0;
    int i;
    if(bar_len > bar_avail_len)
        bar_len = bar_avail_len;

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

    printf("%*s", bar_width - cur_line_len, "");
}

/* Get time in microseconds */
static unsigned long gettime(void)
{
#ifdef HAVE_CLOCK
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (t.tv_nsec/1000 + (t.tv_sec * 1000000));
#else
    struct timeval t;
    gettimeofday(&t, NULL);
    return (t.tv_usec + (t.tv_sec * 1000000));
#endif
}

/* Return number of lines and columns in the terminal */
static void get_screen_size(int *lines, int *cols)
{
    struct winsize ws;
    if (ioctl(0, TIOCGWINSZ, &ws) != -1)
    {
        *lines = ws.ws_row;
        *cols = ws.ws_col;
    } else {
        *lines = 25; /* default */
        *cols = 80;
    }
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

enum display_mode
{
    MODE_PERF,
    MODE_MAX,
    MODE_OCCUPANCY,
    MODE_DMA
};

/* derived counters (derived information computed from existing counters) */
#define NUM_DERIVED_COUNTERS (1)

static uint32_t derived_counters_base;

static struct viv_profile_counter_info derived_counter_info[] = {
    [0] = {"TOTAL_INST_COUNTER", "Total inst counter"},
};

static struct {
    const char *name;
    uint32_t bit;
    bool inv; /* show inverted value */
} idle_module_names[] = {
    {"FE", VIVS_HI_IDLE_STATE_FE, true},
    {"DE", VIVS_HI_IDLE_STATE_DE, true},
    {"PE", VIVS_HI_IDLE_STATE_PE, true},
    {"SH", VIVS_HI_IDLE_STATE_SH, true},
    {"PA", VIVS_HI_IDLE_STATE_PA, true},
    {"SE", VIVS_HI_IDLE_STATE_SE, true},
    {"RA", VIVS_HI_IDLE_STATE_RA, true},
    {"TX", VIVS_HI_IDLE_STATE_TX, true},
    {"VG", VIVS_HI_IDLE_STATE_VG, true},
    {"IM", VIVS_HI_IDLE_STATE_IM, true},
    {"FP", VIVS_HI_IDLE_STATE_FP, true},
    {"TS", VIVS_HI_IDLE_STATE_TS, true},
    {"AXI_LP", VIVS_HI_IDLE_STATE_AXI_LP, false}
};
#define NUM_IDLE_MODULES (sizeof(idle_module_names)/sizeof(idle_module_names[0]))

static const char* cmd_state_names[]={
"IDLE", "DEC", "ADR0", "LOAD0", "ADR1", "LOAD1", "3DADR", "3DCMD",
"3DCNTL", "3DIDXCNTL", "INITREQDMA", "DRAWIDX", "DRAW", "2DRECT0", "2DRECT1", "2DDATA0",
"2DDATA1", "WAITFIFO", "WAIT", "LINK", "END", "STALL", "UNKNOWN"
};
#define NUM_CMD_STATE_NAMES (sizeof(cmd_state_names)/sizeof(cmd_state_names[0]))

static const char* cmd_dma_state_names[]={
    "IDLE", "START", "REQ", "END"
};
#define NUM_CMD_DMA_STATE_NAMES (sizeof(cmd_dma_state_names)/sizeof(cmd_dma_state_names[0]))

static const char* cmd_fetch_state_names[]={
    "IDLE", "RAMVALID", "VALID", "UNKNOWN"
};
#define NUM_CMD_FETCH_STATE_NAMES (sizeof(cmd_fetch_state_names)/sizeof(cmd_fetch_state_names[0]))

static const char* req_dma_state_names[]={
    "IDLE", "START", "REQ", "END"
};
#define NUM_REQ_DMA_STATE_NAMES (sizeof(req_dma_state_names)/sizeof(req_dma_state_names[0]))

static const char* cal_state_names[]={
    "IDLE", "LDADR", "IDXCALC", "UNKNOWN"
};
#define NUM_CAL_STATE_NAMES (sizeof(cal_state_names)/sizeof(cal_state_names[0]))

static const char* ve_req_state_names[]={
    "IDLE", "CKCACHE", "MISS", "UNKNOWN"
};
#define NUM_VE_REQ_STATE_NAMES (sizeof(ve_req_state_names)/sizeof(ve_req_state_names[0]))

static struct viv_profile_counter_info *get_counter_info(uint32_t idx)
{
    if(idx < derived_counters_base)
        return viv_get_profile_counter_info(idx);
    else
        return &derived_counter_info[idx - derived_counters_base];
}

static void print_percentage_row(int l, double percent, const char *name, int name_width, bool color, int bar_width)
{
    printf("%-*s ", name_width, name);
    if(color)
    {
        if(percent > 99.0)
            printf("%s", color_num_max);
        else if(percent < 1.0)
            printf("%s", color_num_zero);
        else
            printf("%s", color_num);
    }
    printf("%5.1f%%  ", percent);
    if(color)
        printf("%s%s", color_reset, (l%2) ? color_percentage_1 : color_percentage_2);
    print_percentage_bar(percent, bar_width);
    if(color)
        printf("%s", color_reset);
}

int main(int argc, char **argv)
{
    int samples_per_second = 100;
    bool interactive = true;
    int mode = MODE_PERF;
    bool color = true;
    int opt;
    bool error = false;

    while ((opt = getopt(argc, argv, "m:s:n")) != -1) {
        switch(opt)
        {
        case 'm':
            switch(optarg[0])
            {
            case 'm': mode = MODE_MAX; break;
            case 'p': mode = MODE_PERF; break;
            case 'o': mode = MODE_OCCUPANCY; break;
            case 'd': mode = MODE_DMA; break;
            default:
                printf("Unknown mode %s\n", optarg);
            }
            break;
        case 'n': color = false; break;
        case 's': samples_per_second = atoi(optarg); break;
        case 'h':
        default:
            error = true;
        }
    }
    if(error)
    {
        printf("Usage:\n");
        printf("  %s [-m <m|p|o|d>] [-n] [-s <samples_per_second>] \n", argv[0]);
        printf("\n");
        printf("  -m <mode>     Set mode:\n");
        printf("                  p   Show performance counters (default)\n");
        printf("                  m   Show performance counter maximum\n");
        printf("                  o   Show occupancy (non-idle) states of modules\n");
        printf("                  d   Show DMA engine states\n");
        printf("  -n            Disable color\n");
        printf("  -h            Show this help message\n");
        printf("  -s <samples>  Number of samples per second (default 100)\n");
        exit(1);
    }
    struct viv_conn *conn = 0;
    int rv;
    rv = viv_open(VIV_HW_3D, &conn);
    if(rv!=0)
    {
        fprintf(stderr, "Error opening device\n");
        exit(1);
    }
    uint32_t orig_num_profile_counters = viv_get_num_profile_counters();
    derived_counters_base = orig_num_profile_counters;
    uint32_t num_profile_counters = derived_counters_base + NUM_DERIVED_COUNTERS;


    bool *reset_after_read = calloc(num_profile_counters, sizeof(bool));
    uint32_t *counter_data = calloc(num_profile_counters, 4);
    uint32_t *counter_data_last = calloc(num_profile_counters, 4);
    uint64_t *events_per_s = calloc(num_profile_counters, 8);
    uint64_t *events_per_s_max = calloc(num_profile_counters, 8);
    /* reset counters and initial values */
    if(viv_read_profile_counters_3d(conn, counter_data_last) != 0)
    {
        fprintf(stderr, "Error querying counters (probably unsupported with this kernel, or not built into libetnaviv)\n");
        exit(1);
    }
    viv_get_counters_reset_after_read(conn, reset_after_read);

    uint32_t begin_time = gettime();
    useconds_t interval = 1000000 / samples_per_second;
    while(true)
    {
        /* Scale counters by real elapsed time */
        for(int c=0; c<num_profile_counters; ++c)
        {
            events_per_s[c] = 0;
        }

        uint32_t idle_states[NUM_IDLE_MODULES] = {};
        uint32_t cmd_state[32] = {};
        uint32_t cmd_dma_state[4] = {};
        uint32_t cmd_fetch_state[4] = {};
        uint32_t req_dma_state[4] = {};
        uint32_t cal_state[4] = {};
        uint32_t ve_req_state[4] = {};
        for(int sample=0; sample<samples_per_second; ++sample)
        {
            if(mode == MODE_OCCUPANCY)
            {
                uint32_t data = 0;
                viv_read_register(conn, VIVS_HI_IDLE_STATE, &data);
                for(int mid=0; mid<NUM_IDLE_MODULES; ++mid)
                {
                    if(data & idle_module_names[mid].bit)
                        idle_states[mid]++;
                }
            } else if(mode == MODE_DMA)
            {
                uint32_t data = 0;
                viv_read_register(conn, VIVS_FE_DMA_DEBUG_STATE, &data);
                int cmd_state_idx = data & 0x1F;
                if(cmd_state_idx >= (NUM_CMD_STATE_NAMES-1)) /* Mark unknowns as UNKNOWN */
                    cmd_state_idx = NUM_CMD_STATE_NAMES-1;
                cmd_state[cmd_state_idx]++;
                cmd_dma_state[(data>>8) & 3]++;
                cmd_fetch_state[(data>>10) & 3]++;
                req_dma_state[(data>>12) & 3]++;
                cal_state[(data>>14) & 3]++;
                ve_req_state[(data>>16) & 3]++;
            } else {
                if(viv_read_profile_counters_3d(conn, counter_data) != 0)
                {
                    fprintf(stderr, "Error querying counters (probably unsupported with this kernel, or not built into libetnaviv)\n");
                    exit(1);
                }
                for(int c=0; c<num_profile_counters; ++c)
                {
                    if(!reset_after_read[c])
                    {
                        if(counter_data_last[c] > counter_data[c])
                        {
                            events_per_s[c] += counter_data[c];
                        } else {
                            events_per_s[c] += (uint32_t)(counter_data[c] - counter_data_last[c]);
                        }
                    } else
                        events_per_s[c] += counter_data[c];
                }
                for(int c=0; c<num_profile_counters; ++c)
                    counter_data_last[c] = counter_data[c];
            }

            usleep(interval);
        }
        uint32_t end_time = gettime();
        uint32_t diff_time = end_time - begin_time;

        /* Scale counters by real elapsed time */
        for(int c=0; c<num_profile_counters; ++c)
        {
            events_per_s[c] = events_per_s[c] * 1000000LL / (uint64_t)diff_time;
        }

        events_per_s[derived_counters_base + 0] = events_per_s[VIV_PROF_VS_INST_COUNTER] + events_per_s[VIV_PROF_PS_INST_COUNTER];

        /* Compute maxima */
        for(int c=0; c<num_profile_counters; ++c)
        {
            if(events_per_s[c] > events_per_s_max[c])
                events_per_s_max[c] = events_per_s[c];
        }

        if(interactive)
        {
            int max_lines, max_cols;
            printf("%s", clear_screen);
            get_screen_size(&max_lines, &max_cols);
            max_lines -= 1;
            if(mode == MODE_PERF)
            {
                /* XXX check that width doesn't exceed screen width */
                for(int l=0; l<max_lines; ++l)
                {
                    int c = l;
                    while(c < num_profile_counters)
                    {
                        char num[100];
                        struct viv_profile_counter_info *info = get_counter_info(c);
                        format_number(num, sizeof(num), events_per_s[c]);
                        if(color)
                            printf("%s", events_per_s_max[c] == 0 ? color_num_zero : color_num);
                        printf("%15.15s", num);
                        if(color)
                            printf("%s", color_reset);
                        printf(" ");
                        printf("%-30.30s", info->description);
                        printf("  ");
                        c += max_lines;
                    }
                    printf("\n");
                }
            } else if(mode == MODE_MAX)
            {
                /* XXX check that width doesn't exceed screen width */
                for(int l=0; l<max_lines; ++l)
                {
                    int c = l;
                    while(c < num_profile_counters)
                    {
                        char num[100];
                        struct viv_profile_counter_info *info = get_counter_info(c);
                        format_number(num, sizeof(num), events_per_s_max[c]);
                        if(color)
                            printf("%s", events_per_s[c] == events_per_s_max[c] ? color_num_max : color_num);
                        printf("%15.15s", num);
                        if(color)
                            printf("%s", color_reset);
                        printf(" ");
                        printf("%-30.30s", info->description);
                        printf("  ");
                        c += max_lines;
                    }
                    printf("\n");
                }
            } else if(mode == MODE_OCCUPANCY)
            {
                int lines = NUM_IDLE_MODULES;
                if(lines > max_lines)
                    lines = max_lines;

                if(color)
                    printf("%s", color_title);
                printf("Module occupancy\n");
                if(color)
                    printf("%s", color_reset);
                for(int l=0; l<lines; ++l)
                {
                    double percent = 100.0 * (double)idle_states[l] / (double)samples_per_second;
                    if(idle_module_names[l].inv)
                        percent = 100.0 - percent;
                    print_percentage_row(l, percent, idle_module_names[l].name, 6, color, 40);
                    printf("\n");
                }
            } else if(mode == MODE_DMA)
            {
                const struct dma_table {
                    int column;
                    int base_y;
                    const char *title;
                    int data_size;
                    const char **data_names;
                    uint32_t *data;
                } dma_tables[] = {
                    {0, 1, "Command state", NUM_CMD_STATE_NAMES, cmd_state_names, cmd_state},
                    {1, 1, "Command DMA state", NUM_CMD_DMA_STATE_NAMES, cmd_dma_state_names, cmd_dma_state},
                    {1, 7, "Command fetch state", NUM_CMD_FETCH_STATE_NAMES, cmd_fetch_state_names, cmd_fetch_state},
                    {1, 13, "DMA request state", NUM_REQ_DMA_STATE_NAMES, req_dma_state_names, req_dma_state},
                    {1, 19, "Cal state", NUM_CAL_STATE_NAMES, cal_state_names, cal_state},
                    {1, 25, "VE req state", NUM_VE_REQ_STATE_NAMES, ve_req_state_names, ve_req_state}
                };
#define NUM_DMA_TABLES (sizeof(dma_tables) / sizeof(dma_tables[0]))
                int bar_width = (max_cols/2) - 20;
                int fill_width = bar_width + 19;
                for(int l=0; l<max_lines; ++l)
                {
                    for(int column=0; column<2; ++column)
                    {
                        bool match = false;
                        for(int t=0; t<NUM_DMA_TABLES; ++t) /* find table that this column,row slot is part of, if any */
                        {
                            const struct dma_table *table = &dma_tables[t];
                            if(table->column == column)
                            {
                                if(l == table->base_y-1)
                                {
                                    if(color)
                                        printf("%s", color_title);
                                    printf("%-*s", fill_width, table->title);
                                    if(color)
                                        printf("%s", color_reset);
                                    match = true;
                                    break;
                                } else if(l >= table->base_y && l < (table->base_y + table->data_size))
                                {
                                    int y = l - table->base_y;
                                    double percent = 100.0 * (double)table->data[y] / (double)samples_per_second;
                                    print_percentage_row(y, percent, table->data_names[y], 10, color, bar_width);
                                    match = true;
                                    break;
                                }
                            }
                        }
                        if(!match) /* empty slot */
                        {
                            printf("%-*s", fill_width, "");
                        }
                        printf(" ");
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

