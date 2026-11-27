@echo off
setlocal
pushd "%~dp0" || exit /b 1

set "gameExe=x64\Debug\GM31_Game.exe"
if not exist "%gameExe%" set "gameExe=x64\Release\GM31_Game.exe"
if not exist "%gameExe%" (
    echo Build GM31_Game.sln for x64 first.
    pause
    popd
    exit /b 1
)

start "" "%CD%\%gameExe%" --rural-map
set "startResult=%errorlevel%"
popd
exit /b %startResult%
