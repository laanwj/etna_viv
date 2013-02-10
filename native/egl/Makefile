TOP=..

include $(TOP)/Makefile.inc

COMMON_FLAGS = -I../lib -I../resources -I$(TOP)/include_$(GCABI) -g -std=c99 -fPIC 
CFLAGS += $(COMMON_FLAGS)
CXXFLAGS += $(COMMON_FLAGS) 
LDFLAGS += -lm

GL_LIBS = $(PLATFORM_GL_LIBS)
TARGETS = cube cube_companion ps_sandbox eglinfo cube_companion_idx simple_texture_cubemap
LIB_OBJS = ../lib/esTransform.o ../lib/esShapes.o ../lib/elf_hook.o ../lib/flightrecorder.o ../lib/viv_hook.o ../lib/write_bmp.o ../lib/dump_gl_screen.o ../lib/eglutil.o
COMPANION_OBJS = ../resources/companion_array.o ../resources/companion_mesh.o ../resources/companion_texture.o

all: $(TARGETS)

clean:
	rm -f *.o ../lib/*.o
	rm -f $(TARGETS) ${COMPANION_OBJS}

cube: cube.o $(LIB_OBJS)
	$(CXX) $(CFLAGS) -o $@ $^  $(GL_LIBS) $(LDFLAGS)

cube_x11: cube_x11.o ../lib/esUtil.o $(LIB_OBJS)
	$(CXX) $(CFLAGS) -o $@ $^  $(GL_LIBS) $(LDFLAGS) -lX11

cube_companion: cube_companion.o $(LIB_OBJS) $(COMPANION_OBJS)
	$(CXX) $(CFLAGS) -o $@ $^  $(GL_LIBS) $(LDFLAGS)

cube_companion_idx: cube_companion_idx.o $(LIB_OBJS) $(COMPANION_OBJS)
	$(CXX) $(CFLAGS) -o $@ $^  $(GL_LIBS) $(LDFLAGS)

ps_sandbox: ps_sandbox.o $(LIB_OBJS)
	$(CXX) $(CFLAGS) -o $@ $^  $(GL_LIBS) $(LDFLAGS)

simple_texture_cubemap: simple_texture_cubemap.o $(LIB_OBJS)
	$(CXX) $(CFLAGS) -o $@ $^  $(GL_LIBS) $(LDFLAGS)

eglinfo: eglinfo.o $(LIB_OBJS)
	$(CXX) $(CFLAGS) -o $@ $^  $(GL_LIBS) $(LDFLAGS)

