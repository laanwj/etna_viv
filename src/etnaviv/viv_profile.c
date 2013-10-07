#include <etnaviv/viv_profile.h>

#include <etnaviv/viv.h>

#include "gc_abi.h"

static struct viv_profile_counter_info viv_profile_counters[] = {
    [VIV_PROF_GPU_CYCLES_COUNTER] = {"GPU_CYCLES_COUNTER", "GPU cycles counter"},
    [VIV_PROF_GPU_TOTAL_READ_64_BIT] = {"GPU_TOTAL_READ_64_BIT", "GPU total read 64 bit"},
    [VIV_PROF_GPU_TOTAL_WRITE_64_BIT] = {"GPU_TOTAL_WRITE_64_BIT", "GPU total write 64 bit"},
    [VIV_PROF_PE_PIXEL_COUNT_KILLED_BY_COLOR_PIPE] = {"PE_PIXEL_COUNT_KILLED_BY_COLOR_PIPE", "PE pixel count killed by color pipe"},
    [VIV_PROF_PE_PIXEL_COUNT_KILLED_BY_DEPTH_PIPE] = {"PE_PIXEL_COUNT_KILLED_BY_DEPTH_PIPE", "PE pixel count killed by depth pipe"},
    [VIV_PROF_PE_PIXEL_COUNT_DRAWN_BY_COLOR_PIPE] = {"PE_PIXEL_COUNT_DRAWN_BY_COLOR_PIPE", "PE pixel count drawn by color pipe"},
    [VIV_PROF_PE_PIXEL_COUNT_DRAWN_BY_DEPTH_PIPE] = {"PE_PIXEL_COUNT_DRAWN_BY_DEPTH_PIPE", "PE pixel count drawn by depth pipe"},
    [VIV_PROF_PS_INST_COUNTER] = {"PS_INST_COUNTER", "PS inst counter"},
    [VIV_PROF_RENDERED_PIXEL_COUNTER] = {"RENDERED_PIXEL_COUNTER", "Rendered pixel counter"},
    [VIV_PROF_VS_INST_COUNTER] = {"VS_INST_COUNTER", "VS inst counter"},
    [VIV_PROF_RENDERED_VERTICE_COUNTER] = {"RENDERED_VERTICE_COUNTER", "Rendered vertice counter"},
    [VIV_PROF_VTX_BRANCH_INST_COUNTER] = {"VTX_BRANCH_INST_COUNTER", "VTX branch inst counter"},
    [VIV_PROF_VTX_TEXLD_INST_COUNTER] = {"VTX_TEXLD_INST_COUNTER", "VTX texld inst counter"},
    [VIV_PROF_PXL_BRANCH_INST_COUNTER] = {"PXL_BRANCH_INST_COUNTER", "PXL branch inst counter"},
    [VIV_PROF_PXL_TEXLD_INST_COUNTER] = {"PXL_TEXLD_INST_COUNTER", "PXL texld inst counter"},
    [VIV_PROF_PA_INPUT_VTX_COUNTER] = {"PA_INPUT_VTX_COUNTER", "PA input vtx counter"},
    [VIV_PROF_PA_INPUT_PRIM_COUNTER] = {"PA_INPUT_PRIM_COUNTER", "PA input prim counter"},
    [VIV_PROF_PA_OUTPUT_PRIM_COUNTER] = {"PA_OUTPUT_PRIM_COUNTER", "PA output prim counter"},
    [VIV_PROF_PA_DEPTH_CLIPPED_COUNTER] = {"PA_DEPTH_CLIPPED_COUNTER", "PA depth clipped counter"},
    [VIV_PROF_PA_TRIVIAL_REJECTED_COUNTER] = {"PA_TRIVIAL_REJECTED_COUNTER", "PA trivial rejected counter"},
    [VIV_PROF_PA_CULLED_COUNTER] = {"PA_CULLED_COUNTER", "PA culled counter"},
    [VIV_PROF_SE_CULLED_TRIANGLE_COUNT] = {"SE_CULLED_TRIANGLE_COUNT", "SE culled triangle count"},
    [VIV_PROF_SE_CULLED_LINES_COUNT] = {"SE_CULLED_LINES_COUNT", "SE culled lines count"},
    [VIV_PROF_RA_VALID_PIXEL_COUNT] = {"RA_VALID_PIXEL_COUNT", "RA valid pixel count"},
    [VIV_PROF_RA_TOTAL_QUAD_COUNT] = {"RA_TOTAL_QUAD_COUNT", "RA total quad count"},
    [VIV_PROF_RA_VALID_QUAD_COUNT_AFTER_EARLY_Z] = {"RA_VALID_QUAD_COUNT_AFTER_EARLY_Z", "RA valid quad count after early Z"},
    [VIV_PROF_RA_TOTAL_PRIMITIVE_COUNT] = {"RA_TOTAL_PRIMITIVE_COUNT", "RA total primitive count"},
    [VIV_PROF_RA_PIPE_CACHE_MISS_COUNTER] = {"RA_PIPE_CACHE_MISS_COUNTER", "RA pipe cache miss counter"},
    [VIV_PROF_RA_PREFETCH_CACHE_MISS_COUNTER] = {"RA_PREFETCH_CACHE_MISS_COUNTER", "RA prefetch cache miss counter"},
    [VIV_PROF_RA_EEZ_CULLED_COUNTER] = {"RA_EEZ_CULLED_COUNTER", "RA EEZ culled counter"},
    [VIV_PROF_TX_TOTAL_BILINEAR_REQUESTS] = {"TX_TOTAL_BILINEAR_REQUESTS", "TX total bilinear requests"},
    [VIV_PROF_TX_TOTAL_TRILINEAR_REQUESTS] = {"TX_TOTAL_TRILINEAR_REQUESTS", "TX total trilinear requests"},
    [VIV_PROF_TX_TOTAL_DISCARDED_TEXTURE_REQUESTS] = {"TX_TOTAL_DISCARDED_TEXTURE_REQUESTS", "TX total discarded texture requests"},
    [VIV_PROF_TX_TOTAL_TEXTURE_REQUESTS] = {"TX_TOTAL_TEXTURE_REQUESTS", "TX total texture requests"},
    [VIV_PROF_TX_MEM_READ_COUNT] = {"TX_MEM_READ_COUNT", "TX mem read count"},
    [VIV_PROF_TX_MEM_READ_IN_8B_COUNT] = {"TX_MEM_READ_IN_8B_COUNT", "TX mem read in 8b count"},
    [VIV_PROF_TX_CACHE_MISS_COUNT] = {"TX_CACHE_MISS_COUNT", "TX cache miss count"},
    [VIV_PROF_TX_CACHE_HIT_TEXEL_COUNT] = {"TX_CACHE_HIT_TEXEL_COUNT", "TX cache hit texel count"},
    [VIV_PROF_TX_CACHE_MISS_TEXEL_COUNT] = {"TX_CACHE_MISS_TEXEL_COUNT", "TX cache miss texel count"},
    [VIV_PROF_MC_TOTAL_READ_REQ_8B_FROM_PIPELINE] = {"MC_TOTAL_READ_REQ_8B_FROM_PIPELINE", "MC total read req 8b from pipeline"},
    [VIV_PROF_MC_TOTAL_READ_REQ_8B_FROM_IP] = {"MC_TOTAL_READ_REQ_8B_FROM_IP", "MC total read req 8b from ip"},
    [VIV_PROF_MC_TOTAL_WRITE_REQ_8B_FROM_PIPELINE] = {"MC_TOTAL_WRITE_REQ_8B_FROM_PIPELINE", "MC total write req 8b from pipeline"},
    [VIV_PROF_HI_AXI_CYCLES_READ_REQUEST_STALLED] = {"HI_AXI_CYCLES_READ_REQUEST_STALLED", "HI AXI cycles read request stalled"},
    [VIV_PROF_HI_AXI_CYCLES_WRITE_REQUEST_STALLED] = {"HI_AXI_CYCLES_WRITE_REQUEST_STALLED", "HI AXI cycles write request stalled"},
    [VIV_PROF_HI_AXI_CYCLES_WRITE_DATA_STALLED] = {"HI_AXI_CYCLES_WRITE_DATA_STALLED", "HI AXI cycles write data stalled"},
    [VIV_PROF_PE_PIXELS_RENDERED_2D] = {"PE_PIXELS_RENDERED_2D", "PE pixels rendered 2D"},
};

