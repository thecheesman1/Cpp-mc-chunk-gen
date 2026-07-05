@echo off
REM ===========================================================================
REM build.bat — Windows (admin) build script
REM Uses winget or choco to install dependencies, then builds.
REM Run in a Visual Studio Developer Command Prompt or with MinGW available.
REM ===========================================================================

setlocal enabledelayedexpansion

echo [BUILD] McChunkGen — Windows Build
echo.

REM ---- Check for g++ ----
where g++ >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo [BUILD] g++ not found. Attempting to install MinGW...
    
    REM Try winget first
    where winget >nul 2>nul
    if !ERRORLEVEL! EQU 0 (
        echo [BUILD] Installing MinGW via winget...
        winget install --id=LLVM.LLVM -e --silent 2>nul || winget install --id=Win32.Win32 -e --silent 2>nul
    )
    
    REM Try choco next
    where choco >nul 2>nul
    if !ERRORLEVEL! EQU 0 (
        echo [BUILD] Installing MinGW via choco...
        choco install mingw -y
    )
    
    where g++ >nul 2>nul
    if !ERRORLEVEL! NEQ 0 (
        echo [ERROR] g++ still not found.
        echo Install MinGW manually: https://www.mingw-w64.org/
        echo Or use build-no-admin.bat if g++ is already installed.
        exit /b 1
    )
)

REM ---- Check for make ----
where make >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo [BUILD] make not found. Using g++ directly...
    set "MAKE="
) else (
    set "MAKE=yes"
)

REM ---- Check for glslc (Vulkan shader compiler) ----
where glslc >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    echo [BUILD] glslc found — compiling Vulkan shader
    mkdir shaders 2>nul
    glslc -O -o shaders\chunk_gen.spv shaders\chunk_gen.comp
    set "HAVE_VULKAN=yes"
) else (
    echo [BUILD] glslc not found — Vulkan shader won't be compiled
    echo        The --vulkan flag will fall back to CPU mode.
    set "HAVE_VULKAN="
)

REM ---- Detect MSVC or MinGW ----
cl.exe /? >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    echo [BUILD] MSVC detected — using cl.exe
    set "CXX=cl.exe"
    set "CXXFLAGS=/O2 /std:c++17 /EHsc"
    set "LDFLAGS="
    REM MSVC doesn't have -x c++ for .cu files, rename approach
    echo [ERROR] MSVC build not supported yet. Use MinGW (g++).
    echo Install: winget install --id=LLVM.LLVM -e
    exit /b 1
) else (
    echo [BUILD] Using g++
    set "CXX=g++"
    set "CXXFLAGS=-O3 -march=native -ffast-math -pthread -std=c++17"
    set "LDFLAGS=-pthread"
)

REM ---- Build ----
echo.
echo [BUILD] Compiling generator.cu...
%CXX% %CXXFLAGS% -x c++ -c generator.cu -o generator.o
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] generator.cu compilation failed
    exit /b 1
)

echo [BUILD] Compiling chunkgen_offline.cpp...
%CXX% %CXXFLAGS% generator.o chunkgen_offline.cpp %LDFLAGS% -o chunkgen_offline.exe
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Linking failed
    exit /b 1
)

echo.
echo [BUILD] Done! chunkgen_offline.exe built successfully.
echo.
echo Usage: chunkgen_offline.exe --world C:\path\to\world --seed 42 --radius 64 --threads 4
if defined HAVE_VULKAN (
    echo        chunkgen_offline.exe --vulkan ... (GPU acceleration)
)
echo.