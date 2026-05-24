@echo off
REM build_all.bat — runs codegen scripts (when present), builds release + debug
REM + native_test, then runs unit tests. Fails fast.
setlocal enableextensions enabledelayedexpansion

REM Make PlatformIO's bundled pio.exe reachable without a global PATH change.
if exist "%USERPROFILE%\.platformio\penv\Scripts\pio.exe" set "PATH=%USERPROFILE%\.platformio\penv\Scripts;%PATH%"

pushd "%~dp0\.."

if exist scripts\wav_to_cpp.py (
    echo === wav_to_cpp.py ===
    python scripts\wav_to_cpp.py
    if errorlevel 1 goto :fail
)

if exist scripts\midi_to_cpp.py (
    echo === midi_to_cpp.py ===
    python scripts\midi_to_cpp.py
    if errorlevel 1 goto :fail
)

echo === build release ===
pio run -e release
if errorlevel 1 goto :fail

echo === build debug ===
pio run -e debug
if errorlevel 1 goto :fail

echo === native_test (build + run unit tests) ===
pio test -e native_test
if errorlevel 1 goto :fail

echo.
echo === ALL GREEN ===
popd
endlocal
exit /b 0

:fail
echo.
echo === BUILD FAILED ===
popd
endlocal
exit /b 1
