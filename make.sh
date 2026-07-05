#!/usr/bin/env bash
#===============================================================================
# make.sh — Linux/macOS build via make (admin, installs make if missing)
#===============================================================================
set -euo pipefail

if ! command -v make &>/dev/null; then
    echo "[BUILD] make not found. Installing..."
    if [[ "$(uname)" == "Darwin" ]]; then
        brew install make
    else
        sudo apt update && sudo apt install -y make
    fi
fi

exec make "$@"