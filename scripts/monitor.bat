@echo off
REM Serial monitor at 115200 with log2file + time filters (per debug env).
setlocal
if exist "%USERPROFILE%\.platformio\penv\Scripts\pio.exe" set "PATH=%USERPROFILE%\.platformio\penv\Scripts;%PATH%"
pushd "%~dp0\.."
pio device monitor -e debug --baud 115200
set RC=%ERRORLEVEL%
popd
endlocal & exit /b %RC%
