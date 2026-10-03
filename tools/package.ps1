param([Parameter(Mandatory)][string]$RuntimeArchive)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$version='0.10.33'
$dll=Join-Path $projectRoot 'build/native/dinput8.dll'
$archive=(Resolve-Path -LiteralPath $RuntimeArchive).Path
if(!(Test-Path -LiteralPath $archive -PathType Leaf)) { throw "Prepared local runtime archive not found: $archive" }
if((Get-FileHash -LiteralPath $archive).Hash -ne '26CE7C2168D655C35BE39E4F17A295206AA0522DE8782CD57A36C4FEEA382F54') { throw 'Expected two-player runtime archive hash does not match.' }
& (Join-Path $PSScriptRoot 'build.ps1') -Configuration Release -BuildOnly
$stamp=[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8)
$distRoot=Join-Path $projectRoot 'dist'
$packageRoot=Join-Path $distRoot "mgs-coop-$version-$stamp"
$null=New-Item -ItemType Directory -Path $packageRoot
$null=New-Item -ItemType Directory -Path (Join-Path $packageRoot 'payload')
$inputs=@(
    @{path=$archive;name='mgs-coop-player-runtime.dat';target='mgs-coop/mgs-coop-player-runtime.dat'},
    @{path=(Join-Path $projectRoot 'third_party/dynamite/LICENSE');name='DYNAMITE-LICENSE';target='mgs-coop/DYNAMITE-LICENSE'},
    @{path=(Join-Path $projectRoot 'third_party/minhook/LICENSE.txt');name='MinHook-LICENSE.txt';target='mgs-coop/MinHook-LICENSE.txt'},
    @{path=(Join-Path $projectRoot 'third_party/tpp-mod/LICENSE');name='tpp-mod-LICENSE';target='mgs-coop/tpp-mod-LICENSE'},
    @{path=(Join-Path $projectRoot 'THIRD_PARTY_NOTICES.md');name='THIRD_PARTY_NOTICES.md';target='mgs-coop/THIRD_PARTY_NOTICES.md'},
    @{path=$dll;name='dinput8.dll';target='dinput8.dll'}
)
$files=@()
foreach($inputFile in $inputs) {
    $relative='payload/'+$inputFile.name
    [IO.File]::Copy($inputFile.path,(Join-Path $packageRoot $relative),$false)
    $files+=@([ordered]@{source=$relative;target=$inputFile.target;sha256=(Get-FileHash -LiteralPath (Join-Path $packageRoot $relative)).Hash})
}
$manifest=[ordered]@{
    product='mgs-coop-local-package';version=$version;prepared_utc=[DateTime]::UtcNow.ToString('o')
    executable_sha256='085C2F82D1C963C40B3D2D55786661DFEE2B18CBBF388A710C00FA76C5E9BB45'
    steam_dll_sha256='08BD0152356E5619EE56A6714AC951B6800DBB21821693A4EFAF85278442C60A'
    files=$files;scope='Local development package using locally generated game assets. Two-player gameplay remains unverified.'
}
[IO.File]::WriteAllText((Join-Path $packageRoot 'package.json'),($manifest | ConvertTo-Json -Depth 5)+"`n",[Text.UTF8Encoding]::new($false))
[IO.File]::Copy((Join-Path $PSScriptRoot 'package-install.ps1'),(Join-Path $packageRoot 'install.ps1'),$false)
[IO.File]::Copy((Join-Path $PSScriptRoot 'package-configure-role.ps1'),(Join-Path $packageRoot 'configure-role.ps1'),$false)
[IO.File]::Copy((Join-Path $PSScriptRoot 'set-session-mode.ps1'),(Join-Path $packageRoot 'set-session-mode.ps1'),$false)
foreach($entry in @('install','configure-role')) {
    $batch="@echo off`r`npowershell.exe -NoProfile -ExecutionPolicy Bypass -File `"%~dp0$entry.ps1`" %*`r`nset `"result=%errorlevel%`"`r`npause`r`nexit /b %result%`r`n"
    [IO.File]::WriteAllText((Join-Path $packageRoot ($entry+'.bat')),$batch,[Text.Encoding]::ASCII)
}
# Include the current working source, not a git archive that omits local edits.
# Exclude binaries, local extracted game assets, build output and repository state.
$sourceRoot=Join-Path $packageRoot 'source'
$extensions=@('.cpp','.hpp','.h','.c','.in','.lua','.ps1','.py','.cmake','.txt','.md','.json','.def','.rc','.bat','.vdf')
Push-Location $projectRoot
try {
    $sourceFiles=@(& rg --files --hidden native lua tools tests third_party data -g '!.git' -g '!__pycache__' -g '!*.exe' -g '!*.dll' -g '!*.obj')
    if($LASTEXITCODE -ne 0) { throw 'Could not enumerate source files.' }
    $sourceFiles+=@(& rg --files docs -g '*.md')
    if($LASTEXITCODE -ne 0) { throw 'Could not enumerate source documentation.' }
} finally { Pop-Location }
$sourceFiles+=@('CMakeLists.txt','README.md','THIRD_PARTY_NOTICES.md')
$sourceHashes=@()
foreach($relative in $sourceFiles) {
    $source=Join-Path $projectRoot $relative
    if([IO.Path]::GetExtension($source) -notin $extensions -and [IO.Path]::GetFileName($source) -notmatch '^LICENSE') { continue }
    $destination=Join-Path $sourceRoot $relative
    $parent=Split-Path -Parent $destination
    if(!(Test-Path -LiteralPath $parent)) { $null=New-Item -ItemType Directory -Path $parent -Force }
    [IO.File]::Copy($source,$destination,$false)
    $sourceHashes+=@([ordered]@{path=$relative;sha256=(Get-FileHash -LiteralPath $destination).Hash})
}
[IO.File]::WriteAllText((Join-Path $packageRoot 'source-files.json'),($sourceHashes | ConvertTo-Json -Depth 3)+"`n",[Text.UTF8Encoding]::new($false))
[IO.File]::Copy((Join-Path $PSScriptRoot 'package-readme.txt'),(Join-Path $packageRoot 'README.txt'),$false)
Compress-Archive -LiteralPath $packageRoot -DestinationPath ($packageRoot+'.zip') -CompressionLevel Optimal
Write-Output "Prepared: $packageRoot.zip"
Write-Output 'No game installation, role change, game launch or tests were performed.'
