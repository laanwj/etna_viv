TOP=..

include $(TOP)/Makefile.inc

COMMON_FLAGS = -I../lib -I$(TOP)/include_v2 -g -std=c99 -fPIC 
CFLAGS += $(COMMON_FLAGS)
CXXFLAGS += $(COMMON_FLAGS) -I$(NDK)/sources/cxx-stl/gnu-libstdc++/4.6/include -I$(NDK)/sources/cxx-stl/gnu-libstdc++/4.6/libs/$(CXXABI)/include
LDFLAGS += -L$(NDK)/sources/cxx-stl/gnu-libstdc++/4.6/libs/$(CXXABI) -lgnustl_static

TARGETS = cube
GL_LIBS = -lEGL_VIVANTE -lGLESv2_VIVANTE -L$(TOP)/lib -L$(TOP)/lib/egl -Xlinker --allow-shlib-undefined -lm

all: $(TARGETS)

clean:
	rm -f *.o ../lib/*.o
	rm -f $(TARGETS)

cube: cube.o ../lib/esTransform.o ../lib/elf_hook.o ../lib/flightrecorder.o ../lib/viv_hook.o ../lib/write_bmp.o
	$(CXX) $(CFLAGS) -o $@ $^  $(GL_LIBS) $(LDFLAGS)

