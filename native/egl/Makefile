TOP=..

include $(TOP)/Makefile.inc

COMMON_FLAGS += -I../resources 

ifeq ($(USE_DIRECTFB),1)
	DIRECTFB_FLAGS = -DLINUX -DEGL_API_DFB -I/usr/include/directfb/
else
	DIRECTFB_FLAGS =
endif

CFLAGS += $(COMMON_FLAGS) $(DIRECTFB_FLAGS)
CXXFLAGS += $(COMMON_FLAGS) $(DIRECTFB_FLAGS)
LDFLAGS += -ldl

GL_LIBS = $(PLATFORM_GL_LIBS)
TARGETS = cube cube_companion ps_sandbox eglinfo cube_companion_idx simple_texture_cubemap displacement
TARGETS_X11 = cube_x11 particle_system_x11
LIB_OBJS = ../lib/esTransform.o ../lib/esShapes.o ../lib/elf_hook.o ../lib/flightrecorder.o ../lib/viv_hook.o ../lib/write_bmp.o ../lib/dump_gl_screen.o ../lib/eglutil.o
COMPANION_OBJS = ../resources/companion_array.o ../resources/companion_mesh.o ../resources/companion_texture.o

all: $(TARGETS)

all_x11: $(TARGETS_X11)

clean:
	rm -f *.o ../lib/*.o
	rm -f $(TARGETS) $(COMPANION_OBJS) $(TARGETS_X11)

cube: cube.o $(LIB_OBJS)
	$(CXX) $(CFLAGS) -o $@ $^  $(GL_LIBS) $(LDFLAGS)

displacement: displacement.o ../lib/esShapes.o $(LIB_OBJS)
	$(CXX) $(CFLAGS) -o $@ $^  $(GL_LIBS) $(LDFLAGS)


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

stencil_test: stencil_test.o ${LIB_OBJS}
	$(CXX) $(CFLAGS) -o $@ $^  $(GL_LIBS) $(LDFLAGS)

cube_x11: cube_x11.o ../lib/esUtil.o ../lib/esWindow.o $(LIB_OBJS)
	$(CXX) $(CFLAGS) -o $@ $^  $(GL_LIBS) $(LDFLAGS) -lX11

particle_system_x11: particle_system_x11.o ../lib/esUtil.o ../lib/esWindow.o ../lib/esShader.o $(LIB_OBJS)
	$(CXX) $(CFLAGS) -o $@ $^  $(GL_LIBS) $(LDFLAGS) -lX11

