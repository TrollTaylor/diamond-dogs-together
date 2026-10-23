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
    # local package builds tools/build.ps1 -BuildOnly -DisableLocalExperiments
    # into build/package-loader (never build/native, the lab's research loader);
    # -Friend packages the loader of a passing Invoke-WorkerChecks -Profile
    # Release receipt (the stock46 gun pieces, worker OFF; docs/two-pc-play.md
    # "Release build"). A supplied loader is packaged as-is; its experiment
    # settings are not inferred. With -Friend, -CheckReceipt is required and
    # the supplied loader must have exactly the receipt loader's hash.
    [string]$LoaderPath,
    # -Friend: an existing passing Release receipt (build\worker-runs\<run>\receipt.json);
    # default: run the Release checks now.
    [string]$CheckReceipt,
    # Output folder (default: <repo>/dist). Tests use a fixture folder.
    [string]$OutputRoot
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$version='0.13.6'
$archiveSha256='87BBE82BB1E907CF405200155DA2794BF7E1F5A3D45BF66426A38CDD31A91F96'
$dll=Join-Path $projectRoot 'build/package-loader/dinput8.dll'
# Provenance (package.json "build"): how the loader was made.
$buildInfo=[ordered]@{source='package_build';profile='Normal';flags=$null;receipt=$null;receipt_sha256=$null;audited_tests=$null}
$releaseFlags=[ordered]@{}
foreach($name in @('MGS_LOCAL_BUDDY_CAPACITY_TEST','MGS_LOCAL_GUN_PRELOAD_TEST','MGS_LOCAL_OFFLINE_EQUIPMENT_TEST','MGS_LOCAL_STOCK46_PRELOAD_TEST','MGS_LOCAL_STOCK46_WRITER_TEST',
        'MGS_LOCAL_STOCK46_AMMO_TEST','MGS_LOCAL_ACTION_EVENTS','MGS_LOCAL_UPPER_END_OBSERVE','MGS_LOCAL_UPPER_PHASE_OBSERVE',
        'MGS_LOCAL_UPPER_MANUAL_IDLE','MGS_LOCAL_UPPER_PROBE','MGS_LOCAL_UPPER_CHANNEL2','MGS_LOCAL_UPPER_FIRE_ZERO_BLEND',
        'MGS_LOCAL_NATIVE_ACTIVATION_OBSERVER','MGS_LOCAL_MGO_UNIQUE_REST_OBSERVER','MGS_LOCAL_MGO_STANDING_CYCLE_OBSERVER',
        'MGS_LOCAL_MGO_STANDING_LOOKUP_OBSERVER','MGS_LOCAL_MGO_STANDING_FRAME_OBSERVER')) {
    $releaseFlags[$name]=if($name -in @('MGS_LOCAL_OFFLINE_EQUIPMENT_TEST','MGS_LOCAL_STOCK46_PRELOAD_TEST','MGS_LOCAL_STOCK46_WRITER_TEST')){'ON'}else{'OFF'}
}
$releaseFlags['MGS_BACKGROUND_WORKER']='OFF'
if($RuntimeArchive) {
    $archive=(Resolve-Path -LiteralPath $RuntimeArchive).Path
    if(!(Test-Path -LiteralPath $archive -PathType Leaf)) { throw "Local runtime archive not found: $archive" }
    if((Get-FileHash -LiteralPath $archive).Hash -ne $archiveSha256) { throw 'Expected two-player runtime archive hash does not match.' }
    Write-Output 'Local runtime archive hash verified. It is not included in the package.'
}
$packageSourceSnapshot=$null
if($Friend) {
    Import-Module (Join-Path $PSScriptRoot 'worker\WorkerSource.psm1') -Force -DisableNameChecking
    # An override is not an escape from the friend loader's audited receipt.
    if($LoaderPath -and !$CheckReceipt) { throw 'Friend LoaderPath requires a passing Release check receipt.' }
    if(!$CheckReceipt) {
        $checkOutput=@(& (Join-Path $PSScriptRoot 'worker\Invoke-WorkerChecks.ps1') -Profile Release 2>&1 | ForEach-Object { [string]$_ })
        $CheckReceipt=@($checkOutput | Select-String -Pattern 'Worker check receipt: (.+receipt\.json)' | ForEach-Object { $_.Matches[0].Groups[1].Value })[-1]
        if(!$CheckReceipt) { throw "The Release checks produced no receipt:`n$($checkOutput -join "`n")" }
    }
    $receiptPath=(Resolve-Path -LiteralPath $CheckReceipt).Path
    $receipt=Get-Content -LiteralPath $receiptPath -Raw | ConvertFrom-Json -AsHashtable
    if($receipt.schema -ne 1 -or $receipt.status -cne 'passed' -or $receipt.plan.profile -cne 'Release') { throw "Not a passing Release receipt: $receiptPath" }
    foreach($name in $releaseFlags.Keys) {
        if([string]$receipt.plan.flags[$name] -cne $releaseFlags[$name]) { throw "The Release receipt's $name is not $($releaseFlags[$name])." }
    }
    if($receipt.plan.flags.Count -ne $releaseFlags.Count) { throw 'The Release receipt names other build options.' }
    $dll=Join-Path (Split-Path -Parent $receiptPath) 'dinput8.dll'
    if((Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash -ine [string]$receipt.artifacts.loader_sha256) { throw 'The receipt loader changed since its checks.' }
    if($LoaderPath) {
        $override=(Resolve-Path -LiteralPath $LoaderPath).Path
        if((Get-FileHash -LiteralPath $override -Algorithm SHA256).Hash -ine [string]$receipt.artifacts.loader_sha256) {
            throw 'Friend LoaderPath does not match the Release receipt loader.'
        }
    }
    $packageSourceSnapshot=Get-WorkerSourceHashes $projectRoot
    Assert-WorkerReceiptSource $receipt $packageSourceSnapshot
    $loaderSource='release_receipt'
    Write-Output "Packaging the Release loader of $receiptPath."
} elseif($LoaderPath) {
    $dll=(Resolve-Path -LiteralPath $LoaderPath).Path
    Write-Output "Packaging the existing loader $dll (no build)."
    $loaderSource='supplied'
} else {
    & (Join-Path $PSScriptRoot 'build.ps1') -Configuration Release -BuildOnly -DisableLocalExperiments -BuildDirectory 'build\package-loader'
    $loaderSource='package_build'
}
# Provenance (package.json "build"): how the loader was made.
$buildInfo.source=$loaderSource
if($loaderSource -ceq 'supplied') { $buildInfo.profile='unknown' }
if($loaderSource -ceq 'release_receipt') {
    $buildInfo.profile='Release';$buildInfo.flags=$receipt.plan.flags
    $buildInfo.receipt=[IO.Path]::GetRelativePath($projectRoot,$receiptPath);$buildInfo.receipt_sha256=(Get-FileHash -LiteralPath $receiptPath -Algorithm SHA256).Hash
    $buildInfo.audited_tests=@($receipt.plan.tests).Count
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
    @{path=(Join-Path $projectRoot 'third_party/imgui/LICENSE.txt');name='ImGui-LICENSE.txt';target='mgs-coop/ImGui-LICENSE.txt'},
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
    'Two-PC play package (docs/two-pc-play.md). Contains no game-derived files; the runtime archive is generated or copied locally. Development build: partner bodies support Mother Base and free roam; world synchronization is experimental.'
} else {
    'Local development package. Contains no game-derived files; the runtime archive is generated or copied locally. Two-player gameplay remains unverified.'
}
# Provenance: the source commit and whether the working tree differed from it.
$buildInfo.loader_sha256=(Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash
$buildInfo.source_commit=$null;$buildInfo.source_dirty=$null
try {
    $commit=@(& git -C $projectRoot rev-parse HEAD 2>$null)
    if($LASTEXITCODE -eq 0 -and $commit.Count -eq 1 -and $commit[0] -match '^[0-9a-f]{40}$') {
        $buildInfo.source_commit=$commit[0]
        $buildInfo.source_dirty=@(& git -C $projectRoot status --porcelain --untracked-files=no 2>$null).Count -gt 0
    }
} catch { $buildInfo.source_commit=$null }
$manifest=[ordered]@{
    product='mgs-coop-local-package';version=$version;prepared_utc=[DateTime]::UtcNow.ToString('o')
    executable_sha256='085C2F82D1C963C40B3D2D55786661DFEE2B18CBBF388A710C00FA76C5E9BB45'
    steam_dll_sha256='08BD0152356E5619EE56A6714AC951B6800DBB21821693A4EFAF85278442C60A'
    files=$files
    runtime_archive=[ordered]@{target='mgs-coop/mgs-coop-player-runtime.dat';sha256=$archiveSha256}
    scope=$scope
    build=$buildInfo
}
if($Friend) { $manifest['flavor']='friend-play' }
[IO.File]::WriteAllText((Join-Path $packageRoot 'package.json'),($manifest | ConvertTo-Json -Depth 5)+"`n",[Text.UTF8Encoding]::new($false))
function Write-CrlfAscii([string]$Path,[string]$Text) {
    # Windows PowerShell 5.1 reads BOM-less scripts as ANSI: keep them ASCII.
    if($Text -match '[^\x00-\x7F]') { throw "Non-ASCII text for $Path" }
    [IO.File]::WriteAllText($Path,$Text.Replace("`r`n","`n").Replace("`n","`r`n"),[Text.Encoding]::ASCII)
}
if($Friend) {
    # Friend layout: six double-click entry points at the top, the scripts in scripts\.
    $scripts=Join-Path $packageRoot 'scripts'
    $null=New-Item -ItemType Directory -Path $scripts
    $pieces=[ordered]@{
        'Install-Coop.ps1'=(Join-Path $PSScriptRoot 'friend/Install-Coop.ps1')
        'Play-Coop.ps1'=(Join-Path $PSScriptRoot 'friend/Play-Coop.ps1')
        'Play-LocalCoop.ps1'=(Join-Path $PSScriptRoot 'friend/Play-LocalCoop.ps1')
        'LocalCoopSession.psm1'=(Join-Path $PSScriptRoot 'friend/LocalCoopSession.psm1')
        'LocalCoopWindows.psm1'=(Join-Path $PSScriptRoot 'friend/LocalCoopWindows.psm1')
        'Uninstall-Coop.ps1'=(Join-Path $PSScriptRoot 'friend/Uninstall-Coop.ps1')
        'Collect-Logs.ps1'=(Join-Path $PSScriptRoot 'friend/Collect-Logs.ps1')
        'Share-Save.ps1'=(Join-Path $PSScriptRoot 'friend/Share-Save.ps1')
        'FriendCommon.psm1'=(Join-Path $PSScriptRoot 'friend/FriendCommon.psm1')
        'CoopPlay.psm1'=(Join-Path $PSScriptRoot 'CoopPlay.psm1')
        'install.ps1'=(Join-Path $PSScriptRoot 'package-install.ps1')
    }
    foreach($name in $pieces.Keys) { Write-CrlfAscii (Join-Path $scripts $name) ([IO.File]::ReadAllText($pieces[$name])) }
    foreach($entry in @('Install-Coop','Play-Coop','Uninstall-Coop','Collect-Logs','Share-Save')) {
        # PSModulePath is cleared so that Windows PowerShell uses its own module
        # path even when started from a PowerShell 7 window.
        $batch="@echo off`r`nsetlocal`r`nrem MGS Co-op two-PC play: runs scripts\$entry.ps1 with Windows PowerShell.`r`nset `"PSModulePath=`"`r`n"+
            "powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"%~dp0scripts\$entry.ps1`" %*`r`n"+
            "set `"result=%errorlevel%`"`r`necho.`r`n"+
            "if not `"%result%`"==`"0`" echo Finished with code %result%. Please read the messages above.`r`n"+
            "pause`r`nexit /b %result%`r`n"
        [IO.File]::WriteAllText((Join-Path $packageRoot ($entry+'.bat')),$batch,[Text.Encoding]::ASCII)
    }
    # Compatibility entry point: the unified launcher owns the local-test flow.
    $batch="@echo off`r`nsetlocal`r`nrem MGS Co-op local two-window test through the Play-Coop menu.`r`nset `"PSModulePath=`"`r`n"+
        "powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"%~dp0scripts\Play-Coop.ps1`" -LocalTest %*`r`n"+
        "set `"result=%errorlevel%`"`r`necho.`r`n"+
        "if not `"%result%`"==`"0`" echo Finished with code %result%. Please read the messages above.`r`n"+
        "pause`r`nexit /b %result%`r`n"
    [IO.File]::WriteAllText((Join-Path $packageRoot 'Play-Offline-Coop.bat'),$batch,[Text.Encoding]::ASCII)
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
    $sourceFiles=@(& rg --files --hidden native lua tools tests third_party data cmake -g '!.git' -g '!__pycache__' -g '!*.exe' -g '!*.dll' -g '!*.obj')
    if($LASTEXITCODE -ne 0) { throw 'Could not enumerate source files.' }
    $sourceFiles+=@(& rg --files docs -g '*.md')
    if($LASTEXITCODE -ne 0) { throw 'Could not enumerate source documentation.' }
} finally { Pop-Location }
$sourceFiles+=@('CMakeLists.txt','README.md','THIRD_PARTY_NOTICES.md')
if($Friend) {
    # rg skips the worktree's known MinHook junction. These paths were already
    # fingerprinted through the resolved dependency by the Release receipt;
    # include that checked source rather than leaving an unbuildable bundle.
    $sourceFiles+=@($packageSourceSnapshot.hashes.Keys | Where-Object {
        $_.StartsWith('third_party\minhook\', [StringComparison]::Ordinal)
    })
}
$sourceFiles=@($sourceFiles | Sort-Object -Unique)
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
if($Friend) {
    # Refuse changes while staging as well as a stale receipt at admission.
    Assert-WorkerSourceUnchanged $packageSourceSnapshot (Get-WorkerSourceHashes $projectRoot)
    if((Get-FileHash -LiteralPath (Join-Path $packageRoot 'payload\dinput8.dll') -Algorithm SHA256).Hash -ine
        [string]$receipt.artifacts.loader_sha256) { throw 'The staged loader does not match the Release receipt.' }
}
Compress-Archive -LiteralPath $packageRoot -DestinationPath ($packageRoot+'.zip') -CompressionLevel Optimal
Write-Output "Prepared: $packageRoot.zip"
Write-Output 'No game installation, role change, game launch or tests were performed.'
