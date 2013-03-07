TOP=..

include $(TOP)/Makefile.inc

COMMON_FLAGS += -Iauxilary -Iinclude
CFLAGS += $(COMMON_FLAGS)
CXXFLAGS += $(COMMON_FLAGS) 
LDFLAGS +=
GALLIUM_OBJS = auxilary/tgsi/tgsi_dump.o \
	auxilary/tgsi/tgsi_iterate.o \
	auxilary/tgsi/tgsi_strings.o \
	auxilary/tgsi/tgsi_info.o \
	auxilary/tgsi/tgsi_parse.o \
	auxilary/tgsi/tgsi_text.o \
	auxilary/tgsi/tgsi_build.o \
	auxilary/tgsi/tgsi_sanity.o \
	auxilary/util/u_debug.o \
	auxilary/os/os_misc.o \
	auxilary/cso_cache/cso_hash.o \
	auxilary/cso_cache/cso_cache.o

TARGETS = libminigallium.a test

all: $(TARGETS)

clean:
	rm -f $(GALLIUM_OBJS)
	rm -f $(TARGETS) *.o

libminigallium.a: $(GALLIUM_OBJS)
	$(AR) rcs $@ $^

test: test.o libminigallium.a
	$(CC) $(CFLAGS) -o $@ $^  $(LDFLAGS)

