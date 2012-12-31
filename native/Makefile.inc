TOOLCHAIN ?= ~/android/toolchain
SDK ?= ~/android/android-sdk-linux/
NDK ?= ~/android/ndk
ANDROID_TARGET ?= android-10
ANDROID_TARGET_NDK ?= android-9
SYSROOT ?= $(TOOLCHAIN)/sysroot/
CXXABI ?= armeabi-v7a

CC = $(TOOLCHAIN)/bin/arm-linux-androideabi-gcc
CXX = $(TOOLCHAIN)/bin/arm-linux-androideabi-g++
LD = $(TOOLCHAIN)/bin/arm-linux-androideabi-ld
AR = $(TOOLCHAIN)/bin/arm-linux-androideabi-ar
ADB = $(SDK)/platform-tools/adb

SYSTEM = android
ARCH = arm

CFLAGS += --sysroot=$(SYSROOT) -O2 -g -Wall 
CXXFLAGS += --sysroot=$(SYSROOT) -O2 -g -Wall 
LDFLAGS += --sysroot=$(SYSROOT)
#-nodefaultlibs

