@echo off
@REM VBWR B
@REM
@REM Project: ESP32 Luna Shell
@REM Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
@REM Creator: Umberto Giacobbi | https://umbertogiacobbi.biz
@REM
@REM VibeWare: Human intent, AI execution, and plenty of tokens
@REM Manifesto: https://umbertogiacobbi.biz/vibeware/manifesto
@REM
@REM Modified with AI: OpenAI Codex; added VibeWare branding on 2026-10-10.
@REM Human guidance: Umberto Giacobbi; requested VibeWare branding.
@REM
@REM Copyright (c) 2026 Umberto Giacobbi
@REM
@REM VBWR E

setlocal enabledelayedexpansion

:: Preflight checks
echo Preflight checks...

:: Check for CMakeLists.txt
if not exist "CMakeLists.txt" (
    echo [ERROR] CMakeLists.txt not found in current directory
    pause
    exit /b 1
)

:: Check for main CMakeLists
if not exist "main\CMakeLists.txt" (
    echo [ERROR] main/CMakeLists.txt not found
    pause
    exit /b 1
)

:: Check for components directory
if not exist "components" (
    echo [WARNING] components directory not found
)

echo Preflight checks passed!

:: Launch VS Code in a new window (non-blocking)
start "" code .

:: Small delay to ensure code starts properly
timeout /t 1 /nobreak

:: Exit and close terminal window
exit /b 0