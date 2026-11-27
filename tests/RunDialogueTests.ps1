param([switch]$Transport, [switch]$Live)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$vswherePath = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$visualStudio = & $vswherePath -latest -version '[17.0,18.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $visualStudio) { throw 'Visual Studio 2022 C++ tools (v143) were not found.' }
$vcvarsPath = Join-Path $visualStudio 'VC\Auxiliary\Build\vcvars64.bat'
$sdkRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Include'
$sdk = Get-ChildItem -LiteralPath $sdkRoot -Directory | Where-Object { $_.Name -match '^\d+\.\d+\.\d+\.\d+$' -and (Test-Path (Join-Path $_.FullName 'cppwinrt\winrt\base.h')) } | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
if (-not $sdk) { throw 'Windows SDK / C++ WinRT headers were not found.' }
Push-Location -LiteralPath $projectRoot
try {
    New-Item -ItemType Directory -Path '.codex-build' -Force | Out-Null
    $compileCommand = '"' + $vcvarsPath + '" >nul && cl /nologo /EHsc /std:c++17 /utf-8 /I"' + (Join-Path $sdk.FullName 'cppwinrt') + '" /Fe:.codex-build\DialogueTests.exe /Fo:.codex-build\ /Fd:.codex-build\dialogue-test.pdb AI\DialogueProtocol.cpp AI\OllamaClient.cpp AI\DialogueSession.cpp tests\DialogueTests.cpp && .codex-build\DialogueTests.exe'
    & $env:ComSpec /d /s /c $compileCommand
    if ($LASTEXITCODE -ne 0) { throw 'C++ dialogue tests failed.' }
    if ($Transport) {
        & python tests\test_dialogue_transport.py
        if ($LASTEXITCODE -ne 0) { throw 'HTTP fixture tests failed.' }
    }
    if ($Live) {
        & '.\.codex-build\DialogueTests.exe' --live
        if ($LASTEXITCODE -ne 0) { throw 'Live Ollama check failed.' }
    }
} finally { Pop-Location }
