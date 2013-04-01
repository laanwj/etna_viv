GCABI ?= v2
PLATFORM_GL_LIBS ?= -lEGL -lGLESv2

CC = $(GCCPREFIX)gcc
CXX = $(GCCPREFIX)g++
LD = $(GCCPREFIX)ld
AR = $(GCCPREFIX)ar

COMMON_FLAGS =  -O2 -g -Wall -DGCABI_${GCABI} -I$(TOP)/lib -I$(TOP)/include_$(GCABI) -g -fPIC -I$(TOP)/driver \
          -I$(TOP)/gallium/include -I$(TOP)/gallium/auxiliary -pthread

CFLAGS += $(PLATFORM_CFLAGS) $(COMMON_FLAGS) -std=c99 
CXXFLAGS += $(PLATFORM_CXXFLAGS) $(COMMON_FLAGS) -std=c++98
LDFLAGS += $(PLATFORM_LDFLAGS) $(COMMON_FLAGS) -lm -lpthread

