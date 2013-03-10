#ifndef H_ETNA_STATE
#define H_ETNA_STATE
#include <string.h>

/* direct state setting */
static inline void etna_set_state(etna_ctx *cmdbuf, uint32_t address, uint32_t value)
{
    etna_reserve(cmdbuf, 2);
    ETNA_EMIT_LOAD_STATE(cmdbuf, address >> 2, 1, 0);
    ETNA_EMIT(cmdbuf, value);
}

static inline void etna_set_state_multi(etna_ctx *cmdbuf, uint32_t base, uint32_t num, const uint32_t *values)
{
    etna_reserve(cmdbuf, 1 + num + 1); /* 1 extra for potential alignment */
    ETNA_EMIT_LOAD_STATE(cmdbuf, base >> 2, num, 0);
    memcpy(&cmdbuf->buf[cmdbuf->offset], values, 4*num);
    cmdbuf->offset += num;
    ETNA_ALIGN(cmdbuf);
}
static inline void etna_set_state_f32(etna_ctx *cmdbuf, uint32_t address, float value)
{
    union {
        uint32_t i32;
        float f32;
    } x = { .f32 = value };
    etna_set_state(cmdbuf, address, x.i32);
}
static inline void etna_set_state_fixp(etna_ctx *cmdbuf, uint32_t address, uint32_t value)
{
    etna_reserve(cmdbuf, 2);
    ETNA_EMIT_LOAD_STATE(cmdbuf, address >> 2, 1, 1);
    ETNA_EMIT(cmdbuf, value);
}
static inline void etna_draw_primitives(etna_ctx *cmdbuf, uint32_t primitive_type, uint32_t start, uint32_t count)
{
#ifdef CMD_DEBUG
    printf("draw_primitives %08x %08x %08x %08x\n",
            VIV_FE_DRAW_PRIMITIVES_HEADER_OP_DRAW_PRIMITIVES,
            primitive_type, start, count);
#endif
    etna_reserve(cmdbuf, 4);
    ETNA_EMIT_DRAW_PRIMITIVES(cmdbuf, primitive_type, start, count);
}
static inline void etna_draw_indexed_primitives(etna_ctx *cmdbuf, uint32_t primitive_type, uint32_t start, uint32_t count, uint32_t offset)
{
#ifdef CMD_DEBUG
    printf("draw_primitives_indexed %08x %08x %08x %08x\n",
            VIV_FE_DRAW_PRIMITIVES_HEADER_OP_DRAW_INDEXED_PRIMITIVES,
            primitive_type, start, count);
#endif
    etna_reserve(cmdbuf, 5+1);
    ETNA_EMIT_DRAW_INDEXED_PRIMITIVES(cmdbuf, primitive_type, start, count, offset);
    ETNA_ALIGN(cmdbuf);
}


#endif

