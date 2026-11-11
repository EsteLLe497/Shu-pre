@echo off
setlocal

rem 素材の相対パスが通るように、このファイルのあるフォルダーで起動する。
pushd "%~dp0" || exit /b 1

set "previewExe=x64\Debug\GM31_Game.exe"
if not exist "%previewExe%" set "previewExe=x64\Release\GM31_Game.exe"
if not exist "%previewExe%" (
    echo GM31_Game.exe was not found. Build the x64 project in Visual Studio first.
    pause
    popd
    exit /b 1
)

start "" "%CD%\%previewExe%" --preview
if errorlevel 1 (
    echo Could not start GM31 Asset Preview.
    pause
    popd
    exit /b 1
)

popd
