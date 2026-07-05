@echo off
REM ===========================================================================
REM build-no-admin.bat — Windows build script (no admin required)
REM
# Assumes g++ and make are already installed (via Scoop, MSYS2, or manual).
# If you don't have them:
#   Scoop:     irm get.scoop.sh | iex  &&  scoop install gcc make
#   MSYS2:     Download portable from https://github.com/msys2/msys2-installer/releases
# ===========================================================================

setlocal enabledelayedexpansion

echo [BUILD] McChunkGen — Windows Build (No Admin)
echo.

REM ---- Check for g++ ----
where g++ >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] g++ not found!
    echo.
    echo Install one of:
    echo   Scoop:  PowerShell ^> irm get.scoop.sh ^| iex
    echo                   scoop install gcc make
    echo   MSYS2:  https://github.com/msys2/msys2-installer/releases
    echo.
    exit /b 1
)

REM ---- Show compiler version ----
g++ --version | head -1

REM ---- Detect available tools ----
set "MAKE="
where make >nul 2>nul
if %ERRORLEVEL% EQU 0 set "MAKE=yes"

where glslc >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    echo [BUILD] glslc found — compiling Vulkan shader
    mkdir shaders 2>nul
    glslc -O -o shaders\chunk_gen.spv shaders\chunk_gen.comp
) else (
    echo [BUILD] glslc not found — skipping Vulkan shader ^(CPU mode only^)
)

REM ---- Build ----
echo.
echo [BUILD] Compiling generator.cu...
g++ -O3 -march=native -ffast-math -pthread -std=c++17 -x c++ -c generator.cu -o generator.o
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] generator.cu compilation failed
    exit /b 1
)

echo [BUILD] Compiling chunkgen_offline.cpp...
g++ -O3 -march=native -ffast-math -pthread -std=c++17 generator.o chunkgen_offline.cpp -pthread -o chunkgen_offline.exe
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Linking failed
    exit /b 1
)

echo.
echo [BUILD] Done! chunkgen_offline.exe built successfully.
echo.
echo Usage: chunkgen_offline.exe --world C:\path\to\world --seed 42 --radius 64 --threads 4