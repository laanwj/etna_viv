TOP=..

include $(TOP)/Makefile.inc

COMMON_FLAGS += -I../resources
CFLAGS += -DDUMP
CXXFLAGS += 
LDFLAGS += ../gallium/libminigallium.a

TARGETS = mip_cube cubemap_sphere stencil_test alpha_blend cube_companion particle_system displacement etna_gears rotate_cube
COMPANION_OBJS = ../resources/companion_array.o ../resources/companion_mesh.o ../resources/companion_texture.o
ETNA_OBJS = ../driver/viv.o ../driver/etna.o ../driver/etna_rs.o ../driver/etna_fb.o ../driver/etna_mem.o \
            ../driver/etna_bswap.o ../driver/etna_tex.o  ../driver/etna_pipe.o \
	    ../driver/etna_shader.o ../driver/etna_asm.o

all: $(TARGETS)

clean:
	rm -f *.o ../lib/*.o ../resources/*.o ../driver/*.o
	rm -f $(TARGETS)

# XXX port this to etna_pipe
rotate_cube: rotate_cube.o ../lib/write_bmp.o $(ETNA_OBJS) ../lib/esTransform.o
	$(CC) $(CFLAGS) -o $@ $^  $(LDFLAGS)

mip_cube: mip_cube.o ../lib/write_bmp.o ../lib/esTransform.o ../lib/dds.o $(ETNA_OBJS)
	$(CC) $(CFLAGS) -o $@ $^  $(LDFLAGS)

cubemap_sphere: cubemap_sphere.o ../lib/write_bmp.o ../lib/esTransform.o ../lib/dds.o ../lib/esShapes.o $(ETNA_OBJS)
	$(CC) $(CFLAGS) -o $@ $^  $(LDFLAGS)

cube_companion: cube_companion.o ../lib/write_bmp.o ../lib/esTransform.o $(COMPANION_OBJS) $(ETNA_OBJS)
	$(CC) $(CFLAGS) -o $@ $^  $(LDFLAGS)

alpha_blend: alpha_blend.o ../lib/write_bmp.o ../lib/esTransform.o $(ETNA_OBJS)
	$(CC) $(CFLAGS) -o $@ $^  $(LDFLAGS)

stencil_test: stencil_test.o ../lib/write_bmp.o ../lib/esTransform.o $(ETNA_OBJS)
	$(CC) $(CFLAGS) -o $@ $^  $(LDFLAGS)

particle_system: particle_system.o ../lib/write_bmp.o ../lib/esUtil.o ../lib/esTransform.o $(ETNA_OBJS)
	$(CC) $(CFLAGS) -o $@ $^  $(LDFLAGS)

displacement: displacement.o ../lib/write_bmp.o ../lib/esUtil.o ../lib/esTransform.o ../lib/esShapes.o $(ETNA_OBJS)
	$(CC) $(CFLAGS) -o $@ $^  $(LDFLAGS)

etna_gears: etna_gears.o ../lib/write_bmp.o ../lib/esUtil.o ../lib/esTransform.o ../lib/esShapes.o $(ETNA_OBJS)
	$(CC) $(CFLAGS) -o $@ $^  $(LDFLAGS)

