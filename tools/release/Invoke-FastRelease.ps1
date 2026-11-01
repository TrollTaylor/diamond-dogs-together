#Requires -Version 7
<#
.SYNOPSIS
The fast release path (AGENTS.md "Releasing"): one deterministic run from dev/oct7-continue to a packaged, scanned and
recorded release merged back into dev. Publishing on GitHub only with -Publish.

.DESCRIPTION
Steps (each timed in the summary):
  preflight   the main checkout is on -DevBranch with no tracked changes; the release worktree (if it exists) is on
              -ReleaseBranch and entirely clean; -Version is newer than dev's; -Publish needs -NotesFile and gh.
  worktree    the persistent release worktree (default C:\mgs-coop-wt2\release on wp/release), created once from dev
              when missing; the ignored dependencies (third_party\minhook, work\lua-5.1.5, work\upstream-dynamite,
              build\*-2026* game-input folders) copied from -DepsFrom when missing. Later builds are incremental.
  merge       dev merged into the release branch (--no-ff); a conflict is aborted and named.
  privacy     git grep of the tracked files for the Windows profile path (\, / and \\ spellings), the git user name
              (outside the GitHub owner login) and the git e-mail; any hit fails.
  bump        the version bump in the same 8 files as every release (tools/release/FastRelease.psm1), committed as
              'chore(release): bump version to X.Y.Z'. A rerun after a later failure reuses a bump commit at HEAD.
  lock        %TEMP%\mgs-coop-verify.lock taken exclusively (waits for another owner; never deletes one), then
              Wait-CtestReaders.
  gate        tools\worker\Invoke-WorkerChecks.ps1 -Profile Release -BuildJobs/-TestJobs (8): the only test run.
  package     tools\package.ps1 -Friend -CheckReceipt <that receipt>; then the lock is released.
  facts       package.json (version, source_commit = the bump, source_dirty false), sizes, SHA-256, build ids.
  defender    MpCmdRun -Scan -ScanType 3 -File <f> -DisableRemediation on the exe, the dll and both zips.
  zip-privacy the privacy grep over every file of both zips.
  record      the docs\STATUS.md package record, committed.
  dev-merge   the release branch merged into dev in the main checkout (--no-ff, no build there); the trees must match.
  publish     only with -Publish: push dev, PR dev -> main, merge it, draft prerelease with BOTH zips and -NotesFile,
              tools\check-release.ps1, publish, check-release again.

-DryRun runs every step on a detached HEAD in the release worktree: its merge, bump and record commits are on no
branch, the dev merge is only computed (git merge-tree, trees compared), nothing is pushed or published, and the
worktree goes back to the release branch at the end. The gate, package, Defender scan and privacy greps run for real.

The summary (JSON) goes to -SummaryPath (default: <log dir>\summary.json; the log dir defaults to
%TEMP%\mgs-coop-fast-release\<version>-<stamp>). Exit code 0 only when every step passed.

.EXAMPLE
pwsh -NoProfile -File tools/release/Invoke-FastRelease.ps1 -Version 0.14.8 -DryRun
pwsh -NoProfile -File tools/release/Invoke-FastRelease.ps1 -Version 0.14.8
pwsh -NoProfile -File tools/release/Invoke-FastRelease.ps1 -Version 0.14.8 -Publish -NotesFile notes.md
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidatePattern('^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$')][string]$Version,
    # Default: the main worktree of the repository this script is in.
    [string]$MainCheckout,
    [string]$WorktreePath = 'C:\mgs-coop-wt2\release',
    [string]$ReleaseBranch = 'wp/release',
    [string]$DevBranch = 'dev/oct7-continue',
    # Where a new release worktree copies its ignored dependencies from (read only).
    [string]$DepsFrom = 'C:\mgs-coop-wt2\rel0147',
    [ValidateRange(1, 64)][int]$BuildJobs = 8,
    [ValidateRange(1, 64)][int]$TestJobs = 8,
    # One line for the bump commit body and the STATUS heading, e.g. 'round r1102: ...'.
    [string]$Summary,
    # The last paragraph of every commit this script makes ('' for none).
    [string]$Trailer = 'Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>',
    [string]$LogDirectory,
    [string]$SummaryPath,
    [ValidateRange(1, 1440)][int]$LockTimeoutMinutes = 90,
    [switch]$DryRun,
    # Stop after this step (nothing later runs; a dry run still returns to the release branch).
    [ValidateSet('preflight', 'worktree', 'merge', 'privacy', 'bump')][string]$StopAfter,
    [switch]$Publish,
    # -Publish: the release notes (written separately, by a person or an agent). Required with -Publish.
    [string]$NotesFile,
    # -Publish: the PR body (default: a short generated one).
    [string]$PrBodyFile,
    [ValidatePattern('^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$')][string]$GitHubRepo = 'TrollTaylor/diamond-dogs-together',
    [string]$GitHubBase = 'main',
    [string]$Remote = 'origin',
    # The privacy grep's identity; defaults: this profile folder, git user.name and user.email of the main checkout.
    [string]$PrivacyProfilePath = $env:USERPROFILE,
    [string]$PrivacyName,
    [string]$PrivacyEmail
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
Import-Module (Join-Path $PSScriptRoot 'FastRelease.psm1') -Force -DisableNameChecking
Import-Module (Join-Path (Split-Path -Parent $PSScriptRoot) 'VerifyLock.psm1') -Force -DisableNameChecking
Remove-Item Env:MGS_GAME_LUA_DIR -ErrorAction SilentlyContinue

$stamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ')
if (!$LogDirectory) { $LogDirectory = Join-Path ([IO.Path]::GetTempPath()) "mgs-coop-fast-release\$Version-$stamp" }
$null = New-Item -ItemType Directory -Path $LogDirectory -Force
$LogDirectory = (Resolve-Path -LiteralPath $LogDirectory).Path
if (!$SummaryPath) { $SummaryPath = Join-Path $LogDirectory 'summary.json' }
$runLog = Join-Path $LogDirectory 'run.log'
$report = New-FastReleaseSummary -Version $Version -DryRun ([bool]$DryRun) -Publish ([bool]$Publish)
$report.log_dir = $LogDirectory
$report.dev_branch = $DevBranch
$report.release_branch = $ReleaseBranch
$report.release_worktree = $WorktreePath
$clock = [Diagnostics.Stopwatch]::StartNew()
$state = @{ Detached = $false; LockOwner = $null; LockPath = (Get-VerifyLockPath); Stopped = $false }

function Say([string]$Text) {
    $line = "[$([DateTime]::Now.ToString('HH:mm:ss'))] $Text"
    Add-Content -LiteralPath $runLog -Value $line -Encoding utf8
    Write-Host $line
}
function Save-Summary {
    $report.total_seconds = [Math]::Round($clock.Elapsed.TotalSeconds, 1)
    $dir = Split-Path -Parent $SummaryPath
    if ($dir) { $null = New-Item -ItemType Directory -Path $dir -Force }
    [IO.File]::WriteAllText($SummaryPath, ($report | ConvertTo-Json -Depth 10), [Text.UTF8Encoding]::new($false))
}
function Step([string]$Name, [scriptblock]$Body) {
    if ($state.Stopped) { return }
    $entry = [ordered]@{ name = $Name; status = 'running'; seconds = $null; note = $null }
    $report.steps += $entry
    Say "== $Name"
    $sw = [Diagnostics.Stopwatch]::StartNew()
    try {
        $state.Note = $null
        $null = & $Body
        $entry.status = 'ok'
        $entry.note = $state.Note
    } catch {
        $entry.status = 'failed'
        $report.failed_step = $Name
        throw
    } finally {
        $entry.seconds = [Math]::Round($sw.Elapsed.TotalSeconds, 1)
        Say "   $Name $($entry.status) in $($entry.seconds) s$(if ($entry.note) { ": $($entry.note)" })"
        Save-Summary
    }
    if ($StopAfter -and $StopAfter -ceq $Name) { $state.Stopped = $true; Say "Stopping after $Name (-StopAfter)." }
}
function RunGit([string]$Path, [string[]]$Arguments, [switch]$AllowFail) { Invoke-FastReleaseGit -Path $Path -Arguments $Arguments -AllowFail:$AllowFail }
function Write-MessageFile([string]$Name, [string]$Text) {
    $path = Join-Path $LogDirectory $Name
    [IO.File]::WriteAllText($path, $Text, [Text.UTF8Encoding]::new($false))
    return $path
}
# Runs a program with its output logged and echoed; returns exit code and lines.
function Invoke-Logged([string]$LogName, [string]$WorkingDirectory, [string]$Program, [string[]]$Arguments) {
    $log = Join-Path $LogDirectory $LogName
    Push-Location -LiteralPath $WorkingDirectory
    try {
        $lines = @(& $Program @Arguments 2>&1 | ForEach-Object {
            $l = [string]$_
            Add-Content -LiteralPath $log -Value $l -Encoding utf8
            Write-Host "   | $l"
            $l
        })
        $code = $LASTEXITCODE
    } finally { Pop-Location }
    return [pscustomobject]@{ Exit = $code; Lines = $lines }
}
function Join-Message([string[]]$Paragraphs) {
    $all = @($Paragraphs | Where-Object { $_ })
    if ($Trailer) { $all += $Trailer }
    return ($all -join "`n`n") + "`n"
}