uint32_t viv_get_num_profile_counters(void)
{
    return VIV_PROF_NUM_COUNTERS;
}

struct viv_profile_counter_info *viv_get_profile_counter_info(enum viv_profile_counter id)
{
    if(id >= VIV_PROF_NUM_COUNTERS)
        return NULL;
    return &viv_profile_counters[id];
}

int viv_read_profile_counters_3d(struct viv_conn *conn, uint32_t *out)
{
#if VIVANTE_PROFILER
    gcsHAL_INTERFACE id = {
        .command = gcvHAL_READ_ALL_PROFILE_REGISTERS,
        .u = {
        }
    };
    int rv = viv_invoke(conn, &id);
    if(rv < 0)
        return rv;
    gcsPROFILER_COUNTERS *counters = &id.u.RegisterProfileData.counters;

    out[VIV_PROF_GPU_CYCLES_COUNTER] = counters->gpuCyclesCounter;
    out[VIV_PROF_GPU_TOTAL_READ_64_BIT] = counters->gpuTotalRead64BytesPerFrame;
    out[VIV_PROF_GPU_TOTAL_WRITE_64_BIT] = counters->gpuTotalWrite64BytesPerFrame;
    out[VIV_PROF_PE_PIXEL_COUNT_KILLED_BY_COLOR_PIPE] = counters->pe_pixel_count_killed_by_color_pipe;
    out[VIV_PROF_PE_PIXEL_COUNT_KILLED_BY_DEPTH_PIPE] = counters->pe_pixel_count_killed_by_depth_pipe;
    out[VIV_PROF_PE_PIXEL_COUNT_DRAWN_BY_COLOR_PIPE] = counters->pe_pixel_count_drawn_by_color_pipe;
    out[VIV_PROF_PE_PIXEL_COUNT_DRAWN_BY_DEPTH_PIPE] = counters->pe_pixel_count_drawn_by_depth_pipe;
    out[VIV_PROF_PS_INST_COUNTER] = counters->ps_inst_counter;
    out[VIV_PROF_RENDERED_PIXEL_COUNTER] = counters->rendered_pixel_counter;
    out[VIV_PROF_VS_INST_COUNTER] = counters->vs_inst_counter;
    out[VIV_PROF_RENDERED_VERTICE_COUNTER] = counters->rendered_vertice_counter;
    out[VIV_PROF_VTX_BRANCH_INST_COUNTER] = counters->vtx_branch_inst_counter;
    out[VIV_PROF_VTX_TEXLD_INST_COUNTER] = counters->vtx_texld_inst_counter;
    out[VIV_PROF_PXL_BRANCH_INST_COUNTER] = counters->pxl_branch_inst_counter;
    out[VIV_PROF_PXL_TEXLD_INST_COUNTER] = counters->pxl_texld_inst_counter;
    out[VIV_PROF_PA_INPUT_VTX_COUNTER] = counters->pa_input_vtx_counter;
    out[VIV_PROF_PA_INPUT_PRIM_COUNTER] = counters->pa_input_prim_counter;
    out[VIV_PROF_PA_OUTPUT_PRIM_COUNTER] = counters->pa_output_prim_counter;
    out[VIV_PROF_PA_DEPTH_CLIPPED_COUNTER] = counters->pa_depth_clipped_counter;
    out[VIV_PROF_PA_TRIVIAL_REJECTED_COUNTER] = counters->pa_trivial_rejected_counter;
    out[VIV_PROF_PA_CULLED_COUNTER] = counters->pa_culled_counter;
    out[VIV_PROF_SE_CULLED_TRIANGLE_COUNT] = counters->se_culled_triangle_count;
    out[VIV_PROF_SE_CULLED_LINES_COUNT] = counters->se_culled_lines_count;
    out[VIV_PROF_RA_VALID_PIXEL_COUNT] = counters->ra_valid_pixel_count;
    out[VIV_PROF_RA_TOTAL_QUAD_COUNT] = counters->ra_total_quad_count;
    out[VIV_PROF_RA_VALID_QUAD_COUNT_AFTER_EARLY_Z] = counters->ra_valid_quad_count_after_early_z;
    out[VIV_PROF_RA_TOTAL_PRIMITIVE_COUNT] = counters->ra_total_primitive_count;
    out[VIV_PROF_RA_PIPE_CACHE_MISS_COUNTER] = counters->ra_pipe_cache_miss_counter;
    out[VIV_PROF_RA_PREFETCH_CACHE_MISS_COUNTER] = counters->ra_prefetch_cache_miss_counter;
    out[VIV_PROF_RA_EEZ_CULLED_COUNTER] = counters->ra_eez_culled_counter;
    out[VIV_PROF_TX_TOTAL_BILINEAR_REQUESTS] = counters->tx_total_bilinear_requests;
    out[VIV_PROF_TX_TOTAL_TRILINEAR_REQUESTS] = counters->tx_total_trilinear_requests;
    out[VIV_PROF_TX_TOTAL_DISCARDED_TEXTURE_REQUESTS] = counters->tx_total_discarded_texture_requests;
    out[VIV_PROF_TX_TOTAL_TEXTURE_REQUESTS] = counters->tx_total_texture_requests;
    out[VIV_PROF_TX_MEM_READ_COUNT] = counters->tx_mem_read_count;
    out[VIV_PROF_TX_MEM_READ_IN_8B_COUNT] = counters->tx_mem_read_in_8B_count;
    out[VIV_PROF_TX_CACHE_MISS_COUNT] = counters->tx_cache_miss_count;
    out[VIV_PROF_TX_CACHE_HIT_TEXEL_COUNT] = counters->tx_cache_hit_texel_count;
    out[VIV_PROF_TX_CACHE_MISS_TEXEL_COUNT] = counters->tx_cache_miss_texel_count;
    out[VIV_PROF_MC_TOTAL_READ_REQ_8B_FROM_PIPELINE] = counters->mc_total_read_req_8B_from_pipeline;
    out[VIV_PROF_MC_TOTAL_READ_REQ_8B_FROM_IP] = counters->mc_total_read_req_8B_from_IP;
    out[VIV_PROF_MC_TOTAL_WRITE_REQ_8B_FROM_PIPELINE] = counters->mc_total_write_req_8B_from_pipeline;
    out[VIV_PROF_HI_AXI_CYCLES_READ_REQUEST_STALLED] = counters->hi_axi_cycles_read_request_stalled;
    out[VIV_PROF_HI_AXI_CYCLES_WRITE_REQUEST_STALLED] = counters->hi_axi_cycles_write_request_stalled;
    out[VIV_PROF_HI_AXI_CYCLES_WRITE_DATA_STALLED] = counters->hi_axi_cycles_write_data_stalled;
    return 0;
#else
    return VIV_STATUS_NOT_SUPPORTED;
#endif
}

