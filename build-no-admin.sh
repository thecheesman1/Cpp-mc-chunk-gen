#!/usr/bin/env bash
#===============================================================================
# build-no-admin.sh — Linux/macOS build script (no sudo required)
#
# Assumes g++ and make are already installed.
# If glslc (Vulkan shader compiler) is not found, skips Vulkan and builds
# in CPU-only mode — the --vulkan flag will fall back to CPU gracefully.
#===============================================================================
set -euo pipefail

echo "[BUILD] McChunkGen — Linux/macOS Build (No Admin)"
echo

# ---- Check prerequisites ----
command -v g++ >/dev/null 2>&1 || { echo "[ERROR] g++ not found. Install build-essential (apt) or gcc (brew)."; exit 1; }
command -v make >/dev/null 2>&1 || echo "[WARN] make not found — will compile directly with g++"

g++ --version | head -1

# ---- Check for glslc (Vulkan) ----
if command -v glslc &>/dev/null; then
    echo "[BUILD] glslc found — compiling Vulkan shader"
    mkdir -p shaders
    glslc -O -o shaders/chunk_gen.spv shaders/chunk_gen.comp
else
    echo "[BUILD] glslc not found — skipping Vulkan shader (CPU mode only)"
fi

# ---- Build ----
echo
echo "[BUILD] Compiling generator.cu..."
g++ -O3 -march=native -ffast-math -pthread -std=c++17 -x c++ -c generator.cu -o generator.o

echo "[BUILD] Compiling chunkgen_offline..."
g++ -O3 -march=native -ffast-math -pthread -std=c++17 generator.o chunkgen_offline.cpp -pthread -o chunkgen_offline

echo
echo "[BUILD] Done! ./chunkgen_offline built successfully."
echo
echo "Usage: ./chunkgen_offline --world /path/to/world --seed 42 --radius 64 --threads 4"