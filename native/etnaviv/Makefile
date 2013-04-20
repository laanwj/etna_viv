TOP=..

include $(TOP)/Makefile.inc

COMMON_FLAGS += -Iauxiliary -Iinclude 
#-DDEBUG
LDFLAGS +=
ETNAVIV_OBJS = etna.o viv.o

TARGETS = libetnaviv.a

all: $(TARGETS)

clean:
	rm -f $(ETNAVIV_OBJS)
	rm -f $(TARGETS) *.o

libetnaviv.a: $(ETNAVIV_OBJS)
	$(AR) rcs $@ $^

test: test.o ../driver/etna_asm.o ../driver/etna_shader.o libminigallium.a
	$(CC) $(CFLAGS) -o $@ $^  $(LDFLAGS)

