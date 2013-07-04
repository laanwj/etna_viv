TOP=..

include $(TOP)/Makefile.inc

COMMON_FLAGS += -I$(TOP)/resources -I$(TOP)/driver
LDFLAGS += -lm
LDFLAGS_ETNA = $(LDFLAGS) -L$(TOP)/etnaviv -letnaviv

TARGETS = reset viv_info
TARGETS_GC800 = cube cube_companion cube_etna ps_sandbox_etna etna_test cube_etna2
TARGETS_GC2000 = cube_etna2_gc2000
COMPANION_OBJS = ../resources/companion_array.o ../resources/companion_mesh.o ../resources/companion_texture.o
ETNA_OBJS = ../driver/etna_rs.o ../driver/etna_fb.o ../lib/etna_bswap.o ../driver/etna_tex.o

all: $(TARGETS)

clean:
	rm -f *.o ../lib/*.o ../driver/*.o
	rm -f $(TARGETS)

# These specifically replay command streams for gc800, don't build them by default
gc800: $(TARGETS_GC800)

# These specifically replay command streams for gc2000, don't build them by default
gc2000: $(TARGETS_GC2000)

cube: cube.o ../lib/write_bmp.o viv_raw.o
	$(CC) $(CFLAGS) -o $@ $^  $(LDFLAGS)

cube_companion: cube_companion.o viv_raw.o ../lib/write_bmp.o $(COMPANION_OBJS)
	$(CC) $(CFLAGS) -o $@ $^  $(LDFLAGS)

cube_etna: cube_etna.o ../lib/write_bmp.o ../lib/esTransform.o viv_raw.o
	$(CC) $(CFLAGS) -o $@ $^  $(LDFLAGS)

cube_etna2: cube_etna2.o ../lib/write_bmp.o ../lib/esTransform.o $(ETNA_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS_ETNA)

cube_etna2_gc2000: cube_etna2_gc2000.o ../lib/write_bmp.o ../lib/esTransform.o $(ETNA_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS_ETNA)

ps_sandbox_etna: ps_sandbox_etna.o ../lib/write_bmp.o ../lib/esTransform.o viv_raw.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

etna_test: etna_test.o ../lib/write_bmp.o ../lib/esTransform.o $(ETNA_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS_ETNA)

reset: reset.o viv_raw.o
	$(CC) $(CFLAGS) -o $@ $^  $(LDFLAGS)

viv_info: viv_info.o viv_raw.o
	$(CC) $(CFLAGS) -o $@ $^  $(LDFLAGS)