$script:BoundPrivacy = @{} + $PSBoundParameters
$failed = $null
try {
    Step 'preflight' {
        if ($Publish -and $DryRun) { throw '-Publish and -DryRun exclude each other.' }
        if ($Publish -and !$NotesFile) { throw '-Publish needs -NotesFile (the release notes are written separately).' }
        if ($NotesFile -and !(Test-Path -LiteralPath $NotesFile -PathType Leaf)) { throw "-NotesFile $NotesFile does not exist." }
        if ($PrBodyFile -and !(Test-Path -LiteralPath $PrBodyFile -PathType Leaf)) { throw "-PrBodyFile $PrBodyFile does not exist." }
        # gh runs in the main checkout: full paths.
        if ($NotesFile) { $script:NotesFile = (Resolve-Path -LiteralPath $NotesFile).Path }
        if ($PrBodyFile) { $script:PrBodyFile = (Resolve-Path -LiteralPath $PrBodyFile).Path }
        if (!$MainCheckout) {
            $porcelain = (RunGit $PSScriptRoot @('worktree', 'list', '--porcelain')).Lines
            $MainCheckout = ((@($porcelain | Where-Object { $_ -like 'worktree *' }))[0]).Substring(9)
        }
        $script:MainCheckout = [IO.Path]::GetFullPath($MainCheckout)
        $script:WorktreePath = [IO.Path]::GetFullPath($WorktreePath)
        $report.main_checkout = $script:MainCheckout
        $report.release_worktree = $script:WorktreePath
        if ($script:MainCheckout.TrimEnd('\') -ieq $script:WorktreePath.TrimEnd('\')) { throw 'The release worktree must not be the main checkout.' }
        Assert-FastReleaseCheckout -Path $script:MainCheckout -Branch $DevBranch -What 'The main checkout' -TrackedOnly
        $state.DevBefore = (RunGit $script:MainCheckout @('rev-parse', "refs/heads/$DevBranch")).Text
        $report.dev_before = $state.DevBefore
        $devCmake = (RunGit $script:MainCheckout @('show', "$($state.DevBefore):CMakeLists.txt")).Text
        if ($devCmake -notmatch 'project\(mgs_coop VERSION ([0-9]+\.[0-9]+\.[0-9]+) LANGUAGES CXX\)') { throw 'dev has no project version in CMakeLists.txt.' }
        $state.DevVersion = $Matches[1]
        if ([version]$Version -le [version]$state.DevVersion) { throw "-Version $Version is not newer than dev's $($state.DevVersion)." }
        if (Test-Path -LiteralPath $script:WorktreePath) {
            Assert-FastReleaseCheckout -Path $script:WorktreePath -Branch $ReleaseBranch -What 'The release worktree'
            $a = (RunGit $script:MainCheckout @('rev-parse', '--path-format=absolute', '--git-common-dir')).Text
            $b = (RunGit $script:WorktreePath @('rev-parse', '--path-format=absolute', '--git-common-dir')).Text
            if ([IO.Path]::GetFullPath($a) -ine [IO.Path]::GetFullPath($b)) { throw "The release worktree $($script:WorktreePath) belongs to another repository." }
        }
        $idArgs = @{ Repo = $script:MainCheckout; GitHubRepo = $GitHubRepo; ProfilePath = $PrivacyProfilePath }
        if ($script:BoundPrivacy.ContainsKey('PrivacyName')) { $idArgs.Name = $PrivacyName }
        if ($script:BoundPrivacy.ContainsKey('PrivacyEmail')) { $idArgs.Email = $PrivacyEmail }
        $state.Identity = Get-FastReleaseIdentity @idArgs
        if ($Publish) {
            $auth = Invoke-Logged 'gh.log' $script:MainCheckout 'gh' @('auth', 'status')
            if ($auth.Exit -ne 0) { throw 'gh is not signed in (gh auth status failed).' }
            $view = Invoke-Logged 'gh.log' $script:MainCheckout 'gh' @('release', 'view', "v$Version", '--repo', $GitHubRepo, '--json', 'tagName')
            if ($view.Exit -eq 0) { throw "The release v$Version already exists on $GitHubRepo." }
        }
        $state.Note = "dev $($state.DevBefore.Substring(0, 8)) at $($state.DevVersion)"
    }

    Step 'worktree' {
        $wt = $script:WorktreePath
        if (!(Test-Path -LiteralPath $wt)) {
            $exists = (RunGit $script:MainCheckout @('show-ref', '--verify', '--quiet', "refs/heads/$ReleaseBranch") -AllowFail).Exit -eq 0
            $add = if ($exists) { @('worktree', 'add', $wt, $ReleaseBranch) } else { @('worktree', 'add', '-b', $ReleaseBranch, $wt, $state.DevBefore) }
            $null = RunGit $script:MainCheckout $add
            $state.Note = "created on $ReleaseBranch"
        }
        $copied = [Collections.Generic.List[string]]::new()
        $deps = @('third_party\minhook', 'work\lua-5.1.5', 'work\upstream-dynamite')
        if ($DepsFrom -and (Test-Path -LiteralPath (Join-Path $DepsFrom 'build'))) {
            $deps += @(Get-ChildItem -LiteralPath (Join-Path $DepsFrom 'build') -Directory -Filter '*-2026*' | ForEach-Object { "build\$($_.Name)" })
        }
        foreach ($dep in $deps) {
            $target = Join-Path $wt $dep
            if ((Test-Path -LiteralPath $target) -and @(Get-ChildItem -LiteralPath $target -Force | Select-Object -First 1).Count) { continue }
            $source = if ($DepsFrom) { Join-Path $DepsFrom $dep } else { $null }
            if (!$source -or !(Test-Path -LiteralPath $source)) { throw "The release worktree lacks $dep and -DepsFrom '$DepsFrom' has none to copy." }
            # robocopy /E: hidden items too (minhook is a git clone with its own .git folder).
            $null = & robocopy $source $target /E /R:1 /W:1 /NFL /NDL /NJH /NJS /NP
            if ($LASTEXITCODE -ge 8) { throw "Copying $dep from $source failed (robocopy exit $LASTEXITCODE)." }
            $copied.Add($dep)
        }
        if ($copied.Count) { $state.Note = (@($state.Note, "copied $($copied -join ', ')") | Where-Object { $_ }) -join '; ' }
        Assert-FastReleaseCheckout -Path $wt -Branch $ReleaseBranch -What 'The release worktree'
    }

    Step 'merge' {
        $wt = $script:WorktreePath
        if ($DryRun) {
            $null = RunGit $wt @('switch', '--quiet', '--detach', $ReleaseBranch)
            $state.Detached = $true
        }
        $contained = (RunGit $wt @('merge-base', '--is-ancestor', $state.DevBefore, 'HEAD') -AllowFail).Exit -eq 0
        if ($contained) {
            $state.Note = "$ReleaseBranch already contains dev"
        } else {
            $msg = Write-MessageFile 'merge-dev.txt' (Join-Message @("Merge $DevBranch into $ReleaseBranch for $Version"))
            $r = RunGit $wt @('merge', '--no-ff', '--no-edit', '-F', $msg, $state.DevBefore) -AllowFail
            if ($r.Exit -ne 0) {
                $conflicts = @((RunGit $wt @('diff', '--name-only', '--diff-filter=U') -AllowFail).Lines | Where-Object { $_ })
                $null = RunGit $wt @('merge', '--abort') -AllowFail
                if ($conflicts.Count) { throw "Merging $DevBranch into $ReleaseBranch conflicts in: $($conflicts -join ', '). The merge was aborted; resolve it in $wt by hand (merge and commit), then run again." }
                throw "Merging $DevBranch into $ReleaseBranch failed: $($r.Text)"
            }
        }
        $report.release_merge_commit = (RunGit $wt @('rev-parse', 'HEAD')).Text
    }

    Step 'privacy' {
        $hits = @(Find-FastReleaseTrackedPrivacyHits -Repo $script:WorktreePath -Identity $state.Identity)
        $report.privacy.tracked_hits = @(Format-FastReleasePrivacyHits $hits)
        if ($hits.Count) { throw "Privacy grep: $(@($report.privacy.tracked_hits).Count) hit(s) in tracked files: $((@($report.privacy.tracked_hits) | Select-Object -First 10) -join '; ')" }
        $state.Note = "$(@($state.Identity.Needles).Count) needles, no hits"
    }

    Step 'bump' {
        $wt = $script:WorktreePath
        $current = Get-FastReleaseProjectVersion $wt
        $subject = "chore(release): bump version to $Version"
        if ($current -ceq $Version) {
            if ((RunGit $wt @('log', '-1', '--format=%s')).Text -cne $subject) { throw "The release worktree is already at $Version, but HEAD is not its bump commit." }
            $parentCmake = (RunGit $wt @('show', 'HEAD~1:CMakeLists.txt')).Text
            $null = $parentCmake -match 'project\(mgs_coop VERSION ([0-9]+\.[0-9]+\.[0-9]+) LANGUAGES CXX\)'
            $report.previous_version = $Matches[1]
            $state.Note = 'reused the bump commit at HEAD'
        } else {
            if ($current -cne $state.DevVersion) { throw "$ReleaseBranch is at $current but $DevBranch at $($state.DevVersion): an unfinished release of $current is on $ReleaseBranch. Finish it (-Version $current), or revert its bump there with a new commit (git revert) first." }
            $report.previous_version = $current
            $files = @(Invoke-FastReleaseBump -Root $wt -Previous $current -Version $Version)
            $null = RunGit $wt (@('add', '--') + $files)
            $msg = Write-MessageFile 'bump.txt' (Get-FastReleaseBumpMessage -Previous $current -Version $Version -Summary $Summary -Trailer $Trailer)
            $null = RunGit $wt @('commit', '--quiet', '-F', $msg)
            $state.Note = "$current -> $Version, $($files.Count) files"
        }
        $report.bump_commit = (RunGit $wt @('rev-parse', 'HEAD')).Text
        $dirty = @((RunGit $wt @('status', '--porcelain')).Lines | Where-Object { $_ })
        if ($dirty.Count) { throw "The release worktree is not clean after the bump: $($dirty -join '; ')" }
    }

    try {
        Step 'lock' {
            $owner = "fast-release $Version worker-checks+package pid=$PID start=$([DateTime]::UtcNow.ToString('o')) checkout=$($script:WorktreePath)"
            Enter-FastReleaseVerifyLock -LockPath $state.LockPath -Owner $owner -TimeoutMinutes $LockTimeoutMinutes -Log { param($t) Say "   $t" }
            $state.LockOwner = $owner
            Say "   verify lock taken: $owner"
            Wait-CtestReaders -TimeoutMinutes 60
        }
        Step 'gate' {
            $wt = $script:WorktreePath
            $r = Invoke-Logged 'gate.log' $wt 'pwsh' @('-NoProfile', '-File', (Join-Path $wt 'tools\worker\Invoke-WorkerChecks.ps1'), '-Profile', 'Release', '-BuildJobs', "$BuildJobs", '-TestJobs', "$TestJobs")
            $receiptPath = Get-FastReleaseReceiptPath $r.Lines
            if (!$receiptPath) { throw "The Release checks wrote no receipt (exit $($r.Exit)); see $(Join-Path $LogDirectory 'gate.log')." }
            $receipt = Get-Content -LiteralPath $receiptPath -Raw | ConvertFrom-Json
            $report.receipt.id = [string]$receipt.run_id
            $report.receipt.path = $receiptPath
            $report.receipt.status = [string]$receipt.status
            $report.receipt.audited_tests = @($receipt.plan.tests).Count
            if ($r.Exit -ne 0 -or $receipt.status -cne 'passed') { throw "The Release checks did not pass (exit $($r.Exit), $($receipt.status)): $($receipt.failure)" }
            $state.Receipt = $receiptPath
            $state.Note = "$($report.receipt.id), $($report.receipt.audited_tests) audited tests"
        }
        Step 'package' {
            $wt = $script:WorktreePath
            $r = Invoke-Logged 'package.log' $wt 'pwsh' @('-NoProfile', '-File', (Join-Path $wt 'tools\package.ps1'), '-Friend', '-CheckReceipt', $state.Receipt)
            if ($r.Exit -ne 0) { throw "tools/package.ps1 failed (exit $($r.Exit)); see $(Join-Path $LogDirectory 'package.log')." }
            $state.Package = Get-FastReleasePackageOutput $r.Lines
            $report.package_dir = $state.Package.PackageDir
            $report.source_files = $state.Package.SourceFiles
            $state.Note = $state.Package.PackageDir
        }
    } finally {
        if ($state.LockOwner) {
            if (Exit-FastReleaseVerifyLock -LockPath $state.LockPath -Owner $state.LockOwner) { Say '   verify lock released' }
            else { Say '   the verify lock was not ours at the end; left alone' }
            $state.LockOwner = $null
        }
    }

    Step 'facts' {
        $p = $state.Package
        $pkg = Get-Content -LiteralPath (Join-Path $p.PackageDir 'package\mgs-coop\package.json') -Raw | ConvertFrom-Json
        if ([string]$pkg.version -cne $Version) { throw "package.json says version $($pkg.version), not $Version." }
        if ([string]$pkg.build.source_commit -cne $report.bump_commit) { throw "package.json source_commit $($pkg.build.source_commit) is not the bump commit $($report.bump_commit)." }
        if ($pkg.build.source_dirty -ne $false) { throw "package.json source_dirty is $($pkg.build.source_dirty), not false." }
        $report.zips = @((Get-FastReleaseFileInfo $p.PlayerZip), (Get-FastReleaseFileInfo $p.SourceZip))
        $report.exe = Get-FastReleaseFileInfo (Join-Path $p.PackageDir 'package\DiamondDogsTogether.exe')
        $report.dll = Get-FastReleaseFileInfo (Join-Path $p.PackageDir 'package\dinput8.dll')
        if ($report.exe.sha256 -cne ([string]$pkg.build.launcher_sha256).ToUpperInvariant() -or $report.dll.sha256 -cne ([string]$pkg.build.loader_sha256).ToUpperInvariant()) { throw 'The packaged exe or dll is not the receipt''s.' }
        $ids = [ordered]@{ exe = @(Get-FastReleaseBuildIds $report.exe.path); dll = @(Get-FastReleaseBuildIds $report.dll.path) }
        $report.build_ids = $ids
        foreach ($k in @('exe', 'dll')) {
            if (!(Test-FastReleaseBuildIds $ids[$k] $Version $report.bump_commit)) { throw "The $k's build ids ($($ids[$k] -join ', ')) are not exactly $Version+$($report.bump_commit.Substring(0, 7))." }
        }
        $state.Note = "build id $($ids.dll[0])"
    }

    Step 'defender' {
        $mp = Join-Path $env:ProgramFiles 'Windows Defender\MpCmdRun.exe'
        if (!(Test-Path -LiteralPath $mp -PathType Leaf)) { throw "MpCmdRun.exe not found at $mp." }
        try { $st = Get-MpComputerStatus; $report.defender.engine = [string]$st.AMEngineVersion; $report.defender.signatures = [string]$st.AntivirusSignatureVersion }
        catch { $report.defender.engine = 'unknown'; $report.defender.signatures = 'unknown' }
        $files = @($report.exe.path, $report.dll.path, $report.zips[0].path, $report.zips[1].path)
        $results = @()
        foreach ($f in $files) {
            $before = (Get-FileHash -LiteralPath $f -Algorithm SHA256).Hash
            $r = Invoke-Logged 'defender.log' (Split-Path -Parent $f) $mp @('-Scan', '-ScanType', '3', '-File', $f, '-DisableRemediation')
            $after = (Get-FileHash -LiteralPath $f -Algorithm SHA256).Hash
            $clean = Test-FastReleaseDefenderClean $r.Exit $r.Lines
            $results += [ordered]@{ file = $f; exit = $r.Exit; clean = $clean; hash_unchanged = ($before -ceq $after) }
        }
        $report.defender.files = $results
        $bad = @($results | Where-Object { !$_.clean -or !$_.hash_unchanged })
        $report.defender.result = if ($bad.Count) { 'FAILED' } else { 'found no threats' }
        if ($bad.Count) { throw "Windows Defender: $(@($bad | ForEach-Object { "$(Split-Path -Leaf $_.file) exit $($_.exit) clean=$($_.clean) hash_unchanged=$($_.hash_unchanged)" }) -join '; ')" }
        $state.Note = "4 files, no threats (engine $($report.defender.engine), signatures $($report.defender.signatures))"
    }

    Step 'zip-privacy' {
        $all = @()
        foreach ($z in $report.zips) { $all += @(Find-FastReleaseZipPrivacyHits -ZipPath $z.path -Identity $state.Identity | ForEach-Object { $_.Source = "$($z.name)!$($_.Source)"; $_ }) }
        $report.privacy.zip_hits = @(Format-FastReleasePrivacyHits $all)
        if ($all.Count) { throw "Privacy grep: $(@($report.privacy.zip_hits).Count) hit(s) in the zips: $((@($report.privacy.zip_hits) | Select-Object -First 10) -join '; ')" }
        $state.Note = 'both zips, no hits'
    }

    Step 'record' {
        $wt = $script:WorktreePath
        $file = { param($i) @{ Bytes = $i.bytes; Sha256 = $i.sha256 } }
        $info = [pscustomobject]@{
            Version = $Version; Previous = $report.previous_version; Date = (Get-Date); Summary = $Summary
            PackageDirName = (Split-Path -Leaf $report.package_dir); BumpCommit = $report.bump_commit; PrivacyCommit = $report.release_merge_commit
            ReceiptId = $report.receipt.id; AuditedTests = $report.receipt.audited_tests; BuildJobs = $BuildJobs; TestJobs = $TestJobs
            SourceFiles = $report.source_files; PlayerZip = (& $file $report.zips[0]); SourceZip = (& $file $report.zips[1])
            Exe = (& $file $report.exe); Dll = (& $file $report.dll)
            DefenderEngine = $report.defender.engine; DefenderSignatures = $report.defender.signatures
        }
        $lines = Format-FastReleaseStatusRecord -Info $info
        [IO.File]::WriteAllText((Join-Path $LogDirectory 'status-record.md'), ($lines -join "`n"), [Text.UTF8Encoding]::new($false))
        Add-FastReleaseStatusRecord -Path (Join-Path $wt 'docs\STATUS.md') -Lines $lines
        $null = RunGit $wt @('add', '--', 'docs/STATUS.md')
        $msg = Write-MessageFile 'record.txt' (Get-FastReleaseRecordMessage -Info $info -Trailer $Trailer)
        $null = RunGit $wt @('commit', '--quiet', '-F', $msg)
        $report.record_commit = (RunGit $wt @('rev-parse', 'HEAD')).Text
        $report.release_head = $report.record_commit
    }

    Step 'dev-merge' {
        $wt = $script:WorktreePath
        $releaseTree = (RunGit $wt @('rev-parse', 'HEAD^{tree}')).Text
        if ($DryRun) {
            $r = RunGit $wt @('merge-tree', '--write-tree', $state.DevBefore, 'HEAD') -AllowFail
            if ($r.Exit -ne 0) { throw "Merging the release into $DevBranch would conflict: $($r.Text)" }
            $tree = ($r.Lines | Select-Object -First 1).Trim()
            $report.trees_match = ($tree -ceq $releaseTree)
            if (!$report.trees_match) { throw "The computed dev merge tree $tree differs from the release tree $releaseTree." }
            $state.Note = "dry run: computed with git merge-tree, tree $($tree.Substring(0, 8)) = release tree"
        } else {
            $main = $script:MainCheckout
            Assert-FastReleaseCheckout -Path $main -Branch $DevBranch -What 'The main checkout' -TrackedOnly
            $now = (RunGit $main @('rev-parse', "refs/heads/$DevBranch")).Text
            if ($now -cne $state.DevBefore) { throw "$DevBranch moved from $($state.DevBefore) to $now during the release; run again (the bump commit is reused)." }
            $msg = Write-MessageFile 'merge-release.txt' (Join-Message @("Merge ${ReleaseBranch}: release $Version"))
            $r = RunGit $main @('merge', '--no-ff', '--no-edit', '-F', $msg, "refs/heads/$ReleaseBranch") -AllowFail
            if ($r.Exit -ne 0) {
                $conflicts = @((RunGit $main @('diff', '--name-only', '--diff-filter=U') -AllowFail).Lines | Where-Object { $_ })
                $null = RunGit $main @('merge', '--abort') -AllowFail
                throw "Merging $ReleaseBranch into $DevBranch failed$(if ($conflicts.Count) { " (conflicts in $($conflicts -join ', '))" }); aborted: $($r.Text)"
            }
            $report.dev_merge_commit = (RunGit $main @('rev-parse', 'HEAD')).Text
            $devTree = (RunGit $main @('rev-parse', 'HEAD^{tree}')).Text
            $report.trees_match = ($devTree -ceq $releaseTree)
            if (!$report.trees_match) { throw "After the merge $DevBranch's tree $devTree differs from $ReleaseBranch's $releaseTree." }
            $state.Note = "$($report.dev_merge_commit.Substring(0, 8)), trees match"
        }
    }

    if ($Publish) {
        Step 'publish' {
            $main = $script:MainCheckout
            $pub = [ordered]@{ pushed = $false; pr = $null; merge_commit = $null; release = $null; draft_checked = $false; published = $false; checked = $false }
            $report.publish_result = $pub
            $r = Invoke-Logged 'publish.log' $main 'git' @('push', $Remote, "refs/heads/${DevBranch}:refs/heads/${DevBranch}")
            if ($r.Exit -ne 0) { throw "git push failed (exit $($r.Exit))." }
            $pub.pushed = $true
            $body = $PrBodyFile
            if (!$body) {
                $z = $report.zips
                $body = Write-MessageFile 'pr-body.md' ((@(
                    "Release $Version (``tools/release/Invoke-FastRelease.ps1``).", '',
                    "- Release receipt ``$($report.receipt.id)`` ($($report.receipt.audited_tests) audited tests, passed)",
                    "- ``$($z[0].name)``: $(Format-FastReleaseBytes $z[0].bytes) bytes, SHA-256 ``$($z[0].sha256)``",
                    "- ``$($z[1].name)``: $(Format-FastReleaseBytes $z[1].bytes) bytes, SHA-256 ``$($z[1].sha256)``",
                    "- Windows Defender: $($report.defender.result); privacy grep: no hits", '',
                    "$([char]::ConvertFromUtf32(0x1F916)) Generated with [Claude Code](https://claude.com/claude-code)") -join "`n") + "`n")
            }
            $r = Invoke-Logged 'publish.log' $main 'gh' @('pr', 'create', '--repo', $GitHubRepo, '--base', $GitHubBase, '--head', $DevBranch, '--title', "Release $Version", '--body-file', $body)
            $prUrl = @($r.Lines | Where-Object { $_ -match '^https://github\.com/.+/pull/\d+' }) | Select-Object -Last 1
            if ($r.Exit -ne 0 -or !$prUrl) { throw "gh pr create failed (exit $($r.Exit))." }
            $pub.pr = $prUrl
            $r = Invoke-Logged 'publish.log' $main 'gh' @('pr', 'merge', $prUrl, '--repo', $GitHubRepo, '--merge')
            if ($r.Exit -ne 0) { throw "gh pr merge failed (exit $($r.Exit))." }
            $r = Invoke-Logged 'publish.log' $main 'gh' @('pr', 'view', $prUrl, '--repo', $GitHubRepo, '--json', 'mergeCommit', '--jq', '.mergeCommit.oid')
            $oid = @($r.Lines | Where-Object { $_ -match '^[0-9a-f]{40}$' }) | Select-Object -Last 1
            if ($r.Exit -ne 0 -or !$oid) { throw 'Could not read the PR merge commit.' }
            $pub.merge_commit = $oid
            $r = Invoke-Logged 'publish.log' $main 'gh' @('release', 'create', "v$Version", '--repo', $GitHubRepo, '--target', $oid, '--title', "Diamond Dogs Together $Version",
                '--notes-file', $NotesFile, '--prerelease', '--draft', $report.zips[0].path, $report.zips[1].path)
            if ($r.Exit -ne 0) { throw "gh release create failed (exit $($r.Exit))." }
            $pub.release = "v$Version"
            $check = @('-NoProfile', '-File', (Join-Path $script:WorktreePath 'tools\check-release.ps1'), '-Version', $Version, '-PackageDir', $report.package_dir, '-Repo', $GitHubRepo)
            $r = Invoke-Logged 'publish.log' $main 'pwsh' $check
            if ($r.Exit -ne 0) { throw 'tools/check-release.ps1 refused the draft release; it stays a draft.' }
            $pub.draft_checked = $true
            $r = Invoke-Logged 'publish.log' $main 'gh' @('release', 'edit', "v$Version", '--repo', $GitHubRepo, '--draft=false')
            if ($r.Exit -ne 0) { throw "gh release edit --draft=false failed (exit $($r.Exit))." }
            $pub.published = $true
            $r = Invoke-Logged 'publish.log' $main 'pwsh' $check
            if ($r.Exit -ne 0) { throw 'tools/check-release.ps1 refused the published release.' }
            $pub.checked = $true
            $state.Note = "$prUrl, v$Version published"
        }
    }
} catch {
    $failed = $_
    $report.error = $_.Exception.Message
    Say "FAILED: $($_.Exception.Message)"
} finally {
    if ($state.Detached) {
        $wt = $script:WorktreePath
        $dirty = @((Invoke-FastReleaseGit $wt @('status', '--porcelain') -AllowFail).Lines | Where-Object { $_ })
        if ($dirty.Count) {
            Say "The dry run left changes in $wt on a detached HEAD; it stays there: $($dirty -join '; ')"
        } else {
            $null = Invoke-FastReleaseGit $wt @('switch', '--quiet', $ReleaseBranch) -AllowFail
            Say "Dry run: $wt is back on $ReleaseBranch (the dry-run commits are on no branch)."
        }
    }
    $report.status = if ($failed) { 'failed' } elseif ($state.Stopped) { 'stopped' } else { 'passed' }
    $report.finished_utc = [DateTime]::UtcNow.ToString('o')
    Save-Summary
}

Write-Host ''
Write-Host "Fast release $Version$(if ($DryRun) { ' (dry run)' }): $($report.status.ToUpperInvariant())$(if ($failed) { " at $($report.failed_step): $($report.error)" })"
if ($report.receipt.id) { Write-Host "  receipt   $($report.receipt.id) ($($report.receipt.audited_tests) audited tests, $($report.receipt.status))" }
foreach ($z in @($report.zips)) { Write-Host ("  zip       {0}  {1} bytes  SHA-256 {2}" -f $z.name, (Format-FastReleaseBytes $z.bytes), $z.sha256) }
if ($report.exe) { Write-Host "  exe       SHA-256 $($report.exe.sha256)"; Write-Host "  dll       SHA-256 $($report.dll.sha256)" }
if ($report.defender.result) { Write-Host "  defender  $($report.defender.result)" }
if ($report.package_dir) { Write-Host "  dist      $($report.package_dir)" }
foreach ($s in $report.steps) { Write-Host ("  {0,-12} {1,7:N1} s  {2}" -f $s.name, $s.seconds, $s.status) }
Write-Host ("  total        {0,7:N1} s" -f $report.total_seconds)
Write-Host "  summary   $SummaryPath"
if ($failed) { exit 1 }
exit 0
