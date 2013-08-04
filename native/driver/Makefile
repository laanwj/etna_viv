TOP=..

include $(TOP)/Makefile.inc

COMMON_FLAGS += -DRAWSHADER
LDFLAGS +=
DRIVER_OBJS = \
	      etna_asm.o \
	      etna_blend.o \
	      etna_clear_blit.o \
	      etna_compiler.o \
	      etna_fence.o \
	      etna_pipe.o \
	      etna_rasterizer.o \
	      etna_resource.o \
	      etna_screen.o \
	      etna_shader.o \
	      etna_surface.o \
	      etna_texture.o \
	      etna_transfer.o \
	      etna_zsa.o

TARGETS = libetnadriver.a

all: $(TARGETS)

clean:
	rm -f $(DRIVER_OBJS)
	rm -f $(TARGETS) *.o

libetnadriver.a: $(DRIVER_OBJS)
	$(AR) rcs $@ $^