int viv_read_profile_counters_2d(struct viv_conn *conn, uint32_t *out)
{
#if VIVANTE_PROFILER
    gcsHAL_INTERFACE id = {
        .command = gcvHAL_PROFILE_REGISTERS_2D,
        .u = {
        }
    };
    int rv = viv_invoke(conn, &id);
    if(rv < 0)
        return rv;
    struct _gcs2D_PROFILE *counters = &id.u.RegisterProfileData2D.hwProfile2D;

    out[VIV_PROF_GPU_CYCLES_COUNTER] = counters->cycleCount;
    out[VIV_PROF_PE_PIXELS_RENDERED_2D] = counters->pixelsRendered;
    return 0;
#else
    return VIV_STATUS_NOT_SUPPORTED;
#endif
}

void viv_get_counters_reset_after_read(struct viv_conn *conn, bool *counters)
{
    /* Either dove driver doesn't reset perf counters properly, or
     * is it the hw, I'm not sure */
    for(int c=0; c<VIV_PROF_NUM_COUNTERS; ++c)
        counters[c] = true;
    if(conn->kernel_driver.major < 4)
    {
        counters[VIV_PROF_SE_CULLED_TRIANGLE_COUNT] = false;
        counters[VIV_PROF_SE_CULLED_LINES_COUNT] = false;
    } else {
        counters[VIV_PROF_SE_CULLED_TRIANGLE_COUNT] = false;
        counters[VIV_PROF_SE_CULLED_LINES_COUNT] = false;
        counters[VIV_PROF_PS_INST_COUNTER] = false;
        counters[VIV_PROF_VS_INST_COUNTER] = false;
        counters[VIV_PROF_RENDERED_PIXEL_COUNTER] = false;
        counters[VIV_PROF_RENDERED_VERTICE_COUNTER] = false;
        counters[VIV_PROF_PXL_TEXLD_INST_COUNTER] = false;
        counters[VIV_PROF_PXL_BRANCH_INST_COUNTER] = false;
        counters[VIV_PROF_VTX_TEXLD_INST_COUNTER] = false;
        counters[VIV_PROF_VTX_BRANCH_INST_COUNTER] = false;
    }
}

