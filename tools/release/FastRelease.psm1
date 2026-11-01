# The pieces of tools/release/Invoke-FastRelease.ps1 that need no build, game or network: the version bump (the same
# 8 files every release), the privacy grep (tracked files and zip contents), the Defender result, the parsers for the
# Release checks' and the package's output, the docs/STATUS.md package record, the summary and the exclusive verify
# lock. tests/fast_release_tests.ps1 tests them.
#
# Personal data never lives in this file: the privacy grep is given its identity (the Windows profile path, the git
# user name and e-mail) at run time (Get-FastReleaseIdentity reads it from the machine and git config).
Set-StrictMode -Version Latest

# ---------------------------------------------------------------------------------------------------------------- git
# Runs git, returns its output lines (stdout and stderr), throws on a non-zero exit unless -AllowFail.
function Invoke-FastReleaseGit {
    param([Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)][string[]]$Arguments, [switch]$AllowFail)
    $output = @(& git -C $Path @Arguments 2>&1 | ForEach-Object { [string]$_ })
    $code = $LASTEXITCODE
    if ($code -ne 0 -and !$AllowFail) {
        throw "git $($Arguments -join ' ') failed in $Path (exit $code): $(($output | Select-Object -Last 8) -join ' | ')"
    }
    return [pscustomobject]@{ Exit = $code; Lines = $output; Text = ($output -join "`n").Trim() }
}

# Refuses a checkout that is not on $Branch or has changes. -TrackedOnly ignores untracked files (the main checkout:
# only tracked changes can collide with a merge); the release worktree must be entirely clean (package.ps1 refuses an
# untracked source file anyway).
function Assert-FastReleaseCheckout {
    param([Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)][string]$Branch, [Parameter(Mandatory)][string]$What, [switch]$TrackedOnly)
    if (!(Test-Path -LiteralPath $Path -PathType Container)) { throw "$What $Path does not exist." }
    $top = Invoke-FastReleaseGit $Path @('rev-parse', '--show-toplevel') -AllowFail
    if ($top.Exit -ne 0) { throw "$What $Path is not a git checkout." }
    $gitDir = (Invoke-FastReleaseGit $Path @('rev-parse', '--path-format=absolute', '--git-dir')).Text
    foreach ($marker in @('MERGE_HEAD', 'REBASE_HEAD', 'CHERRY_PICK_HEAD', 'index.lock')) {
        if (Test-Path -LiteralPath (Join-Path $gitDir $marker)) { throw "$What $Path has $marker (an unfinished git operation); finish or abort it first." }
    }
    $head = Invoke-FastReleaseGit $Path @('symbolic-ref', '--quiet', '--short', 'HEAD') -AllowFail
    if ($head.Exit -ne 0) { throw "$What $Path is not on $Branch (detached HEAD); switch it to $Branch first." }
    if ($head.Text -cne $Branch) { throw "$What $Path is not on $Branch (it is on $($head.Text))." }
    $statusArgs = @('status', '--porcelain')
    if ($TrackedOnly) { $statusArgs += '--untracked-files=no' }
    $dirty = @((Invoke-FastReleaseGit $Path $statusArgs).Lines | Where-Object { $_ -match '\S' })
    if ($dirty.Count) { throw "$What $Path has uncommitted changes ($($dirty.Count): $(($dirty | Select-Object -First 5) -join '; ')). Commit or move them first." }
}

# ------------------------------------------------------------------------------------------------------------ version
function Test-FastReleaseVersion([string]$Version) { return ($Version -match '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$') }

function Get-FastReleaseNextPatch([Parameter(Mandatory)][string]$Version) {
    $v = [version]$Version
    return "$($v.Major).$($v.Minor).$($v.Build + 1)"
}

function Get-FastReleaseProjectVersion([Parameter(Mandatory)][string]$Root) {
    $text = [IO.File]::ReadAllText((Join-Path $Root 'CMakeLists.txt'))
    $m = [regex]::Match($text, 'project\(mgs_coop VERSION ([0-9]+\.[0-9]+\.[0-9]+) LANGUAGES CXX\)')
    if (!$m.Success) { throw 'CMakeLists.txt has no project(mgs_coop VERSION X.Y.Z LANGUAGES CXX) line.' }
    return $m.Groups[1].Value
}

