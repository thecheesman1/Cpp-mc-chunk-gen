#!/usr/bin/env bash
#===============================================================================
# build.sh — Linux/macOS build script (with sudo for dependency install)
#
# Installs build-essential + glslc via apt/brew, then builds the project.
#===============================================================================
set -euo pipefail

echo "[BUILD] McChunkGen — Linux/macOS Build"
echo

# ---- Detect OS ----
if [[ "$(uname)" == "Darwin" ]]; then
    PKG_MGR="brew"
    PKGS=("gcc" "make" "shaderc")
    GLC="glslc"
else
    PKG_MGR="apt"
    PKGS=("build-essential" "g++" "make")
    GLC="glslc"
fi

# ---- Install missing tools ----
if ! command -v g++ &>/dev/null; then
    echo "[BUILD] Installing g++..."
    if [[ "$PKG_MGR" == "apt" ]]; then
        sudo apt update && sudo apt install -y "${PKGS[@]}"
    else
        brew install "${PKGS[@]}"
    fi
fi

if ! command -v "$GLC" &>/dev/null; then
    echo "[BUILD] Installing glslc (Vulkan shader compiler)..."
    if [[ "$PKG_MGR" == "apt" ]]; then
        sudo apt install -y glslang-tools 2>/dev/null || \
        sudo apt install -y shaderc 2>/dev/null || \
        echo "[BUILD] glslc not in apt. Install Vulkan SDK from https://vulkan.lunarg.com/"
    else
        brew install shaderc
    fi
fi

# ---- Compile the Makefile ----
make clean 2>/dev/null || true
make chunkgen_offline -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

echo
echo "[BUILD] Done! ./chunkgen_offline built successfully."
echo
echo "Usage: ./chunkgen_offline --world /path/to/world --seed 42 --radius 64 --threads 4"
echo "       ./chunkgen_offline --vulkan ... (GPU acceleration if glslc + libvulkan available)"