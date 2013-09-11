TOP=..

include $(TOP)/Makefile.inc

COMMON_FLAGS +=
CFLAGS +=
CXXFLAGS += 
LDFLAGS +=  

ifeq ($(ETNAVIV_PROFILER), 1)
# Define ETNAVIV_PROFILER=1 if the kernel was built with VIVANTE_PROFILER
# enabled
COMMON_FLAGS += -DVIVANTE_PROFILER=1
endif

TARGETS = viv_gpu_top viv_info viv_registers viv_reset viv_watch

ETNA_OBJS = ../etnaviv/libetnaviv.a

all: $(TARGETS)

clean:
	rm -f *.o ../lib/*.o ../resources/*.o
	rm -f $(TARGETS)

viv_gpu_top: viv_gpu_top.o $(ETNA_OBJS)
	$(CXX) $(CFLAGS) -o $@ $^  $(LDFLAGS)

viv_info: viv_info.o $(ETNA_OBJS)
	$(CXX) $(CFLAGS) -o $@ $^  $(LDFLAGS)

viv_registers: viv_registers.o $(ETNA_OBJS)
	$(CXX) $(CFLAGS) -o $@ $^  $(LDFLAGS)

viv_reset: viv_reset.o $(ETNA_OBJS)
	$(CXX) $(CFLAGS) -o $@ $^  $(LDFLAGS)

viv_watch: viv_watch.o $(ETNA_OBJS)
	$(CXX) $(CFLAGS) -o $@ $^  $(LDFLAGS)

