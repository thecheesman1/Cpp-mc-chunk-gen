@echo off
REM ==============================================================================
REM benchmark.bat — Run chunkgen_offline.exe N times and average the results
REM
REM Usage:
REM   benchmark.bat [--times N] [chunkgen_offline args...]
REM
REM Examples:
REM   benchmark.bat --radius 64 --threads 4
REM   benchmark.bat --times 5 --radius 128 --threads 4
REM ==============================================================================
SETLOCAL ENABLEDELAYEDEXPANSION ENABLEEXTENSIONS

SET "BIN=chunkgen_offline.exe"
SET "TIMES=4"
SET "BENCH_ARGS="

REM ---- Parse --times before forwarding rest ----
:parse
IF "%~1"=="" GOTO :endparse
IF /I "%~1"=="--times" (
    SET "TIMES=%~2"
    SHIFT
    SHIFT
    GOTO :parse
)
IF "%~1"=="--times=*" (
    SET "TIMES=%~1"
    SET "TIMES=!TIMES:--times==!"
    SHIFT
    GOTO :parse
)
SET "BENCH_ARGS=!BENCH_ARGS! %~1"
SHIFT
GOTO :parse
:endparse

REM ---- Check binary exists ----
IF NOT EXIST "%BIN%" (
    ECHO [BENCH] ERROR: %BIN% not found. Run 'make chunkgen_offline' or 'build.bat' first.
    EXIT /B 1
)

ECHO ==============================================
ECHO   McChunkGen Benchmark
ECHO   Runs  : %TIMES%
ECHO   Args  :%BENCH_ARGS%
ECHO ==============================================
ECHO.

SET /A RUN=1
SET "RESULTS="
SET /A VALID_COUNT=0
SET "TEMPDIR=%TEMP%\mcchunkgen_bench"

:RUNLOOP
IF %RUN% GTR %TIMES% GOTO :DONE_LOOP

SET "WORLDDIR=%TEMPDIR%\run_%RUN%\world"
IF NOT EXIST "%WORLDDIR%\region" MKDIR "%WORLDDIR%\region"

REM Run the benchmark
FOR /F "tokens=*" %%A IN ('%BIN% --world "%WORLDDIR%" %BENCH_ARGS% 2^>^&1') DO SET "OUTPUT=%%A"
REM Need to capture multi-line output properly — use temp file
%BIN% --world "%WORLDDIR%" %BENCH_ARGS% > "%TEMPDIR%\output_%RUN%.txt" 2>&1
SET /P CPS=<NUL

REM Find CPS in output — match "Done!" line for "(XXXXX CPS)"
FOR /F "tokens=*" %%A IN ('FINDSTR /C:"Done!" "%TEMPDIR%\output_%RUN%.txt"') DO (
    SET "DONELINE=%%A"
)
REM Parse: "Done! N chunks in X.Xs (Y CPS)" — grab Y
FOR /F "tokens=4 delims=() " %%A IN ("%DONELINE%") DO SET "CPS=%%A"

IF "%CPS%"=="" (
    ECHO [BENCH] Run !RUN!: FAILED
    TYPE "%TEMPDIR%\output_!RUN!.txt" | FINDSTR /V "^$" | MORE
) ELSE (
    SET "RESULTS=!RESULTS! !CPS!"
    SET /A VALID_COUNT+=1
    ECHO   [Run !RUN!/%TIMES%]  !CPS! CPS
)

REM Clean up
IF EXIST "%WORLDDIR%" RMDIR /S /Q "%WORLDDIR%" >NUL 2>&1
IF EXIST "%TEMPDIR%\output_%RUN%.txt" DEL /Q "%TEMPDIR%\output_%RUN%.txt" >NUL 2>&1

SET /A RUN+=1
GOTO :RUNLOOP

:DONE_LOOP
ECHO.
ECHO ----------------------------------------------

IF %VALID_COUNT% EQU 0 (
    ECHO [BENCH] No valid runs to average.
    EXIT /B 1
)

REM Parse results into an array-like list
SET /A INDEX=0
SET /A SUM=0
SET /A MIN=99999999
SET /A MAX=0
FOR %%C IN (%RESULTS%) DO (
    SET /A SUM+=%%C
    IF %%C LSS !MIN! SET /A MIN=%%C
    IF %%C GTR !MAX! SET /A MAX=%%C
    SET /A ARR_!INDEX!=%%C
    SET /A INDEX+=1
)
SET /A AVG=SUM / VALID_COUNT

REM Stddev
SET /A DEVSUM=0
FOR %%C IN (%RESULTS%) DO (
    SET /A D=%%C - AVG
    SET /A DEVSUM+=D * D
)
SET /A STDEV=DEVSUM / VALID_COUNT

ECHO   Runs     : %VALID_COUNT%
ECHO   Avg      : %AVG% CPS
ECHO   Min      : %MIN% CPS
ECHO   Max      : %MAX% CPS
ECHO   Stddev   : +- %STDEV% CPS
SET /A RANGE_LOW=%MIN%-%AVG%
SET /A RANGE_HIGH=%MAX%-%AVG%
ECHO   Range    : %MIN% - %MAX% CPS
ECHO ----------------------------------------------

ENDLOCAL