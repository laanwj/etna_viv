TOP=..

include $(TOP)/Makefile.inc

COMMON_FLAGS += -Iauxiliary -Iinclude -DDEBUG
LDFLAGS +=
GALLIUM_OBJS = auxiliary/tgsi/tgsi_dump.o \
	auxiliary/tgsi/tgsi_iterate.o \
	auxiliary/tgsi/tgsi_strings.o \
	auxiliary/tgsi/tgsi_info.o \
	auxiliary/tgsi/tgsi_parse.o \
	auxiliary/tgsi/tgsi_text.o \
	auxiliary/tgsi/tgsi_build.o \
	auxiliary/tgsi/tgsi_sanity.o \
	auxiliary/tgsi/tgsi_ureg.o \
	auxiliary/tgsi/tgsi_util.o \
	auxiliary/util/u_blitter.o \
	auxiliary/util/u_debug.o \
	auxiliary/util/u_format.o \
	auxiliary/util/u_format_etc.o \
	auxiliary/util/u_format_latc.o \
	auxiliary/util/u_format_other.o \
	auxiliary/util/u_format_rgtc.o \
	auxiliary/util/u_format_s3tc.o \
	auxiliary/util/u_format_srgb.o \
	auxiliary/util/u_format_table.o \
	auxiliary/util/u_format_tests.o \
	auxiliary/util/u_format_yuv.o \
	auxiliary/util/u_format_zs.o \
	auxiliary/util/u_surface.o \
	auxiliary/util/u_tile.o \
	auxiliary/util/u_transfer.o \
	auxiliary/util/u_slab.o \
	auxiliary/util/u_helpers.o \
	auxiliary/util/u_framebuffer.o \
	auxiliary/util/u_simple_shaders.o \
	auxiliary/util/u_bitmask.o \
	auxiliary/util/u_debug_refcnt.o \
	auxiliary/util/u_debug_describe.o \
	auxiliary/util/u_debug_stack.o \
	auxiliary/util/u_debug_memory.o \
        auxiliary/util/u_debug_symbol.o \
	auxiliary/util/u_upload_mgr.o \
	auxiliary/util/u_draw.o \
	auxiliary/util/u_draw_quad.o \
	auxiliary/util/u_hash_table.o \
	auxiliary/util/u_texture.o \
	auxiliary/util/u_vbuf.o \
	auxiliary/util/u_gen_mipmap.o \
	auxiliary/os/os_misc.o \
	auxiliary/cso_cache/cso_hash.o \
	auxiliary/cso_cache/cso_cache.o \
	auxiliary/cso_cache/cso_context.o \
	auxiliary/translate/translate.o \
	auxiliary/translate/translate_cache.o \
	auxiliary/translate/translate_generic.o \
	auxiliary/translate/translate_sse.o \
        targets/graw-null/graw_util.o

TARGETS = libminigallium.a

all: $(TARGETS)

clean:
	rm -f $(GALLIUM_OBJS)
	rm -f $(TARGETS) *.o

libminigallium.a: $(GALLIUM_OBJS)
	$(AR) rcs $@ $^

test: test.o ../driver/etna_asm.o ../driver/etna_shader.o libminigallium.a
	$(CC) $(CFLAGS) -o $@ $^  $(LDFLAGS)

