@echo off
REM Uploads the debug build by default. Pass an env name to override:
REM   scripts\flash.bat release
setlocal
if exist "%USERPROFILE%\.platformio\penv\Scripts\pio.exe" set "PATH=%USERPROFILE%\.platformio\penv\Scripts;%PATH%"
pushd "%~dp0\.."
set ENV=%1
if "%ENV%"=="" set ENV=debug
pio run -e %ENV% -t upload
set RC=%ERRORLEVEL%
popd
endlocal & exit /b %RC%
