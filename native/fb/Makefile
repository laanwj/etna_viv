TOP=..

include $(TOP)/Makefile.inc

CXXABI = armeabi-v7a
COMMON_FLAGS = -I$(TOP)/lib -I$(TOP)/include_v2 -g -std=c99 -fPIC  -I../resources -DANDROID -I../include
CFLAGS += $(COMMON_FLAGS)
CXXFLAGS += $(COMMON_FLAGS) -I$(NDK)/sources/cxx-stl/gnu-libstdc++/4.6/include -I$(NDK)/sources/cxx-stl/gnu-libstdc++/4.6/libs/$(CXXABI)/include
LDFLAGS += -L$(NDK)/sources/cxx-stl/gnu-libstdc++/4.6/libs/$(CXXABI) -lgnustl_static

TARGETS = fbtest etna_test rotate_cube cube_companion
GL_LIBS = -L$(TOP)/lib -L$(TOP)/lib/egl -Xlinker --allow-shlib-undefined -lm
COMPANION_OBJS = ../resources/companion_array.o ../resources/companion_mesh.o ../resources/companion_texture.o

all: $(TARGETS)

clean:
	rm -f *.o ../lib/*.o
	rm -f $(TARGETS)

fbtest: fbtest.o ../lib/write_bmp.o ../lib/viv.o ../lib/etna_fb.o
	$(CC) $(CFLAGS) -o $@ $^  $(GL_LIBS) $(LDFLAGS)

etna_test: etna_test.o ../lib/write_bmp.o ../lib/viv.o ../lib/etna.o ../lib/etna_rs.o ../lib/etna_fb.o
	$(CC) $(CFLAGS) -o $@ $^  $(GL_LIBS) $(LDFLAGS)

rotate_cube: rotate_cube.o ../lib/write_bmp.o ../lib/viv.o ../lib/etna.o ../lib/etna_rs.o ../lib/etna_fb.o ../lib/esTransform.o
	$(CC) $(CFLAGS) -o $@ $^  $(GL_LIBS) $(LDFLAGS)

cube_companion: cube_companion.o ../lib/write_bmp.o ../lib/viv.o ../lib/etna.o ../lib/etna_rs.o ../lib/etna_fb.o ../lib/esTransform.o $(COMPANION_OBJS)
	$(CC) $(CFLAGS) -o $@ $^  $(GL_LIBS) $(LDFLAGS)

