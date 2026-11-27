$ErrorActionPreference = 'Stop'
$vswherePath = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$visualStudio = & $vswherePath -latest -version '[17.0,18.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $visualStudio) { throw 'Visual Studio 2022 C++ tools (v143) were not found.' }
$vcvarsPath = Join-Path $visualStudio 'VC\Auxiliary\Build\vcvars64.bat'
Push-Location -LiteralPath (Split-Path -Parent $PSScriptRoot)
try {
    New-Item -ItemType Directory -Path '.codex-build' -Force | Out-Null
    $compileCommand = '"' + $vcvarsPath + '" >nul && cl /nologo /EHsc /std:c++17 /utf-8 /Fe:.codex-build\SubtitleLayoutTests.exe /Fo:.codex-build\ /Fd:.codex-build\subtitle-test.pdb AI\SubtitleLayout.cpp tests\SubtitleLayoutTests.cpp && .codex-build\SubtitleLayoutTests.exe'
    & $env:ComSpec /d /s /c $compileCommand
    if ($LASTEXITCODE -ne 0) { throw 'Subtitle tests failed.' }
} finally { Pop-Location }
