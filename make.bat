@echo off
REM ===========================================================================
REM make.bat — Windows build via make (admin variant, installs make if missing)
REM ===========================================================================
setlocal enabledelayedexpansion

where make >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo [BUILD] make not found. Installing via winget...
    where winget >nul 2>nul
    if !ERRORLEVEL! EQU 0 (
        winget install GnuWin32.Make 2>nul || winget install ezwinports.make 2>nul
    )
    where make >nul 2>nul
    if !ERRORLEVEL! NEQ 0 (
        echo [ERROR] make still not found. Install it, or use build.bat which uses g++ directly.
        exit /b 1
    )
)

make %*