TOP=..

include $(TOP)/Makefile.inc

COMMON_FLAGS += -DRAWSHADER
LDFLAGS +=
DRIVER_OBJS = etna_rs.o etna_fb.o etna_bswap.o etna_pipe.o etna_shader.o etna_asm.o etna_screen.o

TARGETS = libetnadriver.a

all: $(TARGETS)

clean:
	rm -f $(DRIVER_OBJS)
	rm -f $(TARGETS) *.o

libetnadriver.a: $(DRIVER_OBJS)
	$(AR) rcs $@ $^


