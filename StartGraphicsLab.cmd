@echo off
setlocal
pushd "%~dp0" || exit /b 1
set "labExe=x64\Release\GM31_Game.exe"
if not exist "%labExe%" set "labExe=x64\Debug\GM31_Game.exe"
if not exist "%labExe%" (
    echo Build GM31_Game.sln for x64 before starting Graphics Lab.
    popd
    exit /b 1
)
start "" "%CD%\%labExe%" --graphics-lab
popd
