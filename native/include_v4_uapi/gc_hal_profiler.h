/****************************************************************************
*
*    Copyright (C) 2005 - 2012 by Vivante Corp.
*
*    This program is free software; you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation; either version 2 of the license, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program; if not write to the Free Software
*    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*
*****************************************************************************/


#ifndef __gc_hal_profiler_h_
#define __gc_hal_profiler_h_

/* HW profile information. */
typedef struct _gcsPROFILER_COUNTERS
{
    /* HW static counters. */
    __u32           gpuClock;
    __u32           axiClock;
    __u32           shaderClock;

    /* HW vairable counters. */
    __u32           gpuClockStart;
    __u32           gpuClockEnd;

    /* HW vairable counters. */
    __u32           gpuCyclesCounter;
    __u32           gpuTotalRead64BytesPerFrame;
    __u32           gpuTotalWrite64BytesPerFrame;

    /* PE */
    __u32           pe_pixel_count_killed_by_color_pipe;
    __u32           pe_pixel_count_killed_by_depth_pipe;
    __u32           pe_pixel_count_drawn_by_color_pipe;
    __u32           pe_pixel_count_drawn_by_depth_pipe;

    /* SH */
    __u32           ps_inst_counter;
    __u32           rendered_pixel_counter;
    __u32           vs_inst_counter;
    __u32           rendered_vertice_counter;
    __u32           vtx_branch_inst_counter;
    __u32           vtx_texld_inst_counter;
    __u32           pxl_branch_inst_counter;
    __u32           pxl_texld_inst_counter;

    /* PA */
    __u32           pa_input_vtx_counter;
    __u32           pa_input_prim_counter;
    __u32           pa_output_prim_counter;
    __u32           pa_depth_clipped_counter;
    __u32           pa_trivial_rejected_counter;
    __u32           pa_culled_counter;

    /* SE */
    __u32           se_culled_triangle_count;
    __u32           se_culled_lines_count;

    /* RA */
    __u32           ra_valid_pixel_count;
    __u32           ra_total_quad_count;
    __u32           ra_valid_quad_count_after_early_z;
    __u32           ra_total_primitive_count;
    __u32           ra_pipe_cache_miss_counter;
    __u32           ra_prefetch_cache_miss_counter;
    __u32           ra_eez_culled_counter;

    /* TX */
    __u32           tx_total_bilinear_requests;
    __u32           tx_total_trilinear_requests;
    __u32           tx_total_discarded_texture_requests;
    __u32           tx_total_texture_requests;
    __u32           tx_mem_read_count;
    __u32           tx_mem_read_in_8B_count;
    __u32           tx_cache_miss_count;
    __u32           tx_cache_hit_texel_count;
    __u32           tx_cache_miss_texel_count;

    /* MC */
    __u32           mc_total_read_req_8B_from_pipeline;
    __u32           mc_total_read_req_8B_from_IP;
    __u32           mc_total_write_req_8B_from_pipeline;

    /* HI */
    __u32           hi_axi_cycles_read_request_stalled;
    __u32           hi_axi_cycles_write_request_stalled;
    __u32           hi_axi_cycles_write_data_stalled;
}
gcsPROFILER_COUNTERS;

#endif /* __gc_hal_profiler_h_ */
