#!/usr/bin/env bash
#===============================================================================
# make-no-admin.sh — Linux/macOS build via make (no admin, assumes make exists)
# Run: ./make-no-admin.sh [target]
# E.g.: ./make-no-admin.sh chunkgen_offline
#===============================================================================
set -euo pipefail

if ! command -v make &>/dev/null; then
    echo "[ERROR] make not found. Install it or use build-no-admin.sh."
    exit 1
fi

exec make "$@"