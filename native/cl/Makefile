TOP=..

include $(TOP)/Makefile.inc

COMMON_FLAGS += -I../resources 
CFLAGS += $(COMMON_FLAGS)
CXXFLAGS += $(COMMON_FLAGS) 
LDFLAGS += 

GL_LIBS = $(PLATFORM_CL_LIBS)
TARGETS = hello 
LIB_OBJS = ../lib/elf_hook.o ../lib/flightrecorder.o ../lib/viv_hook.o 

all: $(TARGETS)

clean:
	rm -f *.o ../lib/*.o
	rm -f $(TARGETS) ${COMPANION_OBJS}

hello: hello.o $(LIB_OBJS)
	$(CXX) $(CFLAGS) -o $@ $^  $(GL_LIBS) $(LDFLAGS)


