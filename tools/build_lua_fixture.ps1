param()
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$vswhere="${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$vsRoot=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$compilerVersion=(Get-Content (Join-Path $vsRoot 'VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt')).Trim()
$compilerRoot=Join-Path $vsRoot "VC/Tools/MSVC/$compilerVersion"
$sdkRoot="${env:ProgramFiles(x86)}/Windows Kits/10"
$sdkVersion=(Get-ChildItem "$sdkRoot/Lib" -Directory | Sort-Object {[version]$_.Name} -Descending | Select-Object -First 1).Name
$env:PATH="$compilerRoot/bin/Hostx64/x64;$env:PATH"
$env:INCLUDE="$compilerRoot/include;$sdkRoot/Include/$sdkVersion/ucrt;$sdkRoot/Include/$sdkVersion/shared;$sdkRoot/Include/$sdkVersion/um"
$env:LIB="$compilerRoot/lib/x64;$sdkRoot/Lib/$sdkVersion/ucrt/x64;$sdkRoot/Lib/$sdkVersion/um/x64"
$luaSource=Join-Path $projectRoot 'work/lua-5.1.5/src'
$fixtureOutput=Join-Path $projectRoot 'build/lua-fixture'
New-Item -ItemType Directory -Path $fixtureOutput -Force | Out-Null
$sources=@(Get-ChildItem -LiteralPath $luaSource -Filter '*.c' -File | Where-Object {$_.Name -notin @('luac.c','print.c')} | ForEach-Object FullName)
if($sources.Count -lt 25){throw 'Pinned Lua 5.1.5 source is missing'}
& cl /nologo /O2 /MT /D_CRT_SECURE_NO_WARNINGS "/I$luaSource" "/Fo$fixtureOutput/" "/Fe$fixtureOutput/lua51.exe" @sources
if($LASTEXITCODE -ne 0){throw 'Lua fixture build failed'}
