TOP=.

include $(TOP)/Makefile.inc

DIRS=minigallium etnaviv driver fb fb_rawshader replay test2d utils

# only needed for reverse engineering/dumping
# this needs a current EGL+OpenGL ES2 driver
# use "make rev"
DIRS_REV=egl

.PHONY: all clean install rev $(DIRS) $(DIRS_REV)
.NOTPARALLEL:

all: CMD = all
all: $(DIRS)

rev: CMD = all
rev: $(DIRS_REV)

$(DIRS) $(DIRS_REV):
	$(MAKE) $(CMD) -C $@

install: CMD = install
install: $(DIRS)

clean: CMD = clean
clean: $(DIRS) $(DIRS_REV)

