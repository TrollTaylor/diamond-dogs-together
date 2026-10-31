# Builds the loader and stages a package under dist/.
# No package contains game-derived files: the two-player runtime archive
# (mgs-coop-player-runtime.dat) is built on each player's own PC from their
# own game files (DiamondDogsTogether.exe does that) and is never bundled.
# Only its SHA-256 is recorded here.
#
# -Friend stages the player package, the Diamond Dogs Together drop-in zip
# (0.14.0+): dist/DiamondDogsTogether-<version>-<UTC stamp>-<8 hex>/ holds
#   DiamondDogsTogether-<version>.zip   the file players get
#   DDT-source-<version>.zip            the complete corresponding source (GPL)
#   package\                            exactly the player zip's contents
# Every release uploads BOTH zips to the same GitHub release (tag v<version>):
# the GPL lets the source be offered "in the same way through the same place"
# (GPLv3 section 6(d)) instead of inside every install. The source zip's name
# never starts with DiamondDogsTogether- and contains "source", so the
# launcher's update check never takes it for the update.
# The player zip's ROOT is what goes into the MGSV game folder (next to mgsvtpp.exe):
#   DiamondDogsTogether.exe             the launcher (setup, host/join, tools)
#   dinput8.dll                         the loader
#   mgs-coop\README.txt                 install, play, troubleshooting, uninstall
#   mgs-coop\package.json               what the launcher checks (below)
#   mgs-coop\THIRD_PARTY_NOTICES.md, DYNAMITE-LICENSE, MinHook-LICENSE.txt,
#            ImGui-LICENSE.txt, tpp-mod-LICENSE
# There are no .bat/.ps1/.psm1 files for players. Both the exe and the dll come
# from one passing Invoke-WorkerChecks -Profile Release receipt (the stock46 gun
# pieces, background worker OFF; the receipt hashes and keeps both).
# package.json: product mgs-coop-local-package, version = the exe's X.Y.Z, the
# pinned game hashes, one files[] row per file of the zip (exactly one with
# target dinput8.dll; native/launcher/core/core_install.hpp ReadPackage) and the
# runtime archive's target and hash, and source_archive (the source zip's name,
# SHA-256 and file count).
#
# Without -Friend: the local development package of the owned installer
# (install.bat, configure-role.bat, set-session-mode.ps1, payload\ and source\;
# docs/local-package.md).
param(
    # Optional: verify that this local archive has the expected hash. It is never copied.
    [string]$RuntimeArchive,
    # Stage the player package (the Diamond Dogs Together drop-in zip).
    [switch]$Friend,
    # Package this loader instead of building one (tests). Default: the
    # local package builds tools/build.ps1 -BuildOnly -DisableLocalExperiments
    # into build/package-loader (never build/native, the lab's research loader);
    # -Friend packages the loader of a passing Invoke-WorkerChecks -Profile
    # Release receipt. A supplied loader is packaged as-is; its experiment
    # settings are not inferred. With -Friend, -CheckReceipt is required and
    # the supplied loader must have exactly the receipt loader's hash.
    [string]$LoaderPath,
    # -Friend: the same for DiamondDogsTogether.exe (tests); it must have exactly
    # the receipt launcher's hash.
    [string]$LauncherPath,
    # -Friend: an existing passing Release receipt (build\worker-runs\<run>\receipt.json);
    # default: run the Release checks now.
    [string]$CheckReceipt,
    # Output folder (default: <repo>/dist). Tests use a fixture folder.
    [string]$OutputRoot
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$version='0.14.5'
$archiveSha256='87BBE82BB1E907CF405200155DA2794BF7E1F5A3D45BF66426A38CDD31A91F96'
$archiveBytes=16333824
$executableSha256='085C2F82D1C963C40B3D2D55786661DFEE2B18CBBF388A710C00FA76C5E9BB45'
$steamDllSha256='08BD0152356E5619EE56A6714AC951B6800DBB21821693A4EFAF85278442C60A'
$launcherName='DiamondDogsTogether.exe'
$dll=Join-Path $projectRoot 'build/package-loader/dinput8.dll'
$exe=$null
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
    # An override is not an escape from the release artifacts' audited receipt.
    if($LoaderPath -and !$CheckReceipt) { throw 'Friend LoaderPath requires a passing Release check receipt.' }
    if($LauncherPath -and !$CheckReceipt) { throw 'Friend LauncherPath requires a passing Release check receipt.' }
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
    # The launcher is built and checked by the same Release run (Invoke-WorkerChecks
    # keeps DiamondDogsTogether.exe and its hash next to the loader).
    if(!$receipt.artifacts.Contains('launcher_sha256') -or [string]$receipt.artifacts.launcher_sha256 -cnotmatch '^[A-Fa-f0-9]{64}$') {
        throw "The Release receipt has no launcher ($launcherName); rerun the Release checks."
    }
    $exe=Join-Path (Split-Path -Parent $receiptPath) $launcherName
    if(!(Test-Path -LiteralPath $exe -PathType Leaf) -or (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash -ine [string]$receipt.artifacts.launcher_sha256) {
        throw 'The receipt launcher changed since its checks.'
    }
    if($LauncherPath) {
        $override=(Resolve-Path -LiteralPath $LauncherPath).Path
        if((Get-FileHash -LiteralPath $override -Algorithm SHA256).Hash -ine [string]$receipt.artifacts.launcher_sha256) {
            throw 'Friend LauncherPath does not match the Release receipt launcher.'
        }
    }
    $packageSourceSnapshot=Get-WorkerSourceHashes $projectRoot
    Assert-WorkerReceiptSource $receipt $packageSourceSnapshot
    $loaderSource='release_receipt'
    Write-Output "Packaging the Release loader and launcher of $receiptPath."
} elseif($LoaderPath) {
    $dll=(Resolve-Path -LiteralPath $LoaderPath).Path
    Write-Output "Packaging the existing loader $dll (no build)."
    $loaderSource='supplied'
} else {
    & (Join-Path $PSScriptRoot 'build.ps1') -Configuration Release -BuildOnly -DisableLocalExperiments -BuildDirectory 'build\package-loader'
    $loaderSource='package_build'
}
# Provenance (package.json "build"): how the loader (and launcher) were made.
$buildInfo.source=$loaderSource
if($loaderSource -ceq 'supplied') { $buildInfo.profile='unknown' }
if($loaderSource -ceq 'release_receipt') {
    $buildInfo.profile='Release';$buildInfo.flags=$receipt.plan.flags
    $buildInfo.receipt=[IO.Path]::GetRelativePath($projectRoot,$receiptPath);$buildInfo.receipt_sha256=(Get-FileHash -LiteralPath $receiptPath -Algorithm SHA256).Hash
    $buildInfo.audited_tests=@($receipt.plan.tests).Count
}
$buildInfo.loader_sha256=(Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash
if($exe) { $buildInfo.launcher_sha256=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash }
# The folders (and root files) the source zip is made from.
$sourceScope=@('native','lua','tools','tests','third_party','cmake','docs','CMakeLists.txt','README.md','THIRD_PARTY_NOTICES.md')
# Files git neither tracks nor ignores in those folders (0.14.4 review): a scratch file left in the checkout (notes, a
# run's JSON with a join code or a local path) must never reach the public source zip. .git is $false when this is not
# a git checkout (an unpacked source zip has no such notion; a checkout whose git cannot be run is refused, below);
# .files are relative paths with backslashes. MinHook is a
# submodule whose files the superproject never lists; a MinHook copied in as a plain folder is the dependency itself
# (the Release receipt pins its files), so it is left to that check. git's output is read as UTF-8 (any file name).
function Get-UntrackedSourceFiles {
    $run={
        param([string[]]$GitArgs)
        $psi=[Diagnostics.ProcessStartInfo]::new('git')
        foreach($a in @('-C',$projectRoot)+$GitArgs) { $psi.ArgumentList.Add($a) }
        $psi.UseShellExecute=$false;$psi.CreateNoWindow=$true;$psi.RedirectStandardOutput=$true;$psi.RedirectStandardError=$true
        $psi.StandardOutputEncoding=[Text.UTF8Encoding]::new($false)
        $p=[Diagnostics.Process]::Start($psi)
        try {
            $out=$p.StandardOutput.ReadToEndAsync();$null=$p.StandardError.ReadToEndAsync();$p.WaitForExit()
            return [pscustomobject]@{exit=$p.ExitCode;out=$out.GetAwaiter().GetResult()}
        } finally { $p.Dispose() }
    }
    $none=[pscustomobject]@{git=$false;files=[string[]]@()}
    # Fails closed (0.14.4 review, round 2): with a .git entry at the root (a folder, or a worktree's file) this is a
    # git checkout, and git must answer; git missing from PATH (or any other failure to run it) must never mean "nothing
    # untracked". Without one (an unpacked source zip) there is nothing to ask.
    $checkout=Test-Path -LiteralPath (Join-Path $projectRoot '.git')
    $refuse={ param([string]$Why) throw "This is a git checkout, but $Why; the files git does not track cannot be listed, so nothing is packaged. Make git work here (put it on PATH), then package again." }
    try { $inside=& $run @('rev-parse','--is-inside-work-tree') }
    catch { if($checkout) { & $refuse "git could not be run ($($_.Exception.Message))" }; return $none }
    if($inside.exit -ne 0 -or $inside.out.Trim() -cne 'true') {
        if($checkout) { & $refuse "git does not see a work tree here (exit $($inside.exit))" }
        return $none
    }
    $listed=& $run (@('-c','core.quotepath=off','ls-files','-z','--others','--exclude-standard','--')+$sourceScope)
    if($listed.exit -ne 0) { throw 'git could not list the untracked files of the checkout.' }
    $files=[string[]]@($listed.out.Split([char]0,[StringSplitOptions]::RemoveEmptyEntries) |
        Where-Object { !$_.StartsWith('third_party/minhook/',[StringComparison]::OrdinalIgnoreCase) } | ForEach-Object { $_.Replace('/','\') })
    return [pscustomobject]@{git=$true;files=$files}
}
# Provenance: the source commit and whether the working tree differed from it (tracked changes anywhere, or files git
# does not track in the published folders).
$buildInfo.source_commit=$null;$buildInfo.source_dirty=$null
try {
    $commit=@(& git -C $projectRoot rev-parse HEAD 2>$null)
    if($LASTEXITCODE -eq 0 -and $commit.Count -eq 1 -and $commit[0] -match '^[0-9a-f]{40}$') {
        $buildInfo.source_commit=$commit[0]
        $changed=@(& git -C $projectRoot status --porcelain --untracked-files=no 2>$null).Count
        $buildInfo.source_dirty=($changed + (Get-UntrackedSourceFiles).files.Count) -gt 0
    }
} catch { $buildInfo.source_commit=$null;$buildInfo.source_dirty=$null }

$preparedUtc=[DateTime]::UtcNow
$stamp=$preparedUtc.ToString('yyyyMMddTHHmmssZ')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8)
$distRoot=if($OutputRoot) { [IO.Path]::GetFullPath($OutputRoot) } else { Join-Path $projectRoot 'dist' }
$licenses=@(
    @{path=(Join-Path $projectRoot 'third_party/dynamite/LICENSE');name='DYNAMITE-LICENSE'},
    @{path=(Join-Path $projectRoot 'third_party/minhook/LICENSE.txt');name='MinHook-LICENSE.txt'},
    @{path=(Join-Path $projectRoot 'third_party/imgui/LICENSE.txt');name='ImGui-LICENSE.txt'},
    @{path=(Join-Path $projectRoot 'third_party/tpp-mod/LICENSE');name='tpp-mod-LICENSE'},
    @{path=(Join-Path $projectRoot 'THIRD_PARTY_NOTICES.md');name='THIRD_PARTY_NOTICES.md'}
)
function Write-CrlfAscii([string]$Path,[string]$Text) {
    # Notepad and Windows PowerShell 5.1 read BOM-less text as ANSI: keep it ASCII.
    if($Text -match '[^\x00-\x7F]') { throw "Non-ASCII text for $Path" }
    [IO.File]::WriteAllText($Path,$Text.Replace("`r`n","`n").Replace("`n","`r`n"),[Text.Encoding]::ASCII)
}
function Get-Sha256Hex([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash }
# The source files to publish: the current working source, not a git archive
# that omits local edits. Binaries, local extracted game assets, build output and
# repository state are excluded; the launcher's embedded assets (the two
# Dynamite-derived controller FPKDs, its icon, logo and manifest) are included,
# and so are the .inc fragments that native/*.cpp #include into the loader.
function Get-PackageSourceFiles {
    $extensions=@('.cpp','.hpp','.h','.c','.inc','.in','.lua','.ps1','.psm1','.py','.cmake','.txt','.md','.json','.def','.rc','.bat','.vdf','.bin','.ico','.png','.manifest')
    # Other file types only at these exact places: the partner-lab fixture logs
    # that tests/lab_scripts_tests.ps1 reads, and the Go archive probe.
    $exactPaths='^(tests\\fixtures\\partner-lab\\[^\\]+\.log|tools\\archive_probe\\(main\.go|go\.mod|go\.sum))$'
    Push-Location $projectRoot
    try {
        $files=@(& rg --files --hidden native lua tools tests third_party cmake -g '!.git' -g '!__pycache__' -g '!*.exe' -g '!*.dll' -g '!*.obj')
        if($LASTEXITCODE -ne 0) { throw 'Could not enumerate source files.' }
        $files+=@(& rg --files docs -g '*.md')
        if($LASTEXITCODE -ne 0) { throw 'Could not enumerate source documentation.' }
    } finally { Pop-Location }
    # docs/readme/logo.png: the launcher icon is generated from it
    # (tools/launcher/make_launcher_assets.py), and its CTests read it.
    $files+=@('CMakeLists.txt','README.md','THIRD_PARTY_NOTICES.md','docs/readme/logo.png')
    if($Friend) {
        # rg skips the worktree's known MinHook junction. These paths were already
        # fingerprinted through the resolved dependency by the Release receipt;
        # include that checked source rather than leaving an unbuildable bundle.
        $files+=@($packageSourceSnapshot.hashes.Keys | Where-Object {
            $_.StartsWith('third_party\minhook\', [StringComparison]::Ordinal)
        })
    }
    $rows=@()
    foreach($relative in @($files | ForEach-Object { $_.Replace('/','\') } | Sort-Object -Unique)) {
        $source=Join-Path $projectRoot $relative
        $leaf=[IO.Path]::GetFileName($source)
        if([IO.Path]::GetExtension($source) -notin $extensions -and $leaf -notmatch '^LICENSE' -and $relative -notmatch $exactPaths) { continue }
        # Never a link key or a runtime archive, whatever lies in the working tree.
        if($leaf -like 'link-secret*' -or $leaf -like 'mgs-coop-player-runtime*') { continue }
        $rows+=@([pscustomobject]@{relative=$relative;path=$source})
    }
    # Only what git tracks is published: an untracked file is refused by name (commit it, or move it out).
    $untracked=(Get-UntrackedSourceFiles).files
    if($untracked.Count) {
        $set=[Collections.Generic.HashSet[string]]::new($untracked,[StringComparer]::OrdinalIgnoreCase)
        $published=@($rows | Where-Object { $set.Contains($_.relative) })
        if($published.Count) {
            throw "Untracked file would be published in the source zip: $($published[0].relative) ($($published.Count) in all). Commit it, or move it out of the checkout, then package again."
        }
    }
    return $rows
}
function Write-PackageZip([string]$ZipPath,$Entries,[DateTime]$Time) {
    # Entries: name (forward slashes, relative) and path, in this order. Every
    # entry gets the same time, so a zip depends only on its contents and $Time.
    Add-Type -AssemblyName System.IO.Compression
    $stream=[IO.File]::Open($ZipPath,[IO.FileMode]::CreateNew)
    try {
        $zip=[IO.Compression.ZipArchive]::new($stream,[IO.Compression.ZipArchiveMode]::Create)
        try {
            foreach($e in $Entries) {
                if($e.name -match '\\' -or $e.name.StartsWith('/') -or $e.name -match '(^|/)\.\.(/|$)') { throw "Bad zip entry name: $($e.name)" }
                $entry=$zip.CreateEntry($e.name,[IO.Compression.CompressionLevel]::Optimal)
                $entry.LastWriteTime=[DateTimeOffset]::new($Time.ToLocalTime())
                $in=[IO.File]::OpenRead($e.path)
                try { $out=$entry.Open(); try { $in.CopyTo($out) } finally { $out.Dispose() } } finally { $in.Dispose() }
            }
        } finally { $zip.Dispose() }
    } finally { $stream.Dispose() }
}
function Assert-PackageZip([string]$ZipPath,$Entries) {
    # Read back: exactly these entries, each with the bytes of its file.
    Add-Type -AssemblyName System.IO.Compression
    $zip=[IO.Compression.ZipFile]::OpenRead($ZipPath)
    try {
        $names=@($zip.Entries | ForEach-Object { $_.FullName })
        if(($names -join "`n") -cne (@($Entries | ForEach-Object { $_.name }) -join "`n")) { throw "The written zip does not hold exactly the staged files: $ZipPath" }
        foreach($e in $Entries) {
            $entry=$zip.GetEntry($e.name)
            $sha=[Security.Cryptography.SHA256]::Create(); $s=$entry.Open()
            try { $hash=[Convert]::ToHexString($sha.ComputeHash($s)) } finally { $s.Dispose(); $sha.Dispose() }
            if($hash -cne (Get-Sha256Hex $e.path)) { throw "The written zip entry differs from its file: $($e.name)" }
        }
    } finally { $zip.Dispose() }
}

# The source files, listed (and an untracked one refused) before anything is written.
$sourceRows=@(Get-PackageSourceFiles)
if(!(Test-Path -LiteralPath $distRoot)) { $null=New-Item -ItemType Directory -Path $distRoot }
if($Friend) {
    # ---------------------------------------------------------------- the drop-in player package
    $runRoot=Join-Path $distRoot "DiamondDogsTogether-$version-$stamp"
    $contents=Join-Path $runRoot 'package'
    $modDir=Join-Path $contents 'mgs-coop'
    $null=New-Item -ItemType Directory -Path $modDir -Force
    $files=@()
    [IO.File]::Copy($exe,(Join-Path $contents $launcherName),$false)
    [IO.File]::Copy($dll,(Join-Path $contents 'dinput8.dll'),$false)
    foreach($license in $licenses) { [IO.File]::Copy($license.path,(Join-Path $modDir $license.name),$false) }
    $readme=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'package-readme-player.txt')).Replace('@VERSION@',$version)
    if($readme.Contains('@')) { throw 'package-readme-player.txt has an unknown placeholder.' }
    Write-CrlfAscii (Join-Path $modDir 'README.txt') $readme
    # The GPL source: its own zip next to the player zip (a release asset of
    # its own), with its own hash list. Never inside the player zip.
    $sourceEntries=@()
    $sourceHashes=@()
    foreach($row in $sourceRows) {
        $name=$row.relative.Replace('\','/')
        $sourceEntries+=@([pscustomobject]@{name=$name;path=$row.path})
        $sourceHashes+=@([ordered]@{path=$name;sha256=(Get-Sha256Hex $row.path)})
    }
    $sourceList=Join-Path $runRoot 'source-files.json'
    [IO.File]::WriteAllText($sourceList,($sourceHashes | ConvertTo-Json -Depth 3)+"`n",[Text.UTF8Encoding]::new($false))
    $sourceEntries+=@([pscustomobject]@{name='source-files.json';path=$sourceList})
    $sourceZipName="DDT-source-$version.zip"
    if($sourceZipName -match '^(DiamondDogsTogether-|mgs-coop-friend-)' -or $sourceZipName -notmatch 'source') { throw 'The source zip name could be taken for an update.' }
    $sourceZip=Join-Path $runRoot $sourceZipName
    Write-PackageZip $sourceZip $sourceEntries $preparedUtc
    Assert-PackageZip $sourceZip $sourceEntries
    # package.json: one row per file of the zip (exactly one dinput8.dll row).
    foreach($target in @($launcherName,'dinput8.dll') + @($licenses | ForEach-Object { 'mgs-coop/' + $_.name }) + @('mgs-coop/README.txt')) {
        $files+=@([ordered]@{source=$target;target=$target;sha256=(Get-Sha256Hex (Join-Path $contents $target))})
    }
    $manifest=[ordered]@{
        product='mgs-coop-local-package';version=$version;prepared_utc=$preparedUtc.ToString('o')
        layout='drop-in';flavor='friend-play'
        executable_sha256=$executableSha256;steam_dll_sha256=$steamDllSha256
        files=$files
        runtime_archive=[ordered]@{target='mgs-coop/mgs-coop-player-runtime.dat';sha256=$archiveSha256;bytes=$archiveBytes;built_by=$launcherName}
        source_archive=[ordered]@{name=$sourceZipName;sha256=(Get-Sha256Hex $sourceZip);files=$sourceRows.Count;where="the GitHub release v$version, next to DiamondDogsTogether-$version.zip"}
        scope='Diamond Dogs Together drop-in package: extract into the MGSV game folder and run DiamondDogsTogether.exe. Contains no game-derived files; the runtime archive is built on the player''s PC from their own game files. Steam online play only.'
        build=$buildInfo
    }
    [IO.File]::WriteAllText((Join-Path $modDir 'package.json'),($manifest | ConvertTo-Json -Depth 6)+"`n",[Text.UTF8Encoding]::new($false))
    # Refuse changes while staging as well as a stale receipt at admission.
    Assert-WorkerSourceUnchanged $packageSourceSnapshot (Get-WorkerSourceHashes $projectRoot)
    if((Get-Sha256Hex (Join-Path $contents 'dinput8.dll')) -ine [string]$receipt.artifacts.loader_sha256) { throw 'The staged loader does not match the Release receipt.' }
    if((Get-Sha256Hex (Join-Path $contents $launcherName)) -ine [string]$receipt.artifacts.launcher_sha256) { throw 'The staged launcher does not match the Release receipt.' }
    # Nothing for players to run but the exe: no scripts, no runtime archive, no link key.
    $staged=@(Get-ChildItem -LiteralPath $contents -File -Recurse -Force)
    $refused=@($staged | Where-Object { $_.Extension -in @('.bat','.cmd','.ps1','.psm1','.dat') -or $_.Name -like 'link-secret*' -or $_.Name -like 'mgs-coop-player-runtime*' -or $_.Name -like '*source*' })
    if($refused.Count) { throw "The package would contain $($refused[0].Name)." }
    $entries=@()
    foreach($target in @($launcherName,'dinput8.dll','mgs-coop/package.json','mgs-coop/README.txt') + @($licenses | ForEach-Object { 'mgs-coop/' + $_.name })) {
        $entries+=@([pscustomobject]@{name=$target;path=(Join-Path $contents $target)})
    }
    if($entries.Count -ne $staged.Count) { throw 'The staged package holds files that are not listed.' }
    $zipPath=Join-Path $runRoot "DiamondDogsTogether-$version.zip"
    Write-PackageZip $zipPath $entries $preparedUtc
    Assert-PackageZip $zipPath $entries
    Write-Output "Prepared: $zipPath"
    Write-Output ("SHA-256: " + (Get-Sha256Hex $zipPath) + "; loader " + $buildInfo.loader_sha256 + "; launcher " + $buildInfo.launcher_sha256)
    Write-Output "Prepared source: $sourceZip"
    Write-Output ("Source SHA-256: " + (Get-Sha256Hex $sourceZip) + "; files " + $sourceRows.Count)
    Write-Output "Upload BOTH zips to the GitHub release v$version, then check it before publishing: pwsh -NoProfile -File tools/check-release.ps1 -Version $version -PackageDir `"$(Split-Path -Parent $zipPath)`""
    Write-Output 'No game installation, game launch or tests were performed.'
    return
}

# -------------------------------------------------------------------- the local development package
$packageRoot=Join-Path $distRoot "mgs-coop-$version-$stamp"
$null=New-Item -ItemType Directory -Path $packageRoot
$null=New-Item -ItemType Directory -Path (Join-Path $packageRoot 'payload')
$files=@()
foreach($inputFile in @($licenses + @(@{path=$dll;name='dinput8.dll'}))) {
    $relative='payload/'+$inputFile.name
    $target=if($inputFile.name -ceq 'dinput8.dll') { 'dinput8.dll' } else { 'mgs-coop/' + $inputFile.name }
    [IO.File]::Copy($inputFile.path,(Join-Path $packageRoot $relative),$false)
    $files+=@([ordered]@{source=$relative;target=$target;sha256=(Get-Sha256Hex (Join-Path $packageRoot $relative))})
}
$manifest=[ordered]@{
    product='mgs-coop-local-package';version=$version;prepared_utc=$preparedUtc.ToString('o')
    executable_sha256=$executableSha256;steam_dll_sha256=$steamDllSha256
    files=$files
    runtime_archive=[ordered]@{target='mgs-coop/mgs-coop-player-runtime.dat';sha256=$archiveSha256}
    scope='Local development package. Contains no game-derived files; the runtime archive is generated or copied locally. Two-player gameplay remains unverified.'
    build=$buildInfo
}
[IO.File]::WriteAllText((Join-Path $packageRoot 'package.json'),($manifest | ConvertTo-Json -Depth 5)+"`n",[Text.UTF8Encoding]::new($false))
[IO.File]::Copy((Join-Path $PSScriptRoot 'package-install.ps1'),(Join-Path $packageRoot 'install.ps1'),$false)
[IO.File]::Copy((Join-Path $PSScriptRoot 'package-configure-role.ps1'),(Join-Path $packageRoot 'configure-role.ps1'),$false)
[IO.File]::Copy((Join-Path $PSScriptRoot 'set-session-mode.ps1'),(Join-Path $packageRoot 'set-session-mode.ps1'),$false)
foreach($entry in @('install','configure-role')) {
    $batch="@echo off`r`npowershell.exe -NoProfile -ExecutionPolicy Bypass -File `"%~dp0$entry.ps1`" %*`r`nset `"result=%errorlevel%`"`r`npause`r`nexit /b %result%`r`n"
    [IO.File]::WriteAllText((Join-Path $packageRoot ($entry+'.bat')),$batch,[Text.Encoding]::ASCII)
}
$sourceRoot=Join-Path $packageRoot 'source'
$sourceHashes=@()
foreach($row in $sourceRows) {
    $destination=Join-Path $sourceRoot $row.relative
    $parent=Split-Path -Parent $destination
    if(!(Test-Path -LiteralPath $parent)) { $null=New-Item -ItemType Directory -Path $parent -Force }
    [IO.File]::Copy($row.path,$destination,$false)
    $sourceHashes+=@([ordered]@{path=$row.relative;sha256=(Get-Sha256Hex $destination)})
}
[IO.File]::WriteAllText((Join-Path $packageRoot 'source-files.json'),($sourceHashes | ConvertTo-Json -Depth 3)+"`n",[Text.UTF8Encoding]::new($false))
[IO.File]::Copy((Join-Path $PSScriptRoot 'package-readme.txt'),(Join-Path $packageRoot 'README.txt'),$false)
$archiveNote=@"
MGS Co-op $version - runtime archive

This package does not contain game files or game-derived data. The loader
needs the two-player runtime archive mgs-coop-player-runtime.dat
(SHA-256 $archiveSha256), which is generated or copied on your own PC from
your own game installation (DiamondDogsTogether.exe builds it, or
ddt_datbuild.exe build <game folder> <out.dat>). Double-clicking install.bat
asks for the game folder and then for the path of this file. From a command
line:

  powershell -NoProfile -File .\install.ps1 -GameDirectory "<MGSV folder>" -RuntimeArchive "<path>\mgs-coop-player-runtime.dat"

An installed archive with the expected hash is reused when -RuntimeArchive is
omitted (upgrades). The installer refuses an archive with any other hash.
"@
[IO.File]::WriteAllText((Join-Path $packageRoot 'RUNTIME-ARCHIVE.txt'),$archiveNote.Replace("`r`n","`n").Replace("`n","`r`n"),[Text.Encoding]::ASCII)
Compress-Archive -LiteralPath $packageRoot -DestinationPath ($packageRoot+'.zip') -CompressionLevel Optimal
Write-Output "Prepared: $packageRoot.zip"
Write-Output 'No game installation, role change, game launch or tests were performed.'
