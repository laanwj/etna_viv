TOP=..

include $(TOP)/Makefile.inc

COMMON_FLAGS +=
#-DDEBUG
LDFLAGS +=
ETNAVIV_OBJS = etna.o viv.o viv_profile.o etna_bo.o etna_queue.o etna_tex.o etna_fb.o etna_rs.o

ifeq ($(ETNAVIV_HOOK), 1)
# Define ETNAVIV_HOOK=1 to enable hook mechanism
# fdr will be written to ETNAVIV_FDR environment variable
COMMON_FLAGS += -I../util -DETNAVIV_HOOK
ETNAVIV_OBJS += ../lib/viv_hook.o ../lib/flightrecorder.o ../lib/elf_hook.o
endif

ifeq ($(ETNAVIV_PROFILER), 1)
# Define ETNAVIV_PROFILER=1 if the kernel was built with VIVANTE_PROFILER
# enabled
COMMON_FLAGS += -DVIVANTE_PROFILER=1
endif

ifeq ($(ETNAVIV_DEBUG), 1)
COMMON_FLAGS += -DDEBUG=1
endif

TARGETS = libetnaviv.a

all: $(TARGETS)

clean:
	rm -f $(ETNAVIV_OBJS)
	rm -f $(TARGETS) *.o

libetnaviv.a: $(ETNAVIV_OBJS)
	$(AR) rcs $@ $^

test: test.o ../driver/etna_asm.o ../driver/etna_shader.o libminigallium.a
	$(CC) $(CFLAGS) -o $@ $^  $(LDFLAGS)

