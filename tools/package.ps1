# Builds the loader and stages a local development package under dist/.
# The package contains NO game-derived files: the two-player runtime archive
# (mgs-coop-player-runtime.dat) is generated or copied on each user's machine
# and passed to install.ps1 -RuntimeArchive (an installed copy with the
# expected hash is reused). Only the archive's SHA-256 is recorded here.
#
# -Friend stages the two-PC play package instead (flavor friend-play,
# dist/mgs-coop-friend-<version>-<stamp>/ + .zip, docs/two-pc-play.md): the
# same payload, package.json and owned installer, plus double-click
# Install-Coop / Play-Coop / Uninstall-Coop / Collect-Logs scripts (scripts/,
# Windows PowerShell 5.1, with FriendCommon.psm1 and CoopPlay.psm1),
# FRIEND-GUIDE.txt and RUNTIME-ARCHIVE.txt. It never contains the lab module
# (LabCommon.psm1), the session-role command, the runtime archive or a
# link-secret.txt.
param(
    # Optional: verify that this local archive has the expected hash. It is never copied.
    [string]$RuntimeArchive,
    # Stage the two-PC play package (friend package).
    [switch]$Friend,
    # Package this loader instead of building one (tests). Default: the
    # tools/build.ps1 -BuildOnly output build/native/dinput8.dll.
    [string]$LoaderPath,
    # Output folder (default: <repo>/dist). Tests use a fixture folder.
    [string]$OutputRoot
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$version='0.11.0'
$archiveSha256='26CE7C2168D655C35BE39E4F17A295206AA0522DE8782CD57A36C4FEEA382F54'
$dll=Join-Path $projectRoot 'build/native/dinput8.dll'
if($RuntimeArchive) {
    $archive=(Resolve-Path -LiteralPath $RuntimeArchive).Path
    if(!(Test-Path -LiteralPath $archive -PathType Leaf)) { throw "Local runtime archive not found: $archive" }
    if((Get-FileHash -LiteralPath $archive).Hash -ne $archiveSha256) { throw 'Expected two-player runtime archive hash does not match.' }
    Write-Output 'Local runtime archive hash verified. It is not included in the package.'
}
if($LoaderPath) {
    $dll=(Resolve-Path -LiteralPath $LoaderPath).Path
    Write-Output "Packaging the existing loader $dll (no build)."
} else {
    & (Join-Path $PSScriptRoot 'build.ps1') -Configuration Release -BuildOnly
}
$stamp=[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8)
$distRoot=if($OutputRoot) { [IO.Path]::GetFullPath($OutputRoot) } else { Join-Path $projectRoot 'dist' }
if(!(Test-Path -LiteralPath $distRoot)) { $null=New-Item -ItemType Directory -Path $distRoot }
$packageName=if($Friend) { "mgs-coop-friend-$version-$stamp" } else { "mgs-coop-$version-$stamp" }
$packageRoot=Join-Path $distRoot $packageName
$null=New-Item -ItemType Directory -Path $packageRoot
$null=New-Item -ItemType Directory -Path (Join-Path $packageRoot 'payload')
$inputs=@(
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
$scope=if($Friend) {
    'Two-PC play package (docs/two-pc-play.md). Contains no game-derived files; the runtime archive is generated or copied locally. Development build: the partner is shown in Mother Base; the world is not synchronized.'
} else {
    'Local development package. Contains no game-derived files; the runtime archive is generated or copied locally. Two-player gameplay remains unverified.'
}
$manifest=[ordered]@{
    product='mgs-coop-local-package';version=$version;prepared_utc=[DateTime]::UtcNow.ToString('o')
    executable_sha256='085C2F82D1C963C40B3D2D55786661DFEE2B18CBBF388A710C00FA76C5E9BB45'
    steam_dll_sha256='08BD0152356E5619EE56A6714AC951B6800DBB21821693A4EFAF85278442C60A'
    files=$files
    runtime_archive=[ordered]@{target='mgs-coop/mgs-coop-player-runtime.dat';sha256=$archiveSha256}
    scope=$scope
}
if($Friend) { $manifest['flavor']='friend-play' }
[IO.File]::WriteAllText((Join-Path $packageRoot 'package.json'),($manifest | ConvertTo-Json -Depth 5)+"`n",[Text.UTF8Encoding]::new($false))
function Write-CrlfAscii([string]$Path,[string]$Text) {
    # Windows PowerShell 5.1 reads BOM-less scripts as ANSI: keep them ASCII.
    if($Text -match '[^\x00-\x7F]') { throw "Non-ASCII text for $Path" }
    [IO.File]::WriteAllText($Path,$Text.Replace("`r`n","`n").Replace("`n","`r`n"),[Text.Encoding]::ASCII)
}
if($Friend) {
    # Friend layout: four double-click entry points at the top, the scripts in scripts\.
    $scripts=Join-Path $packageRoot 'scripts'
    $null=New-Item -ItemType Directory -Path $scripts
    $pieces=[ordered]@{
        'Install-Coop.ps1'=(Join-Path $PSScriptRoot 'friend/Install-Coop.ps1')
        'Play-Coop.ps1'=(Join-Path $PSScriptRoot 'friend/Play-Coop.ps1')
        'Uninstall-Coop.ps1'=(Join-Path $PSScriptRoot 'friend/Uninstall-Coop.ps1')
        'Collect-Logs.ps1'=(Join-Path $PSScriptRoot 'friend/Collect-Logs.ps1')
        'FriendCommon.psm1'=(Join-Path $PSScriptRoot 'friend/FriendCommon.psm1')
        'CoopPlay.psm1'=(Join-Path $PSScriptRoot 'CoopPlay.psm1')
        'install.ps1'=(Join-Path $PSScriptRoot 'package-install.ps1')
    }
    foreach($name in $pieces.Keys) { Write-CrlfAscii (Join-Path $scripts $name) ([IO.File]::ReadAllText($pieces[$name])) }
    foreach($entry in @('Install-Coop','Play-Coop','Uninstall-Coop','Collect-Logs')) {
        # PSModulePath is cleared so that Windows PowerShell uses its own module
        # path even when started from a PowerShell 7 window.
        $batch="@echo off`r`nsetlocal`r`nrem MGS Co-op two-PC play: runs scripts\$entry.ps1 with Windows PowerShell.`r`nset `"PSModulePath=`"`r`n"+
            "powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"%~dp0scripts\$entry.ps1`" %*`r`n"+
            "set `"result=%errorlevel%`"`r`necho.`r`n"+
            "if not `"%result%`"==`"0`" echo Finished with code %result%. Please read the messages above.`r`n"+
            "pause`r`nexit /b %result%`r`n"
        [IO.File]::WriteAllText((Join-Path $packageRoot ($entry+'.bat')),$batch,[Text.Encoding]::ASCII)
    }
    foreach($doc in @('FRIEND-GUIDE.txt','RUNTIME-ARCHIVE.txt')) {
        $text=[IO.File]::ReadAllText((Join-Path $PSScriptRoot "friend/$doc")).Replace('@VERSION@',$version).Replace('@ARCHIVE_SHA256@',$archiveSha256)
        Write-CrlfAscii (Join-Path $packageRoot $doc) $text
    }
} else {
    [IO.File]::Copy((Join-Path $PSScriptRoot 'package-install.ps1'),(Join-Path $packageRoot 'install.ps1'),$false)
    [IO.File]::Copy((Join-Path $PSScriptRoot 'package-configure-role.ps1'),(Join-Path $packageRoot 'configure-role.ps1'),$false)
    [IO.File]::Copy((Join-Path $PSScriptRoot 'set-session-mode.ps1'),(Join-Path $packageRoot 'set-session-mode.ps1'),$false)
    foreach($entry in @('install','configure-role')) {
        $batch="@echo off`r`npowershell.exe -NoProfile -ExecutionPolicy Bypass -File `"%~dp0$entry.ps1`" %*`r`nset `"result=%errorlevel%`"`r`npause`r`nexit /b %result%`r`n"
        [IO.File]::WriteAllText((Join-Path $packageRoot ($entry+'.bat')),$batch,[Text.Encoding]::ASCII)
    }
}
# Include the current working source, not a git archive that omits local edits.
# Exclude binaries, local extracted game assets, build output and repository state.
$sourceRoot=Join-Path $packageRoot 'source'
$extensions=@('.cpp','.hpp','.h','.c','.in','.lua','.ps1','.psm1','.py','.cmake','.txt','.md','.json','.def','.rc','.bat','.vdf')
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
    # Never a link key or a runtime archive, whatever lies in the working tree.
    if([IO.Path]::GetFileName($source) -like 'link-secret*' -or [IO.Path]::GetFileName($source) -like 'mgs-coop-player-runtime*') { continue }
    $destination=Join-Path $sourceRoot $relative
    $parent=Split-Path -Parent $destination
    if(!(Test-Path -LiteralPath $parent)) { $null=New-Item -ItemType Directory -Path $parent -Force }
    [IO.File]::Copy($source,$destination,$false)
    $sourceHashes+=@([ordered]@{path=$relative;sha256=(Get-FileHash -LiteralPath $destination).Hash})
}
[IO.File]::WriteAllText((Join-Path $packageRoot 'source-files.json'),($sourceHashes | ConvertTo-Json -Depth 3)+"`n",[Text.UTF8Encoding]::new($false))
if(!$Friend) {
    [IO.File]::Copy((Join-Path $PSScriptRoot 'package-readme.txt'),(Join-Path $packageRoot 'README.txt'),$false)
    $archiveNote=@"
MGS Co-op $version - runtime archive

This package does not contain game files or game-derived data. The loader
needs the two-player runtime archive mgs-coop-player-runtime.dat
(SHA-256 $archiveSha256), which is generated or copied on your own PC from
your own game installation. Double-clicking install.bat asks for the game
folder and then for the path of this file. From a command line:

  powershell -NoProfile -File .\install.ps1 -GameDirectory "<MGSV folder>" -RuntimeArchive "<path>\mgs-coop-player-runtime.dat"

An installed archive with the expected hash is reused when -RuntimeArchive is
omitted (upgrades). The installer refuses an archive with any other hash.
"@
    [IO.File]::WriteAllText((Join-Path $packageRoot 'RUNTIME-ARCHIVE.txt'),$archiveNote.Replace("`r`n","`n").Replace("`n","`r`n"),[Text.Encoding]::ASCII)
}
Compress-Archive -LiteralPath $packageRoot -DestinationPath ($packageRoot+'.zip') -CompressionLevel Optimal
Write-Output "Prepared: $packageRoot.zip"
Write-Output 'No game installation, role change, game launch or tests were performed.'
