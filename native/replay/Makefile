TOP=..

include $(TOP)/Makefile.inc

COMMON_FLAGS += -I$(TOP)/resources -I$(TOP)/etnaviv -I$(TOP)/driver
LDFLAGS += -lm
LDFLAGS += ../etnaviv/libetnaviv.a

TARGETS = cube cube_companion cube_etna ps_sandbox_etna etna_test reset viv_info cube_etna2
COMPANION_OBJS = ../resources/companion_array.o ../resources/companion_mesh.o ../resources/companion_texture.o
ETNA_OBJS = ../driver/etna_rs.o ../driver/etna_fb.o ../driver/etna_bswap.o ../driver/etna_tex.o 

all: $(TARGETS)

clean:
	rm -f *.o ../lib/*.o ../driver/*.o
	rm -f $(TARGETS)

cube: cube.o ../lib/write_bmp.o
	$(CC) $(CFLAGS) -o $@ $^  $(LDFLAGS)

cube_companion: cube_companion.o ../lib/write_bmp.o $(COMPANION_OBJS)
	$(CC) $(CFLAGS) -o $@ $^  $(LDFLAGS)

cube_etna: cube_etna.o ../lib/write_bmp.o ../lib/esTransform.o
	$(CC) $(CFLAGS) -o $@ $^  $(LDFLAGS)

cube_etna2: cube_etna2.o ../lib/write_bmp.o ../lib/esTransform.o $(ETNA_OBJS)
	$(CC) $(CFLAGS) -o $@ $^  $(LDFLAGS)

ps_sandbox_etna: ps_sandbox_etna.o ../lib/write_bmp.o ../lib/esTransform.o
	$(CC) $(CFLAGS) -o $@ $^  $(LDFLAGS)

etna_test: etna_test.o ../lib/write_bmp.o ../lib/esTransform.o $(ETNA_OBJS)
	$(CC) $(CFLAGS) -o $@ $^  $(LDFLAGS)

reset: reset.o
	$(CC) $(CFLAGS) -o $@ $^  $(LDFLAGS)

viv_info: viv_info.o
	$(CC) $(CFLAGS) -o $@ $^  $(LDFLAGS)