# The files every version bump changes (6c25276a for 0.14.6, 521b82b1 for 0.14.7), in git's order.
function Get-FastReleaseBumpFiles {
    return @('CMakeLists.txt', 'README.md', 'tests/ddt_core_tests.cpp', 'tools/dev/Play-LocalCoop.ps1', 'tools/package-install.ps1',
        'tools/package.ps1', 'tools/probe.ps1', 'tools/set-session-mode.ps1')
}

# Literal replacements of the bump, each with the exact number of occurrences it must find. The allow-lists keep
# accepting the previous version.
function Get-FastReleaseBumpEdits {
    param([Parameter(Mandatory)][string]$Previous, [Parameter(Mandatory)][string]$Version)
    return @(
        [pscustomobject]@{ Path = 'CMakeLists.txt'; Old = "project(mgs_coop VERSION $Previous LANGUAGES CXX)"; New = "project(mgs_coop VERSION $Version LANGUAGES CXX)"; Count = 1 }
        [pscustomobject]@{ Path = 'README.md'; Old = "version $Previous (development build)"; New = "version $Version (development build)"; Count = 1 }
        [pscustomobject]@{ Path = 'tools/package.ps1'; Old = "`$version='$Previous'"; New = "`$version='$Version'"; Count = 1 }
        [pscustomobject]@{ Path = 'tools/probe.ps1'; Old = "version = '$Previous';"; New = "version = '$Version';"; Count = 1 }
        [pscustomobject]@{ Path = 'tools/set-session-mode.ps1'; Old = ",'$Previous'))"; New = ",'$Previous','$Version'))"; Count = 2 }
        [pscustomobject]@{ Path = 'tools/package-install.ps1'; Old = ",'$Previous')"; New = ",'$Previous','$Version')"; Count = 1 }
        [pscustomobject]@{ Path = 'tools/dev/Play-LocalCoop.ps1'; Old = ",'$Previous')"; New = ",'$Previous','$Version')"; Count = 1 }
    )
}

function Get-FastReleaseCount([string]$Text, [string]$Needle) {
    $count = 0; $at = 0
    while (($at = $Text.IndexOf($Needle, $at, [StringComparison]::Ordinal)) -ge 0) { $count++; $at += $Needle.Length }
    return $count
}

