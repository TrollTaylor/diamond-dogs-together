# Standalone build of the Lua 5.1.5 fixture interpreter (build/lua-fixture/lua51.exe)
# for manual checks. tools/build.ps1 now also builds lua51 through CMake
# (cmake/lua_checks.cmake, build/native/lua-fixture/lua51.exe) and runs the Lua
# checks in CTest. Source: -LuaSourceDir, else $env:MGS_LUA_SOURCE_DIR, else
# <project>/work/lua-5.1.5 (the previous fixed location).
param([string]$LuaSourceDir = '')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
if(!$LuaSourceDir){$LuaSourceDir=$env:MGS_LUA_SOURCE_DIR}
if(!$LuaSourceDir){$LuaSourceDir=Join-Path $projectRoot 'work/lua-5.1.5'}
$vswhere="${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$vsRoot=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$compilerVersion=(Get-Content (Join-Path $vsRoot 'VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt')).Trim()
$compilerRoot=Join-Path $vsRoot "VC/Tools/MSVC/$compilerVersion"
$sdkRoot="${env:ProgramFiles(x86)}/Windows Kits/10"
$sdkVersion=(Get-ChildItem "$sdkRoot/Lib" -Directory | Sort-Object {[version]$_.Name} -Descending | Select-Object -First 1).Name
$env:PATH="$compilerRoot/bin/Hostx64/x64;$env:PATH"
$env:INCLUDE="$compilerRoot/include;$sdkRoot/Include/$sdkVersion/ucrt;$sdkRoot/Include/$sdkVersion/shared;$sdkRoot/Include/$sdkVersion/um"
$env:LIB="$compilerRoot/lib/x64;$sdkRoot/Lib/$sdkVersion/ucrt/x64;$sdkRoot/Lib/$sdkVersion/um/x64"
$luaSource=Join-Path $LuaSourceDir 'src'
$luaHeader=Join-Path $luaSource 'lua.h'
if(!(Test-Path -LiteralPath $luaHeader) -or !(Select-String -LiteralPath $luaHeader -Pattern '^#define\s+LUA_RELEASE\s+"Lua 5\.1\.5"' -Quiet)){
    throw "Pinned Lua 5.1.5 source is missing: $LuaSourceDir"
}
$fixtureOutput=Join-Path $projectRoot 'build/lua-fixture'
New-Item -ItemType Directory -Path $fixtureOutput -Force | Out-Null
$sources=@(Get-ChildItem -LiteralPath $luaSource -Filter '*.c' -File | Where-Object {$_.Name -notin @('luac.c','print.c')} | ForEach-Object FullName)
if($sources.Count -lt 25){throw 'Pinned Lua 5.1.5 source is missing'}
& cl /nologo /O2 /MT /D_CRT_SECURE_NO_WARNINGS "/I$luaSource" "/Fo$fixtureOutput/" "/Fe$fixtureOutput/lua51.exe" @sources
if($LASTEXITCODE -ne 0){throw 'Lua fixture build failed'}
