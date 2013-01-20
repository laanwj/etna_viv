TOP=..

include $(TOP)/Makefile.inc

COMMON_FLAGS = -I../lib -I../resources -I$(TOP)/include_v2 -g -std=c99 -fPIC 
CFLAGS += $(COMMON_FLAGS)
CXXFLAGS += $(COMMON_FLAGS) -I$(NDK)/sources/cxx-stl/gnu-libstdc++/4.6/include -I$(NDK)/sources/cxx-stl/gnu-libstdc++/4.6/libs/$(CXXABI)/include
LDFLAGS += -L$(NDK)/sources/cxx-stl/gnu-libstdc++/4.6/libs/$(CXXABI) -lgnustl_static

TARGETS = cube cube_companion ps_sandbox eglinfo cube_companion_idx
GL_LIBS = -lEGL_VIVANTE -lGLESv2_VIVANTE -L$(TOP)/lib -L$(TOP)/lib/egl -Xlinker --allow-shlib-undefined -lm
LIB_OBJS = ../lib/esTransform.o ../lib/elf_hook.o ../lib/flightrecorder.o ../lib/viv_hook.o ../lib/write_bmp.o ../lib/dump_gl_screen.o ../lib/eglutil.o
COMPANION_OBJS = ../resources/companion_array.o ../resources/companion_mesh.o ../resources/companion_texture.o

all: $(TARGETS)

clean:
	rm -f *.o ../lib/*.o
	rm -f $(TARGETS)

cube: cube.o $(LIB_OBJS)
	$(CXX) $(CFLAGS) -o $@ $^  $(GL_LIBS) $(LDFLAGS)

cube_companion: cube_companion.o $(LIB_OBJS) $(COMPANION_OBJS)
	$(CXX) $(CFLAGS) -o $@ $^  $(GL_LIBS) $(LDFLAGS)

cube_companion_idx: cube_companion_idx.o $(LIB_OBJS) $(COMPANION_OBJS)
	$(CXX) $(CFLAGS) -o $@ $^  $(GL_LIBS) $(LDFLAGS)

ps_sandbox: ps_sandbox.o $(LIB_OBJS)
	$(CXX) $(CFLAGS) -o $@ $^  $(GL_LIBS) $(LDFLAGS)

eglinfo: eglinfo.o $(LIB_OBJS)
	$(CXX) $(CFLAGS) -o $@ $^  $(GL_LIBS) $(LDFLAGS)

