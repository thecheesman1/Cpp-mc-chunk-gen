#===============================================================================
# Build the native JNI library: libmcchunkgen.so
#
# This shared library is loaded by the Fabric mod via System.loadLibrary().
# Place the resulting .so into the mod's resources or the JVM library path.
#===============================================================================

SHELL := /bin/bash

CXX      ?= g++
CXXFLAGS ?= -O3 -march=native -ffast-math -pthread -std=c++17 -fPIC

# Paths to Java headers (adjust if your JDK is elsewhere)
JAVA_HOME ?= /usr/lib/jvm/java-17-openjdk-amd64
JNI_CFLAGS := -I$(JAVA_HOME)/include -I$(JAVA_HOME)/include/linux

# Source files from the main project
GEN_SRC  := ../../../generator.cu
BRIDGE_SRC := jni_bridge.c

OBJS    := generator_bridge.o jni_bridge.o
TARGET  := libmcchunkgen.so

# Detect nvcc (GPU mode)
NVCC := $(shell command -v nvcc 2>/dev/null)

ifdef NVCC
$(info [BRIDGE] nvcc detected — GPU mode)
CUFLAGS := -O3 -arch=sm_35 -std=c++17 -dc
%.o: %.cu
	$(NVCC) $(CUFLAGS) $(JNI_CFLAGS) -c $< -o $@
%.o: %.c
	$(CXX) $(CXXFLAGS) $(JNI_CFLAGS) -c $< -o $@
$(TARGET): $(OBJS)
	$(NVCC) --shared $(OBJS) -o $@
else
$(info [BRIDGE] nvcc NOT found — CPU mock mode)
%.o: %.cu
	$(CXX) $(CXXFLAGS) $(JNI_CFLAGS) -x c++ -c $< -o $@
%.o: %.c
	$(CXX) $(CXXFLAGS) $(JNI_CFLAGS) -c $< -o $@
$(TARGET): $(OBJS)
	$(CXX) --shared $(OBJS) -pthread -o $@
endif

.PHONY: all clean install

all: $(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)

# Copy into the mod's resources so Fabric loader can find it
install: $(TARGET)
	cp $(TARGET) ../src/main/resources/