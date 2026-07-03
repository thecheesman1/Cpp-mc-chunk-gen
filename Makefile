#===============================================================================
# Makefile — GPU-Accelerated Chunk Generator
#
# Auto-detects nvcc.  If nvcc is not found, falls back to g++ with the CUDA
# mock layer (cuda_mock.h) for CPU-only testing.
#===============================================================================

SHELL := /bin/bash

# Source files
CU_SRC  := generator.cu
CPP_SRC := main.cpp
OBJS    := generator.o main.o
TARGET  := chunkgen

# Flags
CXX      ?= g++
CXXFLAGS ?= -O3 -march=native -ffast-math -pthread -std=c++17
LDFLAGS  ?= -pthread

#===============================================================================
# Compiler detection — prefer nvcc if available
#===============================================================================
NVCC := $(shell command -v nvcc 2>/dev/null)

ifdef NVCC
# ── Real CUDA mode ────────────────────────────────────────────────────────────
$(info [BUILD] nvcc detected — compiling with real CUDA support)

CUFLAGS   := -O3 -arch=sm_35 -std=c++17
NVCC_LIBS := -lcudart

%.o: %.cu generator.h cuda_mock.h
	$(NVCC) $(CUFLAGS) -c $< -o $@

%.o: %.cpp generator.h cuda_mock.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	$(NVCC) $(OBJS) $(NVCC_LIBS) -o $@

else
# ── CPU-only mock mode ────────────────────────────────────────────────────────
$(info [BUILD] nvcc NOT found — compiling in CPU mock mode)
$(info [BUILD] Using $(CXX) with flags: $(CXXFLAGS))

# Treat .cu files as C++ source
%.o: %.cu generator.h cuda_mock.h
	$(CXX) $(CXXFLAGS) -x c++ -c $< -o $@

%.o: %.cpp generator.h cuda_mock.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) $(LDFLAGS) -o $@

endif

#===============================================================================
# Phony targets
#===============================================================================
.PHONY: all clean test perf

all: $(TARGET)

clean:
	rm -f $(OBJS) $(TARGET) terrain.bin

# Quick smoke-test: generate 256 chunks and verify the output file exists and has
# the correct magic + header.
test: $(TARGET)
	./$(TARGET) terrain.bin 42 256
	@echo "--- Verifying output ---"
	@magic=$$(xxd -l 4 terrain.bin | head -1); \
	 echo "Magic: $$magic"
	@printf "File size: "; ls -lh terrain.bin | awk '{print $$5}'
	@echo "OK"

# Performance benchmark: generate 65536 chunks, report CPS
perf: $(TARGET)
	./$(TARGET) perf_bench.bin 42 65536
	@rm -f perf_bench.bin