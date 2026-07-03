#===============================================================================
# Makefile — GPU-Accelerated Chunk Generator
#
# Auto-detects nvcc.  If nvcc is not found, falls back to g++ with the CUDA
# mock layer (cuda_mock.h) for CPU-only testing.
#===============================================================================

SHELL := /bin/bash

# Source files
CU_SRC    := generator.cu
CPP_SRC   := main.cpp
OFFLINE_SRC := chunkgen_offline.cpp
OBJS      := generator.o
TARGET    := chunkgen
TARGET_OFFLINE := chunkgen_offline

# Flags
CXX      ?= g++
CXXFLAGS ?= -O3 -march=native -ffast-math -pthread -std=c++17
LDFLAGS  ?= -pthread

#===============================================================================
# Compiler detection
#===============================================================================
NVCC := $(shell command -v nvcc 2>/dev/null)

ifdef NVCC
# ── Real CUDA mode 
$(info [BUILD] nvcc detected)
CUFLAGS   := -O3 -arch=sm_35 -std=c++17
NVCC_LIBS := -lcudart
%.o: %.cu generator.h cuda_mock.h
	$(NVCC) $(CUFLAGS) -c $< -o $@
%.o: %.cpp generator.h cuda_mock.h
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(TARGET): $(OBJS) main.cpp
	$(CXX) $(CXXFLAGS) $(OBJS) main.cpp $(LDFLAGS) -o $@
$(TARGET_OFFLINE): $(OBJS) $(OFFLINE_SRC)
	$(CXX) $(CXXFLAGS) $(OBJS) $(OFFLINE_SRC) $(LDFLAGS) -o $@
else
# ── CPU-only mock mode 
$(info [BUILD] nvcc NOT found — CPU mock mode)
%.o: %.cu generator.h cuda_mock.h
	$(CXX) $(CXXFLAGS) -x c++ -c $< -o $@
%.o: %.cpp generator.h cuda_mock.h
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(TARGET): $(OBJS) main.cpp
	$(CXX) $(CXXFLAGS) $(OBJS) main.cpp $(LDFLAGS) -o $@
$(TARGET_OFFLINE): $(OBJS) $(OFFLINE_SRC)
	$(CXX) $(CXXFLAGS) $(OBJS) $(OFFLINE_SRC) $(LDFLAGS) -o $@
endif

#===============================================================================
# Targets
#===============================================================================
.PHONY: all clean test perf gen
all: $(TARGET) $(TARGET_OFFLINE)
clean:
	rm -f $(OBJS) $(TARGET) $(TARGET_OFFLINE) terrain.bin zlib_local.h libz.so
test: $(TARGET)
	./$(TARGET) terrain.bin 42 256
	@echo "OK"
perf: $(TARGET_OFFLINE)
	@mkdir -p /tmp/mcchunkgen_bench/world/region
	./$(TARGET_OFFLINE) --world /tmp/mcchunkgen_bench/world --seed 42 --radius 64 --threads 4 --quiet
	@echo ""
	@du -sh /tmp/mcchunkgen_bench/world/region/
	@rm -rf /tmp/mcchunkgen_bench
gen: $(TARGET_OFFLINE)
	@if [ -z "$(WORLD)" ]; then echo "Usage: make gen WORLD=/path/to/world [SEED=42] [RADIUS=64] [THREADS=4]"; exit 1; fi
	@mkdir -p $(WORLD)/region
	./$(TARGET_OFFLINE) --world $(WORLD) --seed $(SEED) --radius $(RADIUS) --threads $(THREADS)
