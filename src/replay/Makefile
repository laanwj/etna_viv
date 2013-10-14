TOP=..

include $(TOP)/Makefile.inc

COMMON_FLAGS += -I$(TOP)/resources -I$(TOP)/driver
LDFLAGS_ETNA = $(LDFLAGS) -L$(TOP)/etnaviv -letnaviv

TARGETS =
TARGETS_GC800 = cube cube_companion cube_etna ps_sandbox_etna etna_test cube_etna2
TARGETS_GC880 = cube_etna2_gc880 empty_screen_gc880 cube_gc880 cube_companion_gc880 cube_etna_gc880
COMPANION_OBJS = ../resources/companion_array.o ../resources/companion_mesh.o ../resources/companion_texture.o
ETNA_OBJS = ../lib/etna_bswap.o

all: $(TARGETS)

clean:
	rm -f *.o ../lib/*.o ../driver/*.o
	rm -f $(TARGETS) $(TARGETS_GC800) $(TARGETS_GC2000)

# These specifically replay command streams for gc800, don't build them by default
gc800: $(TARGETS_GC800)

# These specifically replay command streams for gc880, don't build them by default
gc880: $(TARGETS_GC880)

empty_screen_gc880: empty_screen_gc880.o ../lib/write_bmp.o viv_raw.o
	$(CC) $(CFLAGS) -o $@ $^  $(LDFLAGS)

cube_gc880: cube_gc880.o ../lib/write_bmp.o viv_raw.o
	$(CC) $(CFLAGS) -o $@ $^  $(LDFLAGS)

cube: cube.o ../lib/write_bmp.o viv_raw.o
	$(CC) $(CFLAGS) -o $@ $^  $(LDFLAGS)

cube_companion: cube_companion.o viv_raw.o ../lib/write_bmp.o $(COMPANION_OBJS)
	$(CC) $(CFLAGS) -o $@ $^  $(LDFLAGS)

cube_companion_gc880: cube_companion_gc880.o ../lib/write_bmp.o viv_raw.o $(COMPANION_OBJS)
	$(CC) $(CFLAGS) -o $@ $^  $(LDFLAGS)

cube_etna: cube_etna.o ../lib/write_bmp.o ../lib/esTransform.o viv_raw.o
	$(CC) $(CFLAGS) -o $@ $^  $(LDFLAGS)

cube_etna_gc880: cube_etna_gc880.o ../lib/write_bmp.o ../lib/esTransform.o viv_raw.o
	$(CC) $(CFLAGS) -o $@ $^  $(LDFLAGS)

cube_etna2: cube_etna2.o ../lib/write_bmp.o ../lib/esTransform.o $(ETNA_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS_ETNA)

cube_etna2_gc880: cube_etna2_gc880.o ../lib/write_bmp.o ../lib/esTransform.o $(ETNA_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS_ETNA)

ps_sandbox_etna: ps_sandbox_etna.o ../lib/write_bmp.o ../lib/esTransform.o viv_raw.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

etna_test: etna_test.o ../lib/write_bmp.o ../lib/esTransform.o $(ETNA_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS_ETNA)

reset: reset.o viv_raw.o
	$(CC) $(CFLAGS) -o $@ $^  $(LDFLAGS)

viv_info: viv_info.o viv_raw.o
	$(CC) $(CFLAGS) -o $@ $^  $(LDFLAGS)

