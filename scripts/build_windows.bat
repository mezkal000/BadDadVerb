@echo off
REM Build BadDadVerb on Windows
REM Run from Developer Command Prompt for VS 2022
cd /d "%~dp0\.."

echo =^> Configuring...
cmake -B Build -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 goto fail

echo =^> Building...
cmake --build Build --config Release
if errorlevel 1 goto fail

set DST="%CommonProgramFiles%\VST3"
set SRC=Build\BadDadVerb_artefacts\Release\VST3\BadDadVerb.vst3
if exist "%SRC%" (
    echo =^> Installing VST3...
    xcopy /E /I /Y "%SRC%" "%DST%\BadDadVerb.vst3\"
    echo =^> Installed to %DST%
) else (
    echo WARNING: VST3 not found at %SRC%
)
echo =^> Done. Rescan plugins in your DAW.
goto end

:fail
echo =^> BUILD FAILED
exit /b 1

:end
