TOP=..

include $(TOP)/Makefile.inc

COMMON_FLAGS += -I$(TOP)/resources -I$(TOP)/etnaviv -I$(TOP)/driver
CFLAGS += $(COMMON_FLAGS)
CXXFLAGS += $(COMMON_FLAGS) 
LDFLAGS += -lm ../gallium/libminigallium.a ../etnaviv/libetnaviv.a

TARGETS = fbtest etna_test rotate_cube cube_companion rstests mip_cube_raw
COMPANION_OBJS = ../resources/companion_array.o ../resources/companion_mesh.o ../resources/companion_texture.o
ETNA_OBJS = ../driver/etna_rs.o ../driver/etna_fb.o ../driver/etna_mem.o ../driver/etna_bswap.o ../driver/etna_tex.o  ../driver/etna_pipe.o ../driver/etna_shader.o ../driver/etna_asm.o

all: $(TARGETS)

clean:
	rm -f *.o ../lib/*.o ../driver/*.o
	rm -f $(TARGETS)

fbtest: fbtest.o ../lib/write_bmp.o $(ETNA_OBJS)
	$(CC) $(CFLAGS) -o $@ $^  $(LDFLAGS)

etna_test: etna_test.o ../lib/write_bmp.o $(ETNA_OBJS)
	$(CC) $(CFLAGS) -o $@ $^  $(LDFLAGS)

rotate_cube: rotate_cube.o ../lib/write_bmp.o $(ETNA_OBJS) ../lib/esTransform.o
	$(CC) $(CFLAGS) -o $@ $^  $(LDFLAGS)

mip_cube_raw: mip_cube_raw.o ../lib/write_bmp.o ../lib/esTransform.o ../lib/dds.o $(ETNA_OBJS)
	$(CC) $(CFLAGS) -o $@ $^  $(LDFLAGS)

cube_companion: cube_companion.o ../lib/write_bmp.o ../lib/esTransform.o $(COMPANION_OBJS) $(ETNA_OBJS)
	$(CC) $(CFLAGS) -o $@ $^  $(LDFLAGS)

rstests: rstests.o ../lib/write_bmp.o ../lib/esTransform.o $(COMPANION_OBJS) $(ETNA_OBJS)
	$(CC) $(CFLAGS) -o $@ $^  $(LDFLAGS)

