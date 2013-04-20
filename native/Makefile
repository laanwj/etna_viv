TOP=.

include $(TOP)/Makefile.inc

DIRS=gallium etnaviv driver fb fb_rawshader replay egl

.PHONY: all clean install $(DIRS)

all: CMD = make all
all: $(DIRS)

$(DIRS):
	$(CMD) -C $@

install: CMD = make install
install: $(DIRS)

clean: CMD = make clean
clean: $(DIRS)

