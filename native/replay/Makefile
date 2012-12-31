TOP=..

include $(TOP)/Makefile.inc

CXXABI = armeabi-v7a
COMMON_FLAGS = -I$(TOP)/lib -I$(TOP)/include_v2 -g -std=c99 -fPIC 
CFLAGS += $(COMMON_FLAGS)
CXXFLAGS += $(COMMON_FLAGS) -I$(NDK)/sources/cxx-stl/gnu-libstdc++/4.6/include -I$(NDK)/sources/cxx-stl/gnu-libstdc++/4.6/libs/$(CXXABI)/include
LDFLAGS += -L$(NDK)/sources/cxx-stl/gnu-libstdc++/4.6/libs/$(CXXABI) -lgnustl_static

TARGETS = cube_replay
GL_LIBS = -L$(TOP)/lib -L$(TOP)/lib/egl -Xlinker --allow-shlib-undefined -lm

all: $(TARGETS)

clean:
	rm -f *.o ../lib/*.o
	rm -f $(TARGETS)

cube_replay: cube_replay.o ../lib/write_bmp.o
	$(CC) $(CFLAGS) -o $@ $^  $(GL_LIBS) $(LDFLAGS)


