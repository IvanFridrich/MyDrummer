@echo off
setlocal
if exist "%USERPROFILE%\.platformio\penv\Scripts\pio.exe" set "PATH=%USERPROFILE%\.platformio\penv\Scripts;%PATH%"
pushd "%~dp0\.."
pio test -e native_test --without-uploading --without-testing
set RC=%ERRORLEVEL%
popd
endlocal & exit /b %RC%
