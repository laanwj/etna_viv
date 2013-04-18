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
	auxiliary/os/os_misc.o \
	auxiliary/cso_cache/cso_hash.o \
	auxiliary/cso_cache/cso_cache.o \
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

