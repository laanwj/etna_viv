#ifndef H_VIV_PROFILE
#define H_VIV_PROFILE

#include <stdint.h>

struct viv_conn;

/* GPU profile counters */
enum viv_profile_counter
{
    VIV_PROF_GPU_CLOCK                              = 0,
    VIV_PROF_AXI_CLOCK                              = 1,
    VIV_PROF_SHADER_CLOCK                           = 2,
    VIV_PROF_GPU_CLOCK_START                        = 3,
    VIV_PROF_GPU_CLOCK_END                          = 4,
    VIV_PROF_GPU_CYCLES_COUNTER                     = 5,
    VIV_PROF_GPU_TOTAL_READ_64_BYTES_PER_FRAME      = 6,
    VIV_PROF_GPU_TOTAL_WRITE_64_BYTES_PER_FRAME     = 7,
    VIV_PROF_PE_PIXEL_COUNT_KILLED_BY_COLOR_PIPE    = 8,
    VIV_PROF_PE_PIXEL_COUNT_KILLED_BY_DEPTH_PIPE    = 9,
    VIV_PROF_PE_PIXEL_COUNT_DRAWN_BY_COLOR_PIPE     = 10,
    VIV_PROF_PE_PIXEL_COUNT_DRAWN_BY_DEPTH_PIPE     = 11,
    VIV_PROF_PS_INST_COUNTER                        = 12,
    VIV_PROF_RENDERED_PIXEL_COUNTER                 = 13,
    VIV_PROF_VS_INST_COUNTER                        = 14,
    VIV_PROF_RENDERED_VERTICE_COUNTER               = 15,
    VIV_PROF_VTX_BRANCH_INST_COUNTER                = 16,
    VIV_PROF_VTX_TEXLD_INST_COUNTER                 = 17,
    VIV_PROF_PXL_BRANCH_INST_COUNTER                = 18,
    VIV_PROF_PXL_TEXLD_INST_COUNTER                 = 19,
    VIV_PROF_PA_INPUT_VTX_COUNTER                   = 20,
    VIV_PROF_PA_INPUT_PRIM_COUNTER                  = 21,
    VIV_PROF_PA_OUTPUT_PRIM_COUNTER                 = 22,
    VIV_PROF_PA_DEPTH_CLIPPED_COUNTER               = 23,
    VIV_PROF_PA_TRIVIAL_REJECTED_COUNTER            = 24,
    VIV_PROF_PA_CULLED_COUNTER                      = 25,
    VIV_PROF_SE_CULLED_TRIANGLE_COUNT               = 26,
    VIV_PROF_SE_CULLED_LINES_COUNT                  = 27,
    VIV_PROF_RA_VALID_PIXEL_COUNT                   = 28,
    VIV_PROF_RA_TOTAL_QUAD_COUNT                    = 29,
    VIV_PROF_RA_VALID_QUAD_COUNT_AFTER_EARLY_Z      = 30,
    VIV_PROF_RA_TOTAL_PRIMITIVE_COUNT               = 31,
    VIV_PROF_RA_PIPE_CACHE_MISS_COUNTER             = 32,
    VIV_PROF_RA_PREFETCH_CACHE_MISS_COUNTER         = 33,
    VIV_PROF_RA_EEZ_CULLED_COUNTER                  = 34,
    VIV_PROF_TX_TOTAL_BILINEAR_REQUESTS             = 35,
    VIV_PROF_TX_TOTAL_TRILINEAR_REQUESTS            = 36,
    VIV_PROF_TX_TOTAL_DISCARDED_TEXTURE_REQUESTS    = 37,
    VIV_PROF_TX_TOTAL_TEXTURE_REQUESTS              = 38,
    VIV_PROF_TX_MEM_READ_COUNT                      = 39,
    VIV_PROF_TX_MEM_READ_IN_8B_COUNT                = 40,
    VIV_PROF_TX_CACHE_MISS_COUNT                    = 41,
    VIV_PROF_TX_CACHE_HIT_TEXEL_COUNT               = 42,
    VIV_PROF_TX_CACHE_MISS_TEXEL_COUNT              = 43,
    VIV_PROF_MC_TOTAL_READ_REQ_8B_FROM_PIPELINE     = 44,
    VIV_PROF_MC_TOTAL_READ_REQ_8B_FROM_IP           = 45,
    VIV_PROF_MC_TOTAL_WRITE_REQ_8B_FROM_PIPELINE    = 46,
    VIV_PROF_HI_AXI_CYCLES_READ_REQUEST_STALLED     = 47,
    VIV_PROF_HI_AXI_CYCLES_WRITE_REQUEST_STALLED    = 48,
    VIV_PROF_HI_AXI_CYCLES_WRITE_DATA_STALLED       = 49,
    VIV_PROF_PE_PIXELS_RENDERED_2D                  = 50,
    VIV_PROF_NUM_COUNTERS
};

struct viv_profile_counter_info
{
    const char *name;
    const char *description;
};

/** Get number of profile counters.
 */
uint32_t viv_get_num_profile_counters(void);

/** Get information about specific profile counter.
 */
struct viv_profile_counter_info *viv_get_profile_counter_info(enum viv_profile_counter id);

/** Read and reset 2D profile counters.
 *  This will return VIV_STATUS_NOT_SUPPORTED if built without profiling support.
 *  Call viv_get_num_profile_counters() to determine how many uint32_ts to reserve for output buffer.
 */
int viv_read_profile_counters_2d(struct viv_conn *conn, uint32_t *out);

/** Read and reset 3D profile counters.
 *  This will return VIV_STATUS_NOT_SUPPORTED if built without profiling support.
 *  Call viv_get_num_profile_counters() to determine how many uint32_ts to reserve for output buffer.
 */
int viv_read_profile_counters_3d(struct viv_conn *conn, uint32_t *out);

#endif

