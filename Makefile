#===============================================================================
# Makefile -- GPU-Accelerated Chunk Generator
#
# Auto-detects nvcc (CUDA) and glslc (Vulkan).
# - nvcc found:  real CUDA GPU mode
# - glslc found: Vulkan compute shader compilation
# - Neither:     CPU-only mock mode via cuda_mock.h and vulkan_backend.h fallback
#===============================================================================

SHELL := /bin/bash

CU_SRC    := generator.cu
CPP_SRC   := main.cpp
OFFLINE_SRC := chunkgen_offline.cpp
OBJS      := generator.o
TARGET    := chunkgen
TARGET_OFFLINE := chunkgen_offline

CXX      ?= g++
CXXFLAGS ?= -O3 -march=native -ffast-math -pthread -std=c++17
LDFLAGS  ?= -pthread

#===============================================================================
# Vulkan detection
#===============================================================================
GLSLC := $(shell command -v glslc 2>/dev/null)
VK_LIBS :=
VK_CFLAGS :=

ifneq ($(shell pkg-config --exists vulkan 2>/dev/null && echo yes),)
VK_LIBS := $(shell pkg-config --libs vulkan)
VK_CFLAGS := $(shell pkg-config --cflags vulkan)
else
ifneq ($(shell ldconfig -p 2>/dev/null | grep -q libvulkan && echo yes),)
VK_LIBS := -lvulkan
endif
endif

ifdef GLSLC
$(info [BUILD] glslc detected -- Vulkan shader support)
SHADER_SPV := shaders/chunk_gen.spv
# Only link -lvulkan when the SDK (including headers) is available
VK_LINK := $(VK_LINK)
else
SHADER_SPV :=
VK_LINK :=
endif

#===============================================================================
# Compiler detection
#===============================================================================
NVCC := $(shell command -v nvcc 2>/dev/null)

ifdef NVCC
$(info [BUILD] nvcc detected)
CUFLAGS   := -O3 -arch=sm_35 -std=c++17
NVCC_LIBS := -lcudart

%.o: %.cu generator.h cuda_mock.h
	$(NVCC) $(CUFLAGS) -c $< -o $@

%.o: %.cpp generator.h cuda_mock.h
	$(CXX) $(CXXFLAGS) $(VK_CFLAGS) -c $< -o $@

$(TARGET): $(OBJS) main.cpp
	$(CXX) $(CXXFLAGS) $(OBJS) main.cpp $(LDFLAGS) -o $@

$(TARGET_OFFLINE): $(OBJS) $(OFFLINE_SRC) $(SHADER_SPV)
	$(CXX) $(CXXFLAGS) $(VK_CFLAGS) $(OBJS) $(OFFLINE_SRC) $(LDFLAGS) $(VK_LINK) -o $@

else
$(info [BUILD] nvcc NOT found -- CPU mock mode)

%.o: %.cu generator.h cuda_mock.h
	$(CXX) $(CXXFLAGS) -x c++ -c $< -o $@

%.o: %.cpp generator.h cuda_mock.h
	$(CXX) $(CXXFLAGS) $(VK_CFLAGS) -c $< -o $@

$(TARGET): $(OBJS) main.cpp
	$(CXX) $(CXXFLAGS) $(OBJS) main.cpp $(LDFLAGS) -o $@

$(TARGET_OFFLINE): $(OBJS) $(OFFLINE_SRC) $(SHADER_SPV)
	$(CXX) $(CXXFLAGS) $(VK_CFLAGS) $(OBJS) $(OFFLINE_SRC) $(LDFLAGS) $(VK_LINK) -o $@

endif

#===============================================================================
# Shader compilation
#===============================================================================
ifdef GLSLC

shaders/chunk_gen.spv: shaders/chunk_gen.comp
	$(GLSLC) -O -o $@ $<

endif

#===============================================================================
# Targets
#===============================================================================
.PHONY: all clean test perf gen vulkan-info

all: $(TARGET) $(TARGET_OFFLINE)

clean:
	rm -f $(OBJS) $(TARGET) $(TARGET_OFFLINE) terrain.bin zlib_local.h libz.so
	rm -f shaders/chunk_gen.spv

vulkan-info:
	@echo "glslc:   $(if $(GLSLC),found,NOT FOUND)"
	@echo "Vulkan:  $(if $(VK_LINK),linked ($(VK_LINK)),NOT FOUND)"
	@echo ""

test: $(TARGET)
	./$(TARGET) terrain.bin 42 256
	@echo "OK"

perf: $(TARGET_OFFLINE) $(SHADER_SPV)
	@mkdir -p /tmp/mcchunkgen_bench/world/region
	./$(TARGET_OFFLINE) --world /tmp/mcchunkgen_bench/world --seed 42 --radius 64 --threads 4 --quiet
	@echo ""
	@du -sh /tmp/mcchunkgen_bench/world/region/
	@rm -rf /tmp/mcchunkgen_bench

gen: $(TARGET_OFFLINE) $(SHADER_SPV)
	@if [ -z "$(WORLD)" ]; then echo "Usage: make gen WORLD=/path/to/world [SEED=42] [RADIUS=64] [THREADS=4]"; exit 1; fi
	@mkdir -p $(WORLD)/region
	./$(TARGET_OFFLINE) --world $(WORLD) --seed $(SEED) --radius $(RADIUS) --threads $(THREADS)

