@echo off
rem =========================================================
rem ファイルの情報[StartGraphicsLab.cmd]
rem
rem 制作者:ozawa souma        日付:2026/11/28
rem =========================================================
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
