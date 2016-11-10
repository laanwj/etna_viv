TOP=..

include $(TOP)/Makefile.inc

COMMON_FLAGS += -I$(TOP)/resources -I$(TOP)/driver
CFLAGS += -DDUMP
CXXFLAGS += 
LDFLAGS += -lpng

TARGETS = clear2d line2d bitblt2d_from_stream line2d_patterned bitblt2d \
	  bitblt2d_palette filterblt2d stretchblt2d filterblt2d_fromplanar \
	  bitblt2d_alpha bitblt2d_rotate

ETNA_OBJS = ../lib/write_bmp.o ../lib/etna_bswap.o ../etnaviv/libetnaviv.a ../lib/read_png.o

all: $(TARGETS)

clean:
	rm -f *.o ../lib/*.o ../resources/*.o
	rm -f $(TARGETS)

clear2d: clear2d.o $(ETNA_OBJS)
	$(CXX) $(CFLAGS) -o $@ $^  $(LDFLAGS)

line2d: line2d.o $(ETNA_OBJS)
	$(CXX) $(CFLAGS) -o $@ $^  $(LDFLAGS)

line2d_patterned: line2d_patterned.o $(ETNA_OBJS)
	$(CXX) $(CFLAGS) -o $@ $^  $(LDFLAGS)

bitblt2d_from_stream: bitblt2d_from_stream.o $(ETNA_OBJS)
	$(CXX) $(CFLAGS) -o $@ $^  $(LDFLAGS)

bitblt2d_palette: bitblt2d_palette.o $(ETNA_OBJS)
	$(CXX) $(CFLAGS) -o $@ $^  $(LDFLAGS)

filterblt2d: filterblt2d.o $(ETNA_OBJS)
	$(CXX) $(CFLAGS) -o $@ $^  $(LDFLAGS)

filterblt2d_fromplanar: filterblt2d_fromplanar.o $(ETNA_OBJS)
	$(CXX) $(CFLAGS) -o $@ $^  $(LDFLAGS)

bitblt2d_rotate: bitblt2d_rotate.o $(ETNA_OBJS)
	$(CXX) $(CFLAGS) -o $@ $^  $(LDFLAGS)

bitblt2d: bitblt2d.o $(ETNA_OBJS)
	$(CXX) $(CFLAGS) -o $@ $^  $(LDFLAGS)

bitblt2d_alpha: bitblt2d_alpha.o $(ETNA_OBJS)
	$(CXX) $(CFLAGS) -o $@ $^  $(LDFLAGS)

stretchblt2d: stretchblt2d.o $(ETNA_OBJS)
	$(CXX) $(CFLAGS) -o $@ $^  $(LDFLAGS)

