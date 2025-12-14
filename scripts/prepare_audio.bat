@echo off
REM Helper script to convert audio files to WAV format compatible with ESP32-P4
REM Requires: ffmpeg (install from https://ffmpeg.org/download.html)

setlocal enabledelayedexpansion

REM Default settings
set SAMPLE_RATE=44100
set CHANNELS=1
set BIT_DEPTH=16
set OUTPUT_DIR=wav_output

REM Check if ffmpeg is installed
where ffmpeg >nul 2>nul
if %ERRORLEVEL% neq 0 (
    echo Error: ffmpeg is not installed or not in PATH
    echo Download from: https://ffmpeg.org/download.html
    echo Add ffmpeg.exe to your PATH environment variable
    pause
    exit /b 1
)

REM Check if any arguments provided
if "%~1"=="" (
    echo Usage: %~nx0 [audio_files]
    echo.
    echo Convert audio files to WAV format compatible with ESP32-P4
    echo.
    echo Examples:
    echo   %~nx0 music.mp3
    echo   %~nx0 *.mp3
    echo   %~nx0 audio\*.mp3
    echo.
    pause
    exit /b 1
)

REM Create output directory
if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%"

echo ===================================
echo Audio Conversion for ESP32-P4
echo ===================================
echo Sample Rate:  %SAMPLE_RATE% Hz
echo Channels:     %CHANNELS% (mono)
echo Bit Depth:    %BIT_DEPTH%-bit
echo Output Dir:   %OUTPUT_DIR%
echo ===================================
echo.

set TOTAL=0
set SUCCESS=0
set FAILED=0

REM Process each file
:process_files
if "%~1"=="" goto summary

set TOTAL=!TOTAL:~0,-1!1
set "input_file=%~1"

REM Check if file exists
if not exist "!input_file!" (
    echo [SKIP] !input_file! (not found)
    set /a FAILED+=1
    shift
    goto process_files
)

REM Get filename without extension
for %%F in ("!input_file!") do (
    set "filename=%%~nF"
)

set "output_file=%OUTPUT_DIR%\!filename!.wav"

echo | set /p="Converting: !input_file! ... "

REM Convert file
ffmpeg -i "!input_file!" -acodec pcm_s%BIT_DEPTH%le -ar %SAMPLE_RATE% -ac %CHANNELS% -y "!output_file!" -loglevel error >nul 2>&1

if !ERRORLEVEL! equ 0 (
    echo [OK]
    set /a SUCCESS+=1
) else (
    echo [FAIL]
    set /a FAILED+=1
)

shift
goto process_files

:summary
echo.
echo ===================================
echo Conversion Complete
echo ===================================
echo Total:    %TOTAL% files
echo Success:  %SUCCESS% files
echo Failed:   %FAILED% files
echo ===================================
echo.

if %SUCCESS% gtr 0 (
    echo Converted files are in: %OUTPUT_DIR%
    echo Copy these .wav files to your SD card root directory.
    echo.
    echo Don't forget to create media.txt to map images to audio files!
)

pause
