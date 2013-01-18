#ifndef H_ETNA_RS
#define H_ETNA_RS
#include "viv.h"

/* warm up RS on aux render target */
void etna_warm_up_rs(etna_ctx *cmdbuf, viv_addr_t aux_rt_physical, viv_addr_t aux_rt_ts_physical);

#endif