# Bumps $Previous -> $Version under $Root exactly as the release bumps did: every replacement must find its exact
# count, line endings stay as they were, and tests/ddt_core_tests.cpp gains IsVersionNewer("<next patch>", "$Version")
# after its IsVersionNewer(..., "$Previous") line. Nothing is written unless every file passes. Returns the changed paths.
function Invoke-FastReleaseBump {
    param([Parameter(Mandatory)][string]$Root, [Parameter(Mandatory)][string]$Previous, [Parameter(Mandatory)][string]$Version)
    if (!(Test-FastReleaseVersion $Previous) -or !(Test-FastReleaseVersion $Version)) { throw "Bad version: $Previous -> $Version" }
    if ([version]$Version -le [version]$Previous) { throw "The new version $Version is not newer than $Previous." }
    $utf8 = [Text.UTF8Encoding]::new($false)
    $out = [ordered]@{}
    foreach ($edit in @(Get-FastReleaseBumpEdits -Previous $Previous -Version $Version)) {
        $path = Join-Path $Root $edit.Path
        if (!$out.Contains($edit.Path)) {
            $bytes = [IO.File]::ReadAllBytes($path)
            $out[$edit.Path] = [pscustomobject]@{ Path = $path; Original = $bytes; Text = $utf8.GetString($bytes) }
        }
        $entry = $out[$edit.Path]
        $found = Get-FastReleaseCount $entry.Text $edit.Old
        if ($found -ne $edit.Count) { throw "Version bump: $($edit.Path) has '$($edit.Old)' $found times, expected $($edit.Count)." }
        $entry.Text = $entry.Text.Replace($edit.Old, $edit.New)
    }
    $testPath = 'tests/ddt_core_tests.cpp'
    $testBytes = [IO.File]::ReadAllBytes((Join-Path $Root $testPath))
    $testText = $utf8.GetString($testBytes)
    $anchor = [regex]::Matches($testText, '(?m)^(?<indent>[ \t]*)CHECK\(IsVersionNewer\("[^"]+", "' + [regex]::Escape($Previous) + '"\)\);(?<nl>\r?\n)')
    if ($anchor.Count -ne 1) { throw "Version bump: $testPath has $($anchor.Count) CHECK(IsVersionNewer(..., `"$Previous`")) lines, expected 1." }
    $a = $anchor[0]
    $line = $a.Groups['indent'].Value + 'CHECK(IsVersionNewer("' + (Get-FastReleaseNextPatch $Version) + '", "' + $Version + '"));' + $a.Groups['nl'].Value
    $out[$testPath] = [pscustomobject]@{ Path = (Join-Path $Root $testPath); Original = $testBytes; Text = $testText.Insert($a.Index + $a.Length, $line) }
    foreach ($key in @($out.Keys)) {
        $entry = $out[$key]
        $new = $utf8.GetBytes($entry.Text)
        $before = $utf8.GetString($entry.Original)
        $crlf = { param($t) ([regex]::Matches($t, "`r`n")).Count }
        $lf = { param($t) ([regex]::Matches($t, "`n")).Count }
        $added = if ($key -ceq $testPath) { 1 } else { 0 }
        if ((& $lf $entry.Text) -ne (& $lf $before) + $added -or
            ((& $crlf $before) -gt 0 -and (& $crlf $entry.Text) -ne (& $crlf $before) + $added) -or
            ((& $crlf $before) -eq 0 -and (& $crlf $entry.Text) -ne 0)) { throw "Version bump: $key line endings would change." }
        $entry | Add-Member -NotePropertyName New -NotePropertyValue $new
    }
    foreach ($key in @($out.Keys)) { [IO.File]::WriteAllBytes($out[$key].Path, $out[$key].New) }
    return @(Get-FastReleaseBumpFiles | Where-Object { $out.Contains($_) })
}

function Get-FastReleaseBumpMessage {
    param([Parameter(Mandatory)][string]$Previous, [Parameter(Mandatory)][string]$Version, [string]$Summary, [string]$Trailer)
    $lines = [Collections.Generic.List[string]]::new()
    $lines.Add("chore(release): bump version to $Version"); $lines.Add('')
    if ($Summary) { $lines.Add($Summary); $lines.Add('') }
    $lines.Add("- CMakeLists.txt, README.md, tools/package.ps1, tools/probe.ps1: $Version.")
    $lines.Add('- tools/set-session-mode.ps1, tools/package-install.ps1,')
    $lines.Add("  tools/dev/Play-LocalCoop.ps1: accept $Version ($Previous still accepted).")
    $lines.Add("- tests/ddt_core_tests.cpp: IsVersionNewer(`"$(Get-FastReleaseNextPatch $Version)`", `"$Version`").")
    $lines.Add(''); $lines.Add('Made by tools/release/Invoke-FastRelease.ps1; the Release gate runs once, after this commit.')
    if ($Trailer) { $lines.Add(''); $lines.Add($Trailer) }
    return ($lines -join "`n") + "`n"
}

