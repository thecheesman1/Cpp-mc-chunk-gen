@echo off
REM ===========================================================================
REM make-no-admin.bat — Windows build via make (no admin, assumes make exists)
REM Run: make-no-admin.bat [target]
REM E.g.: make-no-admin.bat chunkgen_offline
REM ===========================================================================
setlocal

where make >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] make not found!
    echo Install via Scoop: scoop install make
    echo Or use build-no-admin.bat which uses g++ directly.
    exit /b 1
)

make %*