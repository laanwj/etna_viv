TOP=..

include $(TOP)/Makefile.inc

COMMON_FLAGS += -I$(TOP)/driver
CFLAGS +=
CXXFLAGS +=
LDFLAGS +=

ifeq ($(ETNAVIV_PROFILER), 1)
# Define ETNAVIV_PROFILER=1 if the kernel was built with VIVANTE_PROFILER
# enabled
COMMON_FLAGS += -DVIVANTE_PROFILER=1
endif

TARGETS = viv_gpu_top viv_info viv_registers viv_reset viv_watch viv_throughput viv_interpose.so

ETNA_OBJS = ../etnaviv/libetnaviv.a
DRIVER_OBJS = ../lib/fbdemos.o ../lib/etna_bswap.o ../driver/libetnadriver.a ../minigallium/libminigallium.a ../etnaviv/libetnaviv.a

LDFLAGS_SLIB = -shared -Xlinker -Bsymbolic -ldl

all: $(TARGETS)

clean:
	rm -f *.o ../lib/*.o ../resources/*.o
	rm -f $(TARGETS)

viv_gpu_top: viv_gpu_top.o $(ETNA_OBJS)
	$(CXX) $(CFLAGS) -o $@ $^  $(LDFLAGS)

viv_info: viv_info.o
	$(CXX) $(CFLAGS) -o $@ $^  $(LDFLAGS)

viv_interpose.so: viv_interpose.o ../lib/viv_hook.o ../lib/elf_hook.o ../lib/flightrecorder.o
	$(CXX) $(CFLAGS) -o $@ $^  $(LDFLAGS) $(LDFLAGS_SLIB)

viv_registers: viv_registers.o $(ETNA_OBJS)
	$(CXX) $(CFLAGS) -o $@ $^  $(LDFLAGS)

viv_reset: viv_reset.o $(ETNA_OBJS)
	$(CXX) $(CFLAGS) -o $@ $^  $(LDFLAGS)

viv_watch: viv_watch.o $(ETNA_OBJS)
	$(CXX) $(CFLAGS) -o $@ $^  $(LDFLAGS)

# viv_throughput is the only utility that needs the gallium pipe driver
viv_throughput: viv_throughput.o $(DRIVER_OBJS)
	$(CXX) $(CFLAGS) -o $@ $^  $(LDFLAGS)

