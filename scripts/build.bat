@echo off
REM ============================================================
REM DadBass — Windows CMake + Inno Setup build script
REM Requirements:
REM   - CMake 3.22+ on PATH
REM   - Visual Studio 2022 (or Build Tools) on PATH  
REM   - JUCE cloned to DadBass\JUCE\
REM   - Inno Setup 6 installed (iscc on PATH) for installer
REM ============================================================

setlocal enabledelayedexpansion

set PROJECT_DIR=%~dp0..
set BUILD_DIR=%PROJECT_DIR%\Build
set ARTEFACTS=%BUILD_DIR%\DadBass_artefacts\Release

echo.
echo ==^> DadBass Windows Build
echo     Project : %PROJECT_DIR%
echo     Build   : %BUILD_DIR%
echo.

REM ── Check JUCE ─────────────────────────────────────────────
if not exist "%PROJECT_DIR%\JUCE\CMakeLists.txt" (
    echo [ERROR] JUCE not found at %PROJECT_DIR%\JUCE\
    echo         Run: git clone https://github.com/juce-framework/JUCE.git "%PROJECT_DIR%\JUCE"
    exit /b 1
)

REM ── Configure ──────────────────────────────────────────────
echo ==^> Configuring...
cmake -S "%PROJECT_DIR%" ^
      -B "%BUILD_DIR%" ^
      -G "Visual Studio 17 2022" ^
      -A x64 ^
      -DJUCE_BUILD_EXAMPLES=OFF ^
      -DJUCE_BUILD_EXTRAS=OFF
if errorlevel 1 ( echo [ERROR] CMake configure failed & exit /b 1 )

REM ── Build ───────────────────────────────────────────────────
echo.
echo ==^> Building Release...
cmake --build "%BUILD_DIR%" --config Release --parallel
if errorlevel 1 ( echo [ERROR] Build failed & exit /b 1 )

REM ── Report artefacts ────────────────────────────────────────
echo.
echo ==^> Build complete. Artefacts:
if exist "%ARTEFACTS%\VST3\DadBass.vst3"          echo     VST3:       %ARTEFACTS%\VST3\DadBass.vst3
if exist "%ARTEFACTS%\Standalone\DadBass.exe"      echo     Standalone: %ARTEFACTS%\Standalone\DadBass.exe

REM ── Inno Setup installer ────────────────────────────────────
echo.
echo ==^> Building Windows installer...
where iscc >nul 2>&1
if errorlevel 1 (
    echo [WARN] iscc not found on PATH. Skipping installer.
    echo        Install Inno Setup 6 from https://jrsoftware.org/isinfo.php
) else (
    iscc "%PROJECT_DIR%\Installer\Windows\DadBass_Installer.iss"
    if errorlevel 1 ( echo [ERROR] Installer build failed ) else (
        echo     Installer: %PROJECT_DIR%\Installer\Windows\Output\DadBass_Setup_1.0.0_Win64.exe
    )
)

echo.
echo Done.
endlocal
