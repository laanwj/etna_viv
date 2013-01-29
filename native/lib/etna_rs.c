#include "etna.h"
#include "etna_state.h"
#include "etna_rs.h"

#include "etna/state.xml.h"
#include "etna/state_3d.xml.h"

void etna_warm_up_rs(etna_ctx *cmdbuf, viv_addr_t aux_rt_physical, viv_addr_t aux_rt_ts_physical)
{
    etna_set_state(cmdbuf, VIVS_TS_COLOR_STATUS_BASE, aux_rt_ts_physical); /* ADDR_G */
    etna_set_state(cmdbuf, VIVS_TS_COLOR_SURFACE_BASE, aux_rt_physical); /* ADDR_F */
    etna_set_state(cmdbuf, VIVS_RS_FLUSH_CACHE, VIVS_RS_FLUSH_CACHE_FLUSH);
    etna_set_state(cmdbuf, VIVS_RS_CONFIG,  /* wut? */
            VIVS_RS_CONFIG_SOURCE_FORMAT(RS_FORMAT_A8R8G8B8) |
            VIVS_RS_CONFIG_SOURCE_TILED |
            VIVS_RS_CONFIG_DEST_FORMAT(RS_FORMAT_R5G6B5) |
            VIVS_RS_CONFIG_DEST_TILED);
    etna_set_state(cmdbuf, VIVS_RS_SOURCE_ADDR, aux_rt_physical); /* ADDR_F */
    etna_set_state(cmdbuf, VIVS_RS_SOURCE_STRIDE, 0x400);
    etna_set_state(cmdbuf, VIVS_RS_DEST_ADDR, aux_rt_physical); /* ADDR_F */
    etna_set_state(cmdbuf, VIVS_RS_DEST_STRIDE, 0x400);
    etna_set_state(cmdbuf, VIVS_RS_WINDOW_SIZE,
            VIVS_RS_WINDOW_SIZE_HEIGHT(4) |
            VIVS_RS_WINDOW_SIZE_WIDTH(16));
    etna_set_state(cmdbuf, VIVS_RS_CLEAR_CONTROL, VIVS_RS_CLEAR_CONTROL_MODE_DISABLED);
    etna_set_state(cmdbuf, VIVS_RS_KICKER, 0xbeebbeeb);
}
