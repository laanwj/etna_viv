TOP=.

include $(TOP)/Makefile.inc

DIRS=gallium etnaviv driver fb fb_rawshader replay

# only needed for reverse engineering/dumping
# use "make rev"
DIRS_REV=egl

.PHONY: all clean install rev $(DIRS) $(DIRS_REV)

all: CMD = make all
all: $(DIRS)

rev: CMD = make all
rev: $(DIRS_REV)

$(DIRS) $(DIRS_REV):
	$(CMD) -C $@

install: CMD = make install
install: $(DIRS)

clean: CMD = make clean
clean: $(DIRS) $(DIRS_REV)

