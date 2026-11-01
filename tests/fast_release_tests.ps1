#Requires -Version 7
# tools/release/Invoke-FastRelease.ps1 and tools/release/FastRelease.psm1 without a build, game or network: argument
# validation and refusals (wrong branch, dirty trees, an older version, a merge conflict, a privacy hit), the version
# bump against the real 0.14.6 and 0.14.7 bump commits (6c25276a, 521b82b1) when the history is there, the privacy
# grep (tracked files, zip entries, UTF-16), the output parsers, the STATUS record, the summary JSON and the verify-lock
# helpers. The script-level cases run on throw-away git repositories under %TEMP% with a made-up identity; the real
# checkout is only read (git cat-file / show of the two bump commits).
[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$root = Split-Path -Parent $PSScriptRoot
$releaseScript = Join-Path $root 'tools\release\Invoke-FastRelease.ps1'
Import-Module (Join-Path $root 'tools\release\FastRelease.psm1') -Force -DisableNameChecking
$checks = 0
function Check([bool]$Value, [string]$Message) { $script:checks++; if (!$Value) { throw "Assertion failed: $Message" } }
function Refuses([scriptblock]$Call, [string]$Pattern) {
    $caught = $null
    try { & $Call | Out-Null } catch { $caught = $_ }
    Check ($null -ne $caught -and $caught.Exception.Message -match $Pattern) "refuses /$Pattern/ (got: $(if ($caught) { $caught.Exception.Message } else { 'no error' }))"
}
$utf8 = [Text.UTF8Encoding]::new($false)
$temp = Join-Path ([IO.Path]::GetTempPath()) ("mgs-fast-release-tests-" + [Guid]::NewGuid().ToString('N').Substring(0, 10))
$null = New-Item -ItemType Directory -Path $temp
# A made-up identity: nothing personal in this file.
$fakeProfile = 'C:\Users\zedq'
$fakeName = 'Zebulon'
$fakeEmail = 'zebulon.q@example.invalid'
$identity = New-FastReleasePrivacyIdentity -ProfilePath $fakeProfile -Name $fakeName -Email $fakeEmail -AllowedContexts @('TrollZebulon')

function Get-BlobBytes([string]$Repo, [string]$Spec) {
    $psi = [Diagnostics.ProcessStartInfo]::new('git')
    foreach ($a in @('-C', $Repo, 'cat-file', 'blob', $Spec)) { $psi.ArgumentList.Add($a) }
    $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true; $psi.UseShellExecute = $false
    $p = [Diagnostics.Process]::Start($psi)
    $ms = [IO.MemoryStream]::new()
    $p.StandardOutput.BaseStream.CopyTo($ms)
    $null = $p.StandardError.ReadToEnd()
    $p.WaitForExit()
    if ($p.ExitCode -ne 0) { throw "git cat-file blob $Spec failed" }
    return , $ms.ToArray()
}
function Write-Text([string]$Path, [string]$Text) {
    $null = New-Item -ItemType Directory -Path (Split-Path -Parent $Path) -Force
    [IO.File]::WriteAllBytes($Path, $utf8.GetBytes($Text))
}
function FxGit([string]$Repo, [string[]]$Arguments) {
    $out = @(& git -C $Repo @Arguments 2>&1 | ForEach-Object { [string]$_ })
    if ($LASTEXITCODE -ne 0) { throw "fixture git $($Arguments -join ' ') failed: $($out -join ' | ')" }
    return (($out -join "`n").Trim())
}

try {
    # ---------------------------------------------------------------------------------------------- versions
    foreach ($good in @('0.14.8', '1.0.0', '10.20.30')) { Check (Test-FastReleaseVersion $good) "version $good" }
    foreach ($bad in @('0.14', '0.14.8.1', 'v0.14.8', '0.14.08', '0.14.8-rc', '')) { Check (!(Test-FastReleaseVersion $bad)) "not a version: '$bad'" }
    Check ((Get-FastReleaseNextPatch '0.14.7') -ceq '0.14.8') 'next patch'
    Check ((Get-FastReleaseNextPatch '0.15.0') -ceq '0.15.1') 'next patch after a minor bump'

    # ------------------------------------------------------------------------- the bump against the real bump commits
    $history = $true
    foreach ($c in @('521b82b1b2d7051441ed3d53938b3b59b28b59a5', '6c25276a')) { $null = & git -C $root cat-file -e "$c^{commit}" 2>$null; if ($LASTEXITCODE -ne 0) { $history = $false } }
    if ($history) {
        foreach ($case in @(@{ Commit = '6c25276a'; Previous = '0.14.5'; Version = '0.14.6' }, @{ Commit = '521b82b1'; Previous = '0.14.6'; Version = '0.14.7' })) {
            $changed = @(& git -C $root show --name-only --format= $case.Commit | Where-Object { $_ })
            Check ((($changed | Sort-Object) -join ',') -ceq ((Get-FastReleaseBumpFiles | Sort-Object) -join ',')) "the bump files are those of $($case.Commit)"
            foreach ($eol in @('LF', 'CRLF')) {
                $dir = Join-Path $temp "bump-$($case.Commit)-$eol"
                foreach ($f in Get-FastReleaseBumpFiles) {
                    $bytes = Get-BlobBytes $root "$($case.Commit)~1:$f"
                    $text = $utf8.GetString($bytes)
                    if ($eol -ceq 'CRLF') { $text = $text.Replace("`n", "`r`n") }
                    Write-Text (Join-Path $dir $f) $text
                }
                $files = @(Invoke-FastReleaseBump -Root $dir -Previous $case.Previous -Version $case.Version)
                Check ($files.Count -eq 8) "$($case.Commit) $eol bump changes 8 files"
                foreach ($f in Get-FastReleaseBumpFiles) {
                    $want = $utf8.GetString((Get-BlobBytes $root "$($case.Commit):$f"))
                    if ($eol -ceq 'CRLF') { $want = $want.Replace("`n", "`r`n") }
                    $got = [IO.File]::ReadAllText((Join-Path $dir $f), $utf8)
                    Check ($got -ceq $want) "$($case.Commit) $eol bump of $f is byte-identical to the commit"
                }
                Check ((Get-FastReleaseProjectVersion $dir) -ceq $case.Version) 'project version after the bump'
                # A second bump from the old version finds nothing and writes nothing.
                $before = [IO.File]::ReadAllText((Join-Path $dir 'README.md'))
                Refuses { Invoke-FastReleaseBump -Root $dir -Previous $case.Previous -Version $case.Version } 'CMakeLists.txt has .* 0 times'
                Check ([IO.File]::ReadAllText((Join-Path $dir 'README.md')) -ceq $before) 'a refused bump writes nothing'
            }
        }
    } else {
        Write-Host 'fast_release_tests: no git history (source zip?): the comparison with the real bump commits is skipped'
    }

    # ------------------------------------------------------------------------------------- the bump on a fixture
    $fx = Join-Path $temp 'bumpfx'
    function New-BumpFiles([string]$Dir, [string]$Prev, [string]$Cur, [string]$Nl = "`n") {
        $n = Get-FastReleaseNextPatch $Cur
        Write-Text (Join-Path $Dir 'CMakeLists.txt') "cmake_minimum_required(VERSION 3.20)${Nl}project(mgs_coop VERSION $Cur LANGUAGES CXX)${Nl}"
        Write-Text (Join-Path $Dir 'README.md') "# x${Nl}Windows - version $Cur (development build)${Nl}"
        Write-Text (Join-Path $Dir 'tools/package.ps1') "`$version='$Cur'${Nl}"
        Write-Text (Join-Path $Dir 'tools/probe.ps1') "product = 'p'; version = '$Cur';${Nl}"
        Write-Text (Join-Path $Dir 'tools/set-session-mode.ps1') "if(`$v -notin @('$Prev','$Cur')) { throw 'a' }${Nl}if(`$v -in @('$Prev','$Cur')) { 'b' }${Nl}"
        Write-Text (Join-Path $Dir 'tools/package-install.ps1') "if(`$v -notin @('$Prev','$Cur') -or `$x) { throw 'c' }${Nl}"
        Write-Text (Join-Path $Dir 'tools/dev/Play-LocalCoop.ps1') "if(`$v -notin @('$Prev','$Cur')) { throw 'd' }${Nl}"
        Write-Text (Join-Path $Dir 'tests/ddt_core_tests.cpp') "void T() {${Nl}    CHECK(IsVersionNewer(`"$Cur`", `"$Prev`"));${Nl}    CHECK(IsVersionNewer(`"$n`", `"$Cur`"));${Nl}    CHECK(!IsVersionNewer(`"$Cur`", `"$Cur`"));${Nl}}${Nl}"
    }
    New-BumpFiles $fx '1.2.2' '1.2.3' "`r`n"
    Refuses { Invoke-FastReleaseBump -Root $fx -Previous '1.2.3' -Version '1.2.3' } 'not newer'
    Refuses { Invoke-FastReleaseBump -Root $fx -Previous '1.2.3' -Version '1.2' } 'Bad version'
    $files = @(Invoke-FastReleaseBump -Root $fx -Previous '1.2.3' -Version '1.3.0')
    Check ($files.Count -eq 8) 'fixture bump: 8 files'
    $cpp = [IO.File]::ReadAllText((Join-Path $fx 'tests/ddt_core_tests.cpp'))
    Check ($cpp.Contains("    CHECK(IsVersionNewer(`"1.2.4`", `"1.2.3`"));`r`n    CHECK(IsVersionNewer(`"1.3.1`", `"1.3.0`"));`r`n")) 'the next IsVersionNewer check follows the previous one (CRLF kept)'
    Check (([IO.File]::ReadAllText((Join-Path $fx 'tools/set-session-mode.ps1'))) -ceq "if(`$v -notin @('1.2.2','1.2.3','1.3.0')) { throw 'a' }`r`nif(`$v -in @('1.2.2','1.2.3','1.3.0')) { 'b' }`r`n") 'allow-lists keep the previous version (both lines)'
    Check (([IO.File]::ReadAllText((Join-Path $fx 'tools/package.ps1'))) -ceq "`$version='1.3.0'`r`n") 'package.ps1 version'
    $msg = Get-FastReleaseBumpMessage -Previous '1.2.3' -Version '1.3.0' -Summary 'round x' -Trailer 'T: y'
    Check ($msg.StartsWith("chore(release): bump version to 1.3.0`n`nround x`n")) 'bump subject and summary'
    Check ($msg.TrimEnd().EndsWith("`n`nT: y")) 'trailer is the last paragraph'
    $fx2 = Join-Path $temp 'bumpfx2'
    New-BumpFiles $fx2 '1.2.2' '1.2.3'
    Write-Text (Join-Path $fx2 'tools/package-install.ps1') "@('1.2.2','1.2.3') @('1.2.0','1.2.3')`n"
    $readme = [IO.File]::ReadAllText((Join-Path $fx2 'README.md'))
    Refuses { Invoke-FastReleaseBump -Root $fx2 -Previous '1.2.3' -Version '1.2.4' } "package-install.ps1 has .* 2 times, expected 1"
    Check ([IO.File]::ReadAllText((Join-Path $fx2 'README.md')) -ceq $readme) 'a failing file stops the whole bump before any write'

    # -------------------------------------------------------------------------------------------------- privacy
    $text = "a`nC:\Users\zedq\x`nC:/Users/zedq/y`n`"C:\\Users\\zedq\\z`"`n/Users/zedqx/no`nTrollZebulon/repo`nzebulon here`nmail ZEBULON.Q@example.invalid`n"
    $hits = @(Find-FastReleasePrivacyHits -Identity $identity -Text $text -Source 'f')
    Check ((@($hits | Where-Object Kind -ceq 'profile path' | ForEach-Object Line) -join ',') -ceq '2,3,4') 'profile path in \, / and \\ spellings, not a longer user name'
    Check ((@($hits | Where-Object Kind -ceq 'persona name' | ForEach-Object Line) -join ',') -ceq '7,8') 'persona name anywhere but inside the owner login'
    Check ((@($hits | Where-Object Kind -ceq 'e-mail' | ForEach-Object Line | Select-Object -Unique) -join ',') -ceq '8') 'e-mail, any case'
    Check (@(Find-FastReleasePrivacyHits -Identity $identity -Text "clean`ntext with TrollZebulon`n").Count -eq 0) 'clean text'
    Check ((@(Format-FastReleasePrivacyHits $hits) | Select-Object -First 1) -ceq 'f:2: profile path') 'hits name file, line and kind, never the text'
    Refuses { New-FastReleasePrivacyIdentity } 'nothing to look for'
    Refuses { New-FastReleasePrivacyIdentity -ProfilePath 'C:\x' } 'too short'
    Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
    $zipPath = Join-Path $temp 'p.zip'
    $z = [IO.Compression.ZipFile]::Open($zipPath, 'Create')
    try {
        $put = { param($name, [byte[]]$bytes) $e = $z.CreateEntry($name); $s = $e.Open(); try { $s.Write($bytes, 0, $bytes.Length) } finally { $s.Dispose() } }
        & $put 'clean.txt' $utf8.GetBytes('nothing here, TrollZebulon')
        & $put 'prog.exe' ([byte[]](@(0, 1, 2) + [Text.Encoding]::Unicode.GetBytes('D:\Users\zedq\src\x.pdb') + @(0)))
        & $put 'notes/zebulon.md' $utf8.GetBytes('x')
    } finally { $z.Dispose() }
    $zh = @(Format-FastReleasePrivacyHits (Find-FastReleaseZipPrivacyHits -ZipPath $zipPath -Identity $identity))
    Check (($zh -join '|') -ceq 'prog.exe (UTF-16): profile path|notes/zebulon.md (entry name): persona name') "zip: UTF-16 text and entry names ($($zh -join '|'))"

    # --------------------------------------------------------------------------------------------------- parsers
    Check ((Get-FastReleaseReceiptPath @('Worker check: inventory', 'Worker check receipt: C:\w\build\worker-runs\a\receipt.json', 'Worker check receipt: C:\w\build\worker-runs\b\receipt.json')) -ceq 'C:\w\build\worker-runs\b\receipt.json') 'the last receipt line'
    Check ($null -eq (Get-FastReleaseReceiptPath @('no receipt'))) 'no receipt'
    $pkgOut = @('Packaging the Release loader and launcher of r.', 'Prepared: C:\w\dist\D-1\DiamondDogsTogether-1.2.4.zip', 'SHA-256: AA; loader BB; launcher CC',
        'Prepared source: C:\w\dist\D-1\DDT-source-1.2.4.zip', "Source SHA-256: $('A' * 64); files 1572", 'Upload BOTH zips')
    $po = Get-FastReleasePackageOutput $pkgOut
    Check ($po.PackageDir -ceq 'C:\w\dist\D-1' -and $po.SourceFiles -eq 1572 -and $po.PlayerZip.EndsWith('DiamondDogsTogether-1.2.4.zip') -and $po.SourceZip.EndsWith('DDT-source-1.2.4.zip')) 'package output'
    Refuses { Get-FastReleasePackageOutput @('Prepared: C:\w\dist\D-1\DiamondDogsTogether-1.2.4.zip') } 'did not report both zips'
    $clean = @('Scan starting...', 'Scan finished.', 'Scanning C:\x.zip found no threats.')
    Check (Test-FastReleaseDefenderClean 0 $clean) 'Defender: clean'
    Check (!(Test-FastReleaseDefenderClean 2 @('Scan starting...', 'Scanning C:\x.zip found 1 threats.', 'Threat information'))) 'Defender: a threat'
    Check (!(Test-FastReleaseDefenderClean 0 @('Scan starting...'))) 'Defender: no verdict is not clean'
    Check (!(Test-FastReleaseDefenderClean 1 $clean)) 'Defender: a failing exit is not clean'
    $prog = Join-Path $temp 'prog.bin'
    [IO.File]::WriteAllBytes($prog, [byte[]]([Text.Encoding]::ASCII.GetBytes("x 1.2.4+abc1234 y") + [Text.Encoding]::Unicode.GetBytes('1.2.4+abc1234')))
    $ids = @(Get-FastReleaseBuildIds $prog)
    Check (($ids -join ',') -ceq '1.2.4+abc1234') 'build ids, ASCII and UTF-16, unique'
    Check (Test-FastReleaseBuildIds $ids '1.2.4' 'abc1234ffff') 'build id matches the bump commit'
    Check (!(Test-FastReleaseBuildIds $ids '1.2.5' 'abc1234ffff') -and !(Test-FastReleaseBuildIds $ids '1.2.4' 'abd') -and !(Test-FastReleaseBuildIds @() '1.2.4' 'abc')) 'build id mismatches'

    # ---------------------------------------------------------------------------------------------------- record
    $info = [pscustomobject]@{ Version = '1.2.4'; Previous = '1.2.3'; Date = [datetime]'2026-11-02T10:00:00'; Summary = 'round x'
        PackageDirName = 'DiamondDogsTogether-1.2.4-20261102T100000Z-abcdef12'; BumpCommit = 'abc1234567890abc1234567890abc1234567890a'; PrivacyCommit = 'def5678901234567890def5678901234567890ab'
        ReceiptId = '20261102-100000-x'; AuditedTests = 334; BuildJobs = 8; TestJobs = 8; SourceFiles = 1572
        PlayerZip = @{ Bytes = 3221854; Sha256 = 'A' * 64 }; SourceZip = @{ Bytes = 9352627; Sha256 = 'B' * 64 }; Exe = @{ Bytes = 2380288; Sha256 = 'C' * 64 }; Dll = @{ Bytes = 7432192; Sha256 = 'D' * 64 }
        DefenderEngine = '1.1.1'; DefenderSignatures = '2.2.2' }
    $rec = @(Format-FastReleaseStatusRecord -Info $info)
    Check ($rec[0] -ceq '## November 2, 2026: 1.2.4 package (round x)') 'record heading'
    Check ($rec[2].StartsWith('**Package (2026-11-02):** `dist/DiamondDogsTogether-1.2.4-20261102T100000Z-abcdef12/`') -and $rec[2].Contains('`abc1234`') -and $rec[2].Contains('`20261102-100000-x` (passed, 334 audited tests') -and $rec[2].Contains('build id `1.2.4+abc1234`')) 'record package line'
    Check (@($rec | Where-Object { $_.StartsWith('| `') }).Count -eq 4) 'four table rows'
    Check ($rec -ccontains ('| `DDT-source-1.2.4.zip` (1,572 source files plus `source-files.json`) | 9,352,627 | `' + ('B' * 64) + '` |')) 'source row with thousands separators'
    Check (@($rec | Where-Object { $_ -like '- **Windows Defender**' + '*engine 1.1.1, signatures 2.2.2*' }).Count -eq 1) 'Defender line'
    Check (@($rec | Where-Object { $_ -like '*IsVersionNewer("1.2.5", "1.2.4")*' -and $_ -like '*still 1.2.3*' }).Count -eq 1) 'version line'
    Check (@($rec | Where-Object { $_ -like '*tools/check-release.ps1 -Version 1.2.4 -PackageDir dist/DiamondDogsTogether-1.2.4-20261102T100000Z-abcdef12*' }).Count -eq 1) 'release line'
    Check ($rec[-1] -ceq '') 'record ends with a blank line'
    Check (!(($rec -join "`n").Contains('zedq')) -and !(($rec -join "`n") -match 'Users')) 'record carries no path'
    $status = Join-Path $temp 'STATUS.md'
    [IO.File]::WriteAllText($status, "## older`r`n`r`ntext`r`n")
    Add-FastReleaseStatusRecord -Path $status -Lines $rec
    $st = [IO.File]::ReadAllText($status)
    Check ($st.StartsWith("## November 2, 2026: 1.2.4 package (round x)`r`n`r`n**Package") -and $st.EndsWith("`r`n`r`n## older`r`n`r`ntext`r`n")) 'record on top, CRLF kept, a blank line before the older entry'
    Check (($st -replace "`r`n", '').Contains("`n") -eq $false) 'no bare LF'
    Refuses { Add-FastReleaseStatusRecord -Path $status -Lines $rec } 'already starts with this record'
    $rm = Get-FastReleaseRecordMessage -Info $info -Trailer 'T: y'
    Check ($rm.StartsWith("docs: record the 1.2.4 package in STATUS`n`n") -and $rm.TrimEnd().EndsWith("`n`nT: y")) 'record commit message'

    # --------------------------------------------------------------------------------------------------- summary
    $sum = New-FastReleaseSummary -Version '1.2.4' -DryRun $true -Publish $false
    $keys = 'schema,tool,version,previous_version,dry_run,publish,status,error,failed_step,started_utc,finished_utc,total_seconds,main_checkout,dev_branch,dev_before,release_worktree,release_branch,release_merge_commit,bump_commit,record_commit,release_head,dev_merge_commit,trees_match,receipt,package_dir,source_files,zips,exe,dll,build_ids,defender,privacy,publish_result,log_dir,steps'
    Check ((@($sum.Keys) -join ',') -ceq $keys) 'summary keys'
    Check ((@($sum.receipt.Keys) -join ',') -ceq 'id,path,status,audited_tests' -and (@($sum.defender.Keys) -join ',') -ceq 'result,engine,signatures,files' -and (@($sum.privacy.Keys) -join ',') -ceq 'tracked_hits,zip_hits') 'summary sub-keys'
    $fi = Get-FastReleaseFileInfo $prog
    Check ((@($fi.Keys) -join ',') -ceq 'name,path,bytes,sha256' -and $fi.bytes -eq (Get-Item $prog).Length -and $fi.sha256 -ceq (Get-FileHash $prog).Hash) 'file info'

    # ------------------------------------------------------------------------------------------------------ lock
    $lock = Join-Path $temp 'verify.lock'
    Enter-FastReleaseVerifyLock -LockPath $lock -Owner 'me 1' -TimeoutMinutes 1 -Log { param($t) }
    Check ([IO.File]::ReadAllText((Join-Path $lock 'owner.txt')) -ceq 'me 1') 'lock taken with its owner'
    Check (!(Exit-FastReleaseVerifyLock -LockPath $lock -Owner 'someone else')) 'another owner never removes it'
    Check (Test-Path -LiteralPath $lock) 'lock still there'
    Check (Exit-FastReleaseVerifyLock -LockPath $lock -Owner 'me 1') 'its owner removes it'
    Check (!(Test-Path -LiteralPath $lock)) 'lock gone'

    # -------------------------------------------------------------------------------- the script on fixture repos
    function New-FixtureRepo([string]$Name) {
        $repo = Join-Path $temp $Name
        $null = New-Item -ItemType Directory -Path $repo
        $null = FxGit $repo @('init', '--quiet', '-b', 'dev/oct7-continue')
        foreach ($kv in @(@('user.name', 'Fixture'), @('user.email', 'fixture@example.invalid'), @('commit.gpgsign', 'false'), @('core.autocrlf', 'false'))) { $null = FxGit $repo @('config', $kv[0], $kv[1]) }
        New-BumpFiles $repo '1.2.2' '1.2.3'
        Write-Text (Join-Path $repo '.gitignore') "/build/`n/work/`n/third_party/minhook/`n/dist/`n"
        Write-Text (Join-Path $repo 'docs/STATUS.md') "## older`n`ntext`n"
        Write-Text (Join-Path $repo 'notes.txt') "line 1`nline 2`n"
        $null = FxGit $repo @('add', '-A')
        $null = FxGit $repo @('commit', '--quiet', '-m', 'base')
        return $repo
    }
    $deps = Join-Path $temp 'deps'
    Write-Text (Join-Path $deps 'third_party/minhook/.git/HEAD') "ref`n"
    Write-Text (Join-Path $deps 'third_party/minhook/src/a.c') "x`n"
    Write-Text (Join-Path $deps 'work/lua-5.1.5/x') "x`n"
    Write-Text (Join-Path $deps 'work/upstream-dynamite/x') "x`n"
    Write-Text (Join-Path $deps 'build/game-input-20260909/x') "x`n"
    Write-Text (Join-Path $deps 'build/other/x') "not copied`n"
    $runN = 0
    function Invoke-Release([string]$Main, [string]$Wt, [string[]]$More) {
        $script:runN++
        $logs = Join-Path $temp "logs$script:runN"
        $argList = @('-NoProfile', '-File', $releaseScript, '-MainCheckout', $Main, '-WorktreePath', $Wt, '-DepsFrom', $deps, '-LogDirectory', $logs,
            '-PrivacyProfilePath', $fakeProfile, '-PrivacyName', $fakeName, '-PrivacyEmail', $fakeEmail, '-Trailer', 'Fixture-Trailer: yes', '-GitHubRepo', 'TrollZebulon/fixture') + $More
        $out = @(& pwsh @argList 2>&1 | ForEach-Object { [string]$_ })
        $code = $LASTEXITCODE
        $sumPath = Join-Path $logs 'summary.json'
        $s = if (Test-Path -LiteralPath $sumPath) { Get-Content -LiteralPath $sumPath -Raw | ConvertFrom-Json } else { $null }
        return [pscustomobject]@{ Exit = $code; Out = ($out -join "`n"); Summary = $s }
    }

    # Argument validation (the parameter block): nothing runs.
    $r = Invoke-Release (Join-Path $temp 'none') (Join-Path $temp 'none-wt') @('-Version', '1.2')
    Check ($r.Exit -ne 0 -and $r.Out -match 'Version' -and $null -eq $r.Summary) 'a bad -Version is refused before anything runs'
    $r = Invoke-Release (Join-Path $temp 'none') (Join-Path $temp 'none-wt') @('-Version', '1.2.4', '-StopAfter', 'gate')
    Check ($r.Exit -ne 0 -and $null -eq $r.Summary) '-StopAfter only takes the steps before the gate'

    $main = New-FixtureRepo 'main'
    $wt = Join-Path $temp 'release-wt'
    $base = FxGit $main @('rev-parse', 'HEAD')
    $r = Invoke-Release $main $wt @('-Version', '1.2.4', '-Publish', '-DryRun', '-NotesFile', (Join-Path $main 'notes.txt'))
    Check ($r.Exit -eq 1 -and $r.Summary.status -ceq 'failed' -and $r.Summary.failed_step -ceq 'preflight' -and $r.Summary.error -match 'exclude each other') '-Publish with -DryRun'
    $r = Invoke-Release $main $wt @('-Version', '1.2.4', '-Publish')
    Check ($r.Exit -eq 1 -and $r.Summary.error -match 'needs -NotesFile') '-Publish needs -NotesFile'
    $r = Invoke-Release $main $wt @('-Version', '1.2.4', '-Publish', '-NotesFile', (Join-Path $temp 'missing.md'))
    Check ($r.Exit -eq 1 -and $r.Summary.error -match 'does not exist') '-NotesFile must exist'
    $r = Invoke-Release $main $wt @('-Version', '1.2.3', '-DryRun')
    Check ($r.Exit -eq 1 -and $r.Summary.error -match 'not newer') 'the version must be newer than dev''s'
    Check (@($r.Summary.steps).Count -eq 1 -and $r.Summary.steps[0].name -ceq 'preflight' -and $r.Summary.steps[0].status -ceq 'failed' -and $null -ne $r.Summary.steps[0].seconds) 'a failed step is timed in the summary'
    Check (!(Test-Path -LiteralPath $wt)) 'refusals create no worktree'

    # The main checkout on another branch, then with a tracked change.
    $null = FxGit $main @('switch', '--quiet', '-c', 'other')
    $r = Invoke-Release $main $wt @('-Version', '1.2.4', '-DryRun')
    Check ($r.Exit -eq 1 -and $r.Summary.error -match 'not on dev/oct7-continue \(it is on other\)') 'wrong branch'
    $null = FxGit $main @('switch', '--quiet', 'dev/oct7-continue')
    Write-Text (Join-Path $main 'notes.txt') "changed`n"
    $r = Invoke-Release $main $wt @('-Version', '1.2.4', '-DryRun')
    Check ($r.Exit -eq 1 -and $r.Summary.error -match 'main checkout .* uncommitted changes') 'dirty main checkout'
    $null = FxGit $main @('checkout', '--quiet', '--', 'notes.txt')
    Write-Text (Join-Path $main 'untracked.txt') "x`n"

    # A dry run up to the bump: the worktree is created (deps copied), the merge and bump commits stay on no branch.
    $r = Invoke-Release $main $wt @('-Version', '1.2.4', '-DryRun', '-StopAfter', 'bump', '-Summary', 'fixture round')
    Check ($r.Exit -eq 0 -and $r.Summary.status -ceq 'stopped') "dry run stopped after bump (an untracked file in the main checkout is fine): $($r.Summary.error)"
    Check ((@($r.Summary.steps | ForEach-Object name) -join ',') -ceq 'preflight,worktree,merge,privacy,bump' -and @($r.Summary.steps | Where-Object { $_.status -cne 'ok' }).Count -eq 0) 'steps so far, all ok'
    Check ((Test-Path -LiteralPath (Join-Path $wt 'third_party/minhook/.git/HEAD')) -and (Test-Path -LiteralPath (Join-Path $wt 'build/game-input-20260909/x')) -and !(Test-Path -LiteralPath (Join-Path $wt 'build/other'))) 'deps copied (hidden .git too), only the *-2026* build folders'
    Check ((FxGit $wt @('symbolic-ref', '--short', 'HEAD')) -ceq 'wp/release' -and (FxGit $wt @('rev-parse', 'HEAD')) -ceq $base -and (FxGit $main @('rev-parse', 'dev/oct7-continue')) -ceq $base) 'after the dry run: back on wp/release, no branch moved'
    Check ((FxGit $wt @('status', '--porcelain')) -ceq '') 'worktree clean after the dry run'
    $bump = [string]$r.Summary.bump_commit
    Check ((FxGit $wt @('show', "${bump}:CMakeLists.txt")) -match 'VERSION 1\.2\.4 LANGUAGES' -and (FxGit $wt @('log', '-1', '--format=%B', $bump)) -match '(?s)^chore\(release\): bump version to 1\.2\.4\s+fixture round.*Fixture-Trailer: yes$') 'the dry-run bump commit (on no branch)'
    Check ((FxGit $wt @('branch', '--contains', $bump)) -ceq '') 'no branch contains the dry-run bump'
    Check ($r.Summary.previous_version -ceq '1.2.3' -and $r.Summary.dry_run -eq $true) 'summary previous version and dry_run'

    # dev moves on: a privacy hit (the made-up profile path) stops a dry run at the privacy step.
    Write-Text (Join-Path $main 'leak.md') "ok`nsee C:/Users/zedq/notes`n"
    $null = FxGit $main @('add', 'leak.md'); $null = FxGit $main @('commit', '--quiet', '-m', 'leak')
    $r = Invoke-Release $main $wt @('-Version', '1.2.4', '-DryRun')
    Check ($r.Exit -eq 1 -and $r.Summary.failed_step -ceq 'privacy' -and (@($r.Summary.privacy.tracked_hits) -join '|') -ceq 'leak.md:2: profile path') "privacy hit in a tracked file: $($r.Summary.error)"
    Check (!$r.Out.Contains('zedq')) 'the hit text is not printed'
    Check ((FxGit $wt @('symbolic-ref', '--short', 'HEAD')) -ceq 'wp/release' -and (FxGit $wt @('rev-parse', 'HEAD')) -ceq $base) 'a failed dry run goes back to wp/release'
    $null = FxGit $main @('rm', '--quiet', 'leak.md'); $null = FxGit $main @('commit', '--quiet', '-m', 'unleak')

    # A real run up to the bump: merge and bump commits on wp/release, dev untouched; a rerun reuses the bump.
    $devTip = FxGit $main @('rev-parse', 'HEAD')
    $r = Invoke-Release $main $wt @('-Version', '1.2.4', '-StopAfter', 'bump')
    Check ($r.Exit -eq 0 -and $r.Summary.status -ceq 'stopped') "real run up to the bump: $($r.Summary.error)"
    Check ((FxGit $wt @('log', '-1', '--format=%s', 'wp/release')) -ceq 'chore(release): bump version to 1.2.4' -and (FxGit $wt @('log', '-1', '--format=%s', 'wp/release~1')) -ceq 'Merge dev/oct7-continue into wp/release for 1.2.4') 'bump on top of the dev merge on wp/release'
    Check ((FxGit $wt @('rev-list', '--parents', '-n', '1', 'wp/release~1')).Split(' ').Count -eq 3) 'the dev merge is a merge commit (--no-ff)'
    Check ((FxGit $main @('rev-parse', 'dev/oct7-continue')) -ceq $devTip) 'dev untouched'
    $r2 = Invoke-Release $main $wt @('-Version', '1.2.4', '-StopAfter', 'bump')
    Check ($r2.Exit -eq 0 -and $r2.Summary.bump_commit -ceq $r.Summary.bump_commit -and (@($r2.Summary.steps | Where-Object name -ceq 'bump')[0].note) -match 'reused') 'a rerun reuses the bump commit'
    $r3 = Invoke-Release $main $wt @('-Version', '1.2.5', '-StopAfter', 'bump')
    Check ($r3.Exit -eq 1 -and $r3.Summary.failed_step -ceq 'bump' -and $r3.Summary.error -match 'unfinished release of 1\.2\.4 is on wp/release') "another version after an unfinished release is refused at the bump: $($r3.Summary.error)"

    # The release worktree with an untracked file, then a conflicting change on both branches.
    Write-Text (Join-Path $wt 'scratch.txt') "x`n"
    $r = Invoke-Release $main $wt @('-Version', '1.2.5', '-DryRun')
    Check ($r.Exit -eq 1 -and $r.Summary.error -match 'release worktree .* uncommitted changes') 'dirty release worktree'
    Remove-Item -LiteralPath (Join-Path $wt 'scratch.txt')
    Write-Text (Join-Path $wt 'notes.txt') "release side`n"
    $null = FxGit $wt @('commit', '--quiet', '-am', 'release side')
    Write-Text (Join-Path $main 'notes.txt') "dev side`n"
    $null = FxGit $main @('commit', '--quiet', '-am', 'dev side')
    $relTip = FxGit $wt @('rev-parse', 'wp/release')
    $r = Invoke-Release $main $wt @('-Version', '1.2.5', '-DryRun')
    Check ($r.Exit -eq 1 -and $r.Summary.failed_step -ceq 'merge' -and $r.Summary.error -match 'conflicts in: notes\.txt') "merge conflict named: $($r.Summary.error)"
    Check ((FxGit $wt @('status', '--porcelain')) -ceq '' -and (FxGit $wt @('symbolic-ref', '--short', 'HEAD')) -ceq 'wp/release' -and (FxGit $wt @('rev-parse', 'HEAD')) -ceq $relTip) 'conflict aborted, back on wp/release'

    # The worktree must be on wp/release.
    $null = FxGit $wt @('switch', '--quiet', '--detach')
    $r = Invoke-Release $main $wt @('-Version', '1.2.5', '-DryRun')
    Check ($r.Exit -eq 1 -and $r.Summary.error -match 'not on wp/release \(detached HEAD\)') 'detached release worktree'
    $null = FxGit $wt @('switch', '--quiet', 'wp/release')
    Write-Host "fast_release_tests: $checks checks passed"
} finally {
    # Only the folders this test created: the fixture worktree is unregistered with its repository.
    Remove-Item -LiteralPath $temp -Recurse -Force -ErrorAction SilentlyContinue
}