# ------------------------------------------------------------------------------------------------------------ privacy
# The identity the privacy grep looks for. ProfilePath: a Windows profile folder (C:\Users\<name>); it is looked for
# without its drive in the \, / and \\ (JSON) spellings. Name: each word (3+ letters) of the git user name, anywhere,
# except inside an allowed context (the GitHub owner login, which contains it). Email: the address and its local part.
function New-FastReleasePrivacyIdentity {
    param([string]$ProfilePath, [string]$Name, [string]$Email, [string[]]$AllowedContexts = @(), [string[]]$Extra = @())
    $needles = [Collections.Generic.List[object]]::new()
    $add = {
        param([string]$Kind, [string]$Literal, [string]$Tail)
        if (!$Literal) { return }
        foreach ($n in $needles) { if ($n.Literal -ieq $Literal -and $n.Kind -ceq $Kind) { return } }
        $needles.Add([pscustomobject]@{ Kind = $Kind; Literal = $Literal; Regex = [regex]::new([regex]::Escape($Literal) + $Tail, 'IgnoreCase, CultureInvariant') })
    }
    if ($ProfilePath) {
        $p = ($ProfilePath -replace '^[A-Za-z]:', '').Trim('\', '/')
        $parts = @($p -split '[\\/]+' | Where-Object { $_ })
        if ($parts.Count -lt 2) { throw "The profile path '$ProfilePath' is too short to look for." }
        foreach ($sep in @('\', '/', '\\')) { & $add 'profile path' ($sep + ($parts -join $sep)) '(?![A-Za-z0-9_])' }
    }
    if ($Name) { foreach ($word in @($Name -split '[^\p{L}\p{N}]+' | Where-Object { $_.Length -ge 3 })) { & $add 'persona name' $word '' } }
    if ($Email) {
        & $add 'e-mail' $Email ''
        $local = ($Email -split '@')[0]
        if ($local.Length -ge 6) { & $add 'e-mail' $local '' }
    }
    foreach ($x in @($Extra | Where-Object { $_ })) { & $add 'extra' $x '' }
    if (!$needles.Count) { throw 'The privacy grep has nothing to look for (no profile path, name or e-mail).' }
    return [pscustomobject]@{ Needles = $needles.ToArray(); Allowed = @($AllowedContexts | Where-Object { $_ }) }
}

# From this machine and git: the profile folder, git user.name and user.email of $Repo; the GitHub owner login is an
# allowed context.
function Get-FastReleaseIdentity {
    param([Parameter(Mandatory)][string]$Repo, [string]$GitHubRepo, [string]$ProfilePath = $env:USERPROFILE, [string]$Name, [string]$Email)
    if (!$PSBoundParameters.ContainsKey('Name')) { $Name = (Invoke-FastReleaseGit $Repo @('config', 'user.name') -AllowFail).Text }
    if (!$PSBoundParameters.ContainsKey('Email')) { $Email = (Invoke-FastReleaseGit $Repo @('config', 'user.email') -AllowFail).Text }
    $allowed = @()
    if ($GitHubRepo -and $GitHubRepo.Contains('/')) { $allowed += ($GitHubRepo -split '/')[0] }
    return New-FastReleasePrivacyIdentity -ProfilePath $ProfilePath -Name $Name -Email $Email -AllowedContexts $allowed
}

# Hits in one text: [pscustomobject]@{ Source; Line; Kind }. The matched text is never reported.
function Find-FastReleasePrivacyHits {
    param([Parameter(Mandatory)]$Identity, [AllowEmptyString()][string]$Text, [string]$Source = '', [int]$LineOffset = 0, [int]$Max = 20)
    $hits = [Collections.Generic.List[object]]::new()
    if (!$Text) { return @() }
    $t = $Text
    foreach ($ctx in $Identity.Allowed) {
        $t = [regex]::Replace($t, [regex]::Escape($ctx), { param($m) ' ' * $m.Length }, 'IgnoreCase, CultureInvariant')
    }
    foreach ($needle in $Identity.Needles) {
        foreach ($m in $needle.Regex.Matches($t)) {
            $line = $LineOffset + 1 + ([regex]::Matches($t.Substring(0, $m.Index), "`n")).Count
            $hits.Add([pscustomobject]@{ Source = $Source; Line = $line; Kind = $needle.Kind })
            if ($hits.Count -ge $Max) { return $hits.ToArray() }
        }
    }
    return $hits.ToArray()
}

# The tracked files of $Repo's work tree (git grep: binary files skipped). git grep finds candidates for every needle
# literal; Find-FastReleasePrivacyHits then applies the boundaries and allowed contexts.
function Find-FastReleaseTrackedPrivacyHits {
    param([Parameter(Mandatory)][string]$Repo, [Parameter(Mandatory)]$Identity)
    $grepArgs = @('grep', '-I', '-i', '-n', '-F', '-z')
    foreach ($n in $Identity.Needles) { $grepArgs += @('-e', $n.Literal) }
    $r = Invoke-FastReleaseGit $Repo $grepArgs -AllowFail
    if ($r.Exit -gt 1) { throw "git grep failed in $Repo (exit $($r.Exit)): $($r.Text)" }
    $hits = [Collections.Generic.List[object]]::new()
    if ($r.Exit -eq 1) { return @() }
    foreach ($row in $r.Lines) {
        $f = $row -split "`0", 3
        if ($f.Count -lt 3) { continue }
        foreach ($h in @(Find-FastReleasePrivacyHits -Identity $Identity -Text $f[2] -Source $f[0] -LineOffset ([int]$f[1] - 1))) { $hits.Add($h) }
    }
    return $hits.ToArray()
}

# Every entry of a zip (its name, and its bytes read as UTF-8 and as UTF-16LE: binaries included, as earlier audits did;
# the user asked for the text files, this is a superset).
function Find-FastReleaseZipPrivacyHits {
    param([Parameter(Mandatory)][string]$ZipPath, [Parameter(Mandatory)]$Identity)
    Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
    $hits = [Collections.Generic.List[object]]::new()
    $zip = [IO.Compression.ZipFile]::OpenRead($ZipPath)
    try {
        foreach ($e in $zip.Entries) {
            if ($e.FullName.EndsWith('/')) { continue }
            foreach ($h in @(Find-FastReleasePrivacyHits -Identity $Identity -Text $e.FullName -Source "$($e.FullName) (entry name)")) { $h.Line = 0; $hits.Add($h) }
            $ms = [IO.MemoryStream]::new(); $s = $e.Open(); try { $s.CopyTo($ms) } finally { $s.Dispose() }
            $bytes = $ms.ToArray()
            $found = @(Find-FastReleasePrivacyHits -Identity $Identity -Text ([Text.Encoding]::UTF8.GetString($bytes)) -Source $e.FullName)
            # UTF-16 at both byte alignments (a string in a program need not start on an even offset).
            foreach ($offset in @(0, 1)) {
                if ($found.Count -or $bytes.Length -le $offset) { break }
                $found = @(Find-FastReleasePrivacyHits -Identity $Identity -Text ([Text.Encoding]::Unicode.GetString($bytes, $offset, $bytes.Length - $offset)) -Source "$($e.FullName) (UTF-16)" |
                    ForEach-Object { $_.Line = 0; $_ })
            }
            foreach ($h in $found) { $hits.Add($h) }
        }
    } finally { $zip.Dispose() }
    return $hits.ToArray()
}

function Format-FastReleasePrivacyHits([object[]]$Hits) {
    return @($Hits | ForEach-Object { if ($_.Line) { "$($_.Source):$($_.Line): $($_.Kind)" } else { "$($_.Source): $($_.Kind)" } } | Select-Object -Unique)
}

# ----------------------------------------------------------------------------------------------------- output parsers
function Get-FastReleaseReceiptPath([string[]]$Lines) {
    $found = @($Lines | ForEach-Object { if ($_ -match 'Worker check receipt: (.+receipt\.json)\s*$') { $Matches[1] } })
    if (!$found.Count) { return $null }
    return $found[-1]
}

# tools/package.ps1 -Friend output -> the two zips, the package folder and the source file count.
function Get-FastReleasePackageOutput([string[]]$Lines) {
    $player = $null; $source = $null; $files = $null
    foreach ($l in $Lines) {
        if ($l -match '^Prepared: (.+\.zip)\s*$') { $player = $Matches[1] }
        elseif ($l -match '^Prepared source: (.+\.zip)\s*$') { $source = $Matches[1] }
        elseif ($l -match '^Source SHA-256: [0-9A-Fa-f]{64}; files (\d+)\s*$') { $files = [int]$Matches[1] }
    }
    if (!$player -or !$source -or $null -eq $files) { throw 'tools/package.ps1 did not report both zips and the source file count.' }
    if ((Split-Path -Parent $player) -ne (Split-Path -Parent $source)) { throw 'tools/package.ps1 wrote the two zips into different folders.' }
    return [pscustomobject]@{ PackageDir = (Split-Path -Parent $player); PlayerZip = $player; SourceZip = $source; SourceFiles = $files }
}

# Build ids (X.Y.Z+<hex>) in a program, ASCII and UTF-16.
function Get-FastReleaseBuildIds([Parameter(Mandatory)][string]$Path) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    $re = '\b[0-9]+\.[0-9]+\.[0-9]+\+[0-9a-f]{7,40}\b'
    $ids = @([regex]::Matches([Text.Encoding]::ASCII.GetString($bytes), $re) | ForEach-Object Value) +
        @([regex]::Matches([Text.Encoding]::Unicode.GetString($bytes), $re) | ForEach-Object Value)
    return @($ids | Sort-Object -Unique)
}

function Test-FastReleaseBuildIds([string[]]$Ids, [string]$Version, [string]$Commit) {
    if (!@($Ids).Count) { return $false }
    foreach ($id in $Ids) {
        $v, $h = $id -split '\+', 2
        if ($v -cne $Version -or !$Commit.StartsWith($h, [StringComparison]::OrdinalIgnoreCase)) { return $false }
    }
    return $true
}

# MpCmdRun.exe -Scan -ScanType 3 -File <f> -DisableRemediation: clean = exit 0 and "found no threats", nothing else.
function Test-FastReleaseDefenderClean([int]$ExitCode, [string[]]$Lines) {
    $text = $Lines -join "`n"
    return ($ExitCode -eq 0 -and $text -match 'found no threats' -and $text -notmatch '(?i)found [1-9][0-9]* threats?|Threat information|Threat\s*:')
}

# ------------------------------------------------------------------------------------------------------------- record
function Format-FastReleaseBytes([int64]$Bytes) { return $Bytes.ToString('N0', [Globalization.CultureInfo]::InvariantCulture) }

# The docs/STATUS.md package record (the shape of fea0ff18 / 3975933d). $Info: Version, Previous, Date ([datetime]),
# Summary, PackageDirName, BumpCommit, PrivacyCommit, ReceiptId, AuditedTests, BuildJobs, TestJobs, SourceFiles,
# PlayerZip/SourceZip/Exe/Dll (each Bytes, Sha256), DefenderEngine, DefenderSignatures.
function Format-FastReleaseStatusRecord {
    param([Parameter(Mandatory)]$Info)
    $en = [Globalization.CultureInfo]::GetCultureInfo('en-US')
    $v = $Info.Version; $short = $Info.BumpCommit.Substring(0, 7); $dir = $Info.PackageDirName
    $heading = "## $($Info.Date.ToString('MMMM d, yyyy', $en)): $v package"
    if ($Info.Summary) { $heading += " ($($Info.Summary))" }
    $row = { param($label, $f) "| $label | $(Format-FastReleaseBytes $f.Bytes) | ``$($f.Sha256)`` |" }
    $lines = @(
        $heading
        ''
        "**Package ($($Info.Date.ToString('yyyy-MM-dd'))):** ``dist/$dir/``, built by ``tools/package.ps1 -Friend -CheckReceipt`` from the clean tree at the version bump ``$short`` with Release receipt ``$($Info.ReceiptId)`` (passed, $($Info.AuditedTests) audited tests, 0 failures; ``-BuildJobs $($Info.BuildJobs) -TestJobs $($Info.TestJobs)``). ``DiamondDogsTogether.exe`` and ``dinput8.dll`` both carry build id ``$v+$short`` and are the receipt's own files. Made by ``tools/release/Invoke-FastRelease.ps1``; this record is committed after the packaged commit."
        ''
        '| File | Bytes | SHA-256 |'
        '|---|---|---|'
        (& $row "``DiamondDogsTogether-$v.zip`` (the player zip: exe, dll, ``mgs-coop/package.json``, README, notices and licences; no source)" $Info.PlayerZip)
        (& $row "``DDT-source-$v.zip`` ($(Format-FastReleaseBytes $Info.SourceFiles) source files plus ``source-files.json``)" $Info.SourceZip)
        (& $row '`DiamondDogsTogether.exe`' $Info.Exe)
        (& $row '`dinput8.dll`' $Info.Dll)
        ''
        "- **Package checks:** the checks of ``tools/package.ps1 -Friend`` itself (a passing Release receipt with the Release flags; the staged exe and dll are the receipt's; each zip holds exactly its staged files with their hashes; only tracked source files). ``package.json``: version $v, ``source_commit`` ``$short``, ``source_dirty`` false. No separate zip audit."
        "- **Windows Defender** (``MpCmdRun.exe -Scan -ScanType 3 -File <path> -DisableRemediation``, one scan per file; engine $($Info.DefenderEngine), signatures $($Info.DefenderSignatures)): the exe, the dll and both zips ""found no threats"" (exit 0); the four hashes unchanged after the scans."
        "- **Privacy:** the tracked files at ``$($Info.PrivacyCommit.Substring(0, 7))`` and every file in both zips: no Windows profile path (both slash styles), persona name or the user's e-mail. History was not rewritten."
        "- **Version.** $v in ``CMakeLists.txt``, ``README.md``, ``tools/package.ps1`` and ``tools/probe.ps1``; ``tools/set-session-mode.ps1``, ``tools/package-install.ps1`` and ``tools/dev/Play-LocalCoop.ps1`` accept $v and still $($Info.Previous); ``tests/ddt_core_tests.cpp`` checks ``IsVersionNewer(""$(Get-FastReleaseNextPatch $v)"", ""$v"")``."
        "- **Checks at the bump commit ``$short``:** the Release receipt above is the only test run ($($Info.AuditedTests) audited tests); no other CTest run, hidden-game run, smoke run or review panel."
        "- **To release:** upload **both** zips to the GitHub release ``v$v``, then run ``pwsh -NoProfile -File tools/check-release.ps1 -Version $v -PackageDir dist/$dir`` before publishing. Nothing was pushed, tagged or released by the packaging."
        ''
    )
    return $lines
}

# Puts the record at the top of docs/STATUS.md with the file's own line endings.
function Add-FastReleaseStatusRecord {
    param([Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)][AllowEmptyString()][string[]]$Lines)
    $utf8 = [Text.UTF8Encoding]::new($false)
    $bytes = [IO.File]::ReadAllBytes($Path)
    $text = $utf8.GetString($bytes)
    $nl = if ($text.Contains("`r`n")) { "`r`n" } else { "`n" }
    $first = ($Lines | Select-Object -First 1)
    if ($text.StartsWith($first + $nl)) { throw "docs/STATUS.md already starts with this record: $first" }
    [IO.File]::WriteAllBytes($Path, $utf8.GetBytes(($Lines -join $nl) + $nl + $text))
}

function Get-FastReleaseRecordMessage {
    param([Parameter(Mandatory)]$Info, [string]$Trailer)
    $short = $Info.BumpCommit.Substring(0, 7)
    $lines = @(
        "docs: record the $($Info.Version) package in STATUS", '',
        "Package dist/$($Info.PackageDirName) from the clean tree at the version",
        "bump $short with Release receipt $($Info.ReceiptId)",
        "(passed, $($Info.AuditedTests) audited tests). Build id $($Info.Version)+$short in the exe and the dll.",
        'Windows Defender found no threats in the exe, the dll or either zip; no',
        'profile path, persona name or e-mail in the tracked files or either zip.',
        'Made by tools/release/Invoke-FastRelease.ps1.'
    )
    if ($Trailer) { $lines += @('', $Trailer) }
    return ($lines -join "`n") + "`n"
}

# ------------------------------------------------------------------------------------------------------------ summary
function New-FastReleaseSummary {
    param([Parameter(Mandatory)][string]$Version, [bool]$DryRun, [bool]$Publish)
    return [ordered]@{
        schema = 1; tool = 'tools/release/Invoke-FastRelease.ps1'; version = $Version; previous_version = $null
        dry_run = $DryRun; publish = $Publish; status = 'running'; error = $null; failed_step = $null
        started_utc = [DateTime]::UtcNow.ToString('o'); finished_utc = $null; total_seconds = 0
        main_checkout = $null; dev_branch = $null; dev_before = $null; release_worktree = $null; release_branch = $null
        release_merge_commit = $null; bump_commit = $null; record_commit = $null; release_head = $null; dev_merge_commit = $null; trees_match = $null
        receipt = [ordered]@{ id = $null; path = $null; status = $null; audited_tests = $null }
        package_dir = $null; source_files = $null
        zips = @(); exe = $null; dll = $null; build_ids = $null
        defender = [ordered]@{ result = $null; engine = $null; signatures = $null; files = @() }
        privacy = [ordered]@{ tracked_hits = @(); zip_hits = @() }
        publish_result = $null
        log_dir = $null
        steps = @()
    }
}

function Get-FastReleaseFileInfo([Parameter(Mandatory)][string]$Path) {
    $item = Get-Item -LiteralPath $Path
    return [ordered]@{ name = $item.Name; path = $item.FullName; bytes = [int64]$item.Length; sha256 = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash }
}

# --------------------------------------------------------------------------------------------------------------- lock
# Takes %TEMP%\mgs-coop-verify.lock exclusively (mkdir + owner.txt), waiting while anyone else holds it. Never deletes
# another owner's lock.
function Enter-FastReleaseVerifyLock {
    param([Parameter(Mandatory)][string]$LockPath, [Parameter(Mandatory)][string]$Owner, [int]$TimeoutMinutes = 90, [scriptblock]$Log = { param($t) Write-Host $t })
    $deadline = (Get-Date).AddMinutes($TimeoutMinutes)
    $reported = $null
    while ($true) {
        try {
            $null = New-Item -ItemType Directory -Path $LockPath -ErrorAction Stop
            [IO.File]::WriteAllText((Join-Path $LockPath 'owner.txt'), $Owner)
            return
        } catch {
            $held = ''; try { $held = [IO.File]::ReadAllText((Join-Path $LockPath 'owner.txt')) } catch {}
            if ($held -ne $reported) { & $Log "verify lock held by: $(if ($held) { $held } else { '(no owner.txt yet)' }); waiting"; $reported = $held }
            if ((Get-Date) -gt $deadline) { throw "The verify lock is still held after $TimeoutMinutes min: $held" }
            Start-Sleep -Seconds 15
        }
    }
}

# Removes the lock only when owner.txt is still exactly ours. Returns whether it did.
function Exit-FastReleaseVerifyLock {
    param([Parameter(Mandatory)][string]$LockPath, [Parameter(Mandatory)][string]$Owner)
    $mine = $false
    try { $mine = ([IO.File]::ReadAllText((Join-Path $LockPath 'owner.txt')) -ceq $Owner) } catch {}
    if ($mine) { Remove-Item -LiteralPath $LockPath -Recurse -Force }
    return $mine
}

Export-ModuleMember -Function Invoke-FastReleaseGit, Assert-FastReleaseCheckout, Test-FastReleaseVersion,
    Get-FastReleaseNextPatch, Get-FastReleaseProjectVersion, Get-FastReleaseBumpFiles, Get-FastReleaseBumpEdits, Invoke-FastReleaseBump,
    Get-FastReleaseBumpMessage, New-FastReleasePrivacyIdentity, Get-FastReleaseIdentity, Find-FastReleasePrivacyHits,
    Find-FastReleaseTrackedPrivacyHits, Find-FastReleaseZipPrivacyHits, Format-FastReleasePrivacyHits, Get-FastReleaseReceiptPath,
    Get-FastReleasePackageOutput, Get-FastReleaseBuildIds, Test-FastReleaseBuildIds, Test-FastReleaseDefenderClean,
    Format-FastReleaseBytes, Format-FastReleaseStatusRecord, Add-FastReleaseStatusRecord, Get-FastReleaseRecordMessage,
    New-FastReleaseSummary, Get-FastReleaseFileInfo, Enter-FastReleaseVerifyLock, Exit-FastReleaseVerifyLock
