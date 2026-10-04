#Requires -Version 7
# Tests for the two-PC friend-test package (tools/friend, tools/package.ps1 -Friend).
# Everything runs inside an owned temporary folder under build/: fixture game
# folders hold inert stand-in files, a fixture Steam folder holds fixture saves.
# No real game folder, Steam folder, network or game process is touched.
# The friend scripts run as child processes under Windows PowerShell 5.1 (what
# the .bat files start on a friend's PC) and, for one flow, PowerShell 7.
[CmdletBinding()]
param([string]$IniCheck, [string]$FakeProcess)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
Import-Module (Join-Path $projectRoot 'tools/lab/LabCommon.psm1') -Force -DisableNameChecking
Import-Module (Join-Path $projectRoot 'tools/friend/FriendCommon.psm1') -Force -DisableNameChecking
$buildRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'build'))
$nonce = [Guid]::NewGuid().ToString('N')
$fixture = Join-Path $buildRoot ('friend-package-tests-' + $nonce)
$ownerMarker = Join-Path $fixture '.fixture-owner'
$script:checks = 0
$pwshPath = (Get-Process -Id $PID).Path
$winPs = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
if (!(Test-Path -LiteralPath $winPs -PathType Leaf)) {
    Write-Warning 'Windows PowerShell 5.1 not found: the friend scripts are tested under PowerShell 7 only.'
    $winPs = $pwshPath
}

function Assert-True([bool]$Condition, [string]$Message) {
    $script:checks++
    if (!$Condition) { throw "Assertion failed: $Message" }
}
function Assert-Equal($Actual, $Expected, [string]$Message) {
    $script:checks++
    if ($Actual -cne $Expected) { throw "Assertion failed: $Message (expected '$Expected', got '$Actual')" }
}
function Assert-Throws([scriptblock]$Operation, [string]$Expected, [string]$Message) {
    $script:checks++
    $failure = $null
    try { & $Operation | Out-Null } catch { $failure = $_ }
    if (!$failure) { throw "Assertion failed: expected failure: $Message" }
    if ($Expected -and !$failure.Exception.Message.Contains($Expected)) { throw "Assertion failed: $Message (unexpected error: $($failure.Exception.Message))" }
}
function Assert-Refused($Result, [string]$Needle, [string]$Message) {
    $script:checks++
    if ($Result.Exit -eq 0 -or !(Has $Result $Needle)) { throw "Assertion failed: $Message (exit $($Result.Exit); output: $($Result.Text))" }
}
function Assert-InFixture([string]$Path) {
    if (!(Test-PathInside -Child $Path -Parent $fixture)) { throw "Test path escaped its fixture: $Path" }
}
function Invoke-IniCheck([string]$Mode, [string]$Value) {
    $output = & $IniCheck $Mode $Value
    if ($LASTEXITCODE -ne 0) { throw "lab_ini_check $Mode failed ($LASTEXITCODE)" }
    $map = @{}
    foreach ($line in $output) { $i = $line.IndexOf('='); if ($i -gt 0) { $map[$line.Substring(0, $i)] = $line.Substring($i + 1) } }
    return $map
}
function Invoke-Friend([string]$Script, [string[]]$Arguments, [string]$HostExe = $winPs) {
    # Child process, like the .bat files; output flattened for message checks.
    foreach ($a in $Arguments) { if ($a -and [IO.Path]::IsPathRooted($a) -and $a -notmatch '^[0-9]') { Assert-InFixture $a } }
    $raw = & $HostExe -NoProfile -ExecutionPolicy Bypass -NonInteractive -File $Script @Arguments 2>&1 | Out-String
    $code = $LASTEXITCODE
    # Windows PowerShell 5.1 wraps error records at the console width, even
    # inside words: messages are compared with all whitespace removed.
    return [pscustomobject]@{ Exit = $code; Raw = $raw; Text = ($raw -replace '\s', '') }
}
function Invoke-FriendAnswers([string]$Script, [string[]]$Arguments, [string[]]$Answers) {
    # The prompt path: answers arrive on stdin, one per Read-Host
    # (MGS_COOP_TEST_INTERACTIVE makes the scripts treat redirected input as a console).
    foreach ($a in $Arguments) { if ($a -and [IO.Path]::IsPathRooted($a) -and $a -notmatch '^[0-9]') { Assert-InFixture $a } }
    $env:MGS_COOP_TEST_INTERACTIVE = '1'
    try { $raw = $Answers | & $winPs -NoProfile -ExecutionPolicy Bypass -File $Script @Arguments 2>&1 | Out-String }
    finally { Remove-Item Env:MGS_COOP_TEST_INTERACTIVE -ErrorAction SilentlyContinue }
    $code = $LASTEXITCODE
    # Windows PowerShell 5.1 wraps error records at the console width, even
    # inside words: messages are compared with all whitespace removed.
    return [pscustomobject]@{ Exit = $code; Raw = $raw; Text = ($raw -replace '\s', '') }
}
function Has($Result, [string]$Needle) { return $Result.Text.Contains(($Needle -replace '\s', '')) }
function Get-TreeHashes([string]$Root) {
    $rows = [ordered]@{}
    foreach ($f in @(Get-ChildItem -LiteralPath $Root -File -Recurse -Force | Sort-Object FullName)) { $rows[$f.FullName] = Get-Sha256 $f.FullName }
    return ($rows.Keys | ForEach-Object { $_ + '=' + $rows[$_] }) -join "`n"
}
function New-GameFixture([string]$Name) {
    $game = Join-Path $fixture "games/$Name"
    Assert-InFixture $game
    $null = New-Item -ItemType Directory -Path $game -Force
    [IO.File]::WriteAllText((Join-Path $game 'mgsvtpp.exe'), 'INERT executable fixture; never executed')
    [IO.File]::WriteAllText((Join-Path $game 'steam_api64.dll'), 'INERT Steam DLL fixture')
    return [IO.Path]::GetFullPath($game)
}
function Remove-OwnedFixture {
    if (!(Test-Path -LiteralPath $fixture)) { return }
    if ((Split-Path -Parent $fixture) -ne $buildRoot -or !(Test-Path -LiteralPath $ownerMarker -PathType Leaf) -or
        [IO.File]::ReadAllText($ownerMarker) -ne $nonce) { throw 'Refusing cleanup: fixture ownership check failed' }
    foreach ($item in @(Get-ChildItem -LiteralPath $fixture -Force -Recurse)) {
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Refusing cleanup of a fixture containing links' }
    }
    Remove-Item -LiteralPath $fixture -Recurse -Force
}

if (!(Test-Path -LiteralPath $buildRoot)) { $null = New-Item -ItemType Directory -Path $buildRoot }
$null = New-Item -ItemType Directory -Path $fixture
[IO.File]::WriteAllText($ownerMarker, $nonce)
$fakeGame = $null
try {
    # ------------------------------------------------------------ SteamID validation
    $good = '76561198012345678'
    Assert-True ($null -eq (Get-FriendSteamIdProblem $good)) 'valid individual SteamID64'
    Assert-True ($null -eq (Get-FriendSteamIdProblem " $good ")) 'surrounding blanks are trimmed'
    Assert-True ((Get-FriendSteamIdProblem '76561198000000001').Contains('example')) 'the placeholder is refused'
    Assert-Equal (Get-FriendPlaceholderSteamId) '76561198000000001' 'placeholder value'
    Assert-True ((Get-FriendSteamIdProblem '7656119801234567').Contains('17 digits')) '16 digits refused'
    Assert-True ((Get-FriendSteamIdProblem '765611980123456789').Contains('17 digits')) '18 digits refused'
    Assert-True ((Get-FriendSteamIdProblem '7656119801234567x').Contains('digits only')) 'letters refused'
    Assert-True ((Get-FriendSteamIdProblem 'https://steamcommunity.com/id/x').Contains('digits only')) 'profile link refused'
    Assert-True ((Get-FriendSteamIdProblem '').Contains('No SteamID64')) 'empty refused'
    Assert-True ((Get-FriendSteamIdProblem $null).Contains('No SteamID64')) 'null refused'
    Assert-True ((Get-FriendSteamIdProblem '76561202255233008').Contains('7656119')) 'other prefix (lab fake account) refused'
    Assert-True ((Get-FriendSteamIdProblem '76561197960265728').Contains('not an individual')) 'account 0 refused'
    Assert-True ((Get-FriendSteamIdProblem '12345678901234567').Contains('7656119')) 'random 17 digits refused'
    Assert-True ((Get-FriendSteamIdProblem $good -OwnSteamId $good).Contains('your own')) 'own SteamID refused'
    Assert-True ($null -eq (Get-FriendSteamIdProblem $good -OwnSteamId '76561197960265735')) 'other own id accepted'
    Assert-Equal (ConvertTo-FriendSteamId64 -AccountId 7) '76561197960265735' 'ActiveUser arithmetic'
    Assert-Equal (ConvertTo-FriendSteamId64 -AccountId 52079950) $good 'account -> SteamID64'
    Assert-Throws { ConvertTo-FriendSteamId64 -AccountId 0 } 'Invalid Steam account' 'account 0'
    foreach ($id in '76561198012345678', '76561197960265729', '76561197960265728', '76561202255233008', '7656119801234567') {
        Assert-Equal (Test-FriendIndividualSteamId64 $id) (Test-IndividualSteamId64 $id) "individual check matches LabCommon for $id"
    }

    # ------------------------------------------------------------ lab.ini / coop.ini text
    $saveDir = 'C:\Games\MGS_TPP\mgs-coop\coop-saves'
    foreach ($role in 'Host', 'Join') {
        $lab = New-FriendLabIniText -Role $role -SaveDir $saveDir
        $coop = New-FriendCoopIniText -Character (Get-FriendDefaultCharacter -Role $role)
        $l = ConvertFrom-IniText $lab
        $c = ConvertFrom-IniText $coop
        $labRole = if ($role -eq 'Host') { 'host' } else { 'guest' }
        Assert-Equal (($l.Keys | Sort-Object) -join ',') 'enabled,join_trigger,loopback,role,save_dir,self_steamid' "$role lab.ini keys"
        Assert-Equal "$($l['enabled'])|$($l['role'])|$($l['loopback'])|$($l['self_steamid'])|$($l['join_trigger'])" "1|$labRole|0|real|0" "$role lab.ini diagnostic values"
        Assert-Equal $l['save_dir'] $saveDir "$role lab.ini save_dir"
        Assert-True ($lab.EndsWith("`n") -and !$lab.Contains("`r") -and (Test-FriendGeneratedText $lab)) "$role lab.ini LF + header"
        Assert-True (!(Test-IniHasKey $lab 'master_url') -and !(Test-IniHasKey $coop 'master_url')) "${role}: no master_url key"
        Assert-True ($lab -notmatch '(?m)^\s*master_url' -and $coop -notmatch '(?m)^\s*master_url') "${role}: no master_url line at all"
        Assert-Equal (($c.Keys | Sort-Object) -join ',') 'character,character_fallback' "$role coop.ini keys"
        Assert-Equal $c['character'] (Get-FriendDefaultCharacter -Role $role) "$role default character"
        Assert-Equal $c['character_fallback'] 'dd_male' "$role fallback"
        if ($IniCheck) {
            $file = Join-Path $fixture "$role-lab.ini"; [IO.File]::WriteAllText($file, $lab)
            $n = Invoke-IniCheck lab $file
            Assert-Equal "$($n['present'])|$($n['role'])|$($n['loopback'])|$($n['self_steamid'])|$($n['peer_steamid'])|$($n['join_trigger'])|$($n['xinput'])" "1|$labRole|0|0|0|0|-2" "native: $role lab.ini (real identity, no loopback, passthrough input)"
            Assert-Equal $n['save_dir'] $saveDir "native: $role save_dir"
            Assert-Equal "$($n['mutex_suffix'])|$($n['auto_join_delay_s'])|$($n['unknown_keys'])/$($n['invalid_values'])" '|0|0/0' "native: $role lab.ini clean"
            $file = Join-Path $fixture "$role-coop.ini"; [IO.File]::WriteAllText($file, $coop)
            $n = Invoke-IniCheck coop $file
            Assert-Equal "$($n['present'])|$($n['master_url'])|$($n['character'])|$($n['character_fallback'])|$($n['unknown_keys'])/$($n['invalid_values'])" "1||$((Get-FriendDefaultCharacter -Role $role))|dd_male|0/0" "native: $role coop.ini has no master URL"
        }
    }
    if (!$IniCheck) { Write-Warning 'lab_ini_check not supplied; native ini cross-checks skipped.' }
    Assert-Equal (Get-FriendDefaultCharacter -Role Host) 'default' 'host default character'
    Assert-Equal (Get-FriendDefaultCharacter -Role Join) 'quiet' 'joiner default character'
    foreach ($ch in 'default', 'quiet', 'dd_male', 'dd_female', 'snake', 'avatar', 'ocelot') { Assert-True (Test-FriendCharacter $ch) "character $ch" }
    Assert-True (-not (Test-FriendCharacter 'dog')) 'unknown character'
    Assert-True (-not (Test-FriendCharacter 'Quiet')) 'character names are lower case'
    Assert-Throws { New-FriendCoopIniText -Character dog } 'Invalid character' 'bad character text refused'
    Assert-Throws { New-FriendCoopIniText -Character quiet -CharacterFallback ocelot } 'character_fallback' 'Ocelot is no fallback'
    Assert-Throws { New-FriendLabIniText -Role Host -SaveDir 'relative\saves' } 'absolute' 'relative save_dir refused'
    Assert-Throws { New-FriendLabIniText -Role Host -SaveDir ('C:\' + ('a' * 220)) } 'too long' 'save_dir over 213 bytes refused'
    Assert-True ($null -eq (Get-FriendSaveDirProblem ('C:\' + ('a' * 210)))) 'save_dir of 213 bytes accepted'
    Assert-True ($null -ne (Get-FriendSaveDirProblem ("C:\x`tz"))) 'control characters refused'
    Assert-Equal (ConvertTo-FriendRoleName "host-v3`n") 'host' 'marker host'
    Assert-Equal (ConvertTo-FriendRoleName "client-v1`n") 'join' 'marker join'
    Assert-Equal (ConvertTo-FriendRoleName "client-check-v1`n") 'diagnostic' 'marker diagnostic'
    Assert-Equal (ConvertTo-FriendRoleName '') 'none' 'no marker'
    Assert-Equal (ConvertTo-FriendRoleName 'heap-v1') 'other' 'other marker'

    # ------------------------------------------------------------ redaction and log excerpts
    $sample = "client_host=76561198012345678`nid:76561197960265735;`nC:\Users\Alice Smith\Desktop\x.txt`nD:\Steam\userdata\52079950\287700\local`n12345678901234567 765611980123456789 98765"
    $hidden = Hide-FriendSensitiveText $sample
    Assert-True ($hidden.Contains('client_host=7656119XXXXXX5678') -and $hidden.Contains('id:7656119XXXXXX5735;')) 'SteamIDs keep only the last 4 digits'
    Assert-True ($hidden -notmatch '(?<![0-9])7656119[0-9]{10}(?![0-9])') 'no complete SteamID left'
    Assert-True ($hidden.Contains('C:\Users\<user>\Desktop') -and !$hidden.Contains('Alice')) 'Windows user name removed'
    Assert-True ($hidden.Contains('userdata\<account>\287700') -and !$hidden.Contains('52079950')) 'Steam account folder removed'
    Assert-True ($hidden.Contains('12345678901234567') -and $hidden.Contains('765611980123456789')) 'other numbers unchanged'
    Assert-Equal (Hide-FriendSensitiveText '') '' 'empty text'
    $log = "old line`nMGS Co-op script adapter 0.10.33; a`nfirst session`n`nMGS Co-op script adapter 0.11.0; b`nsecond session`n"
    Assert-Equal (Get-TextFromLastMarker -Text $log -Marker 'MGS Co-op script adapter') "MGS Co-op script adapter 0.11.0; b`nsecond session`n" 'from the last adapter start'
    Assert-Equal (Get-TextFromLastMarker -Text "x`ny`n" -Marker 'MGS Co-op script adapter') "x`ny`n" 'no marker: whole text'
    $tail = Limit-TextTail -Text (("line`n") * 100) -MaxChars 50
    Assert-True ($tail.StartsWith('[... ') -and $tail.Length -lt 100) 'tail limited with a note'
    $big = Join-Path $fixture 'big.log'
    [IO.File]::WriteAllText($big, ((1..5000 | ForEach-Object { "line $_" }) -join "`n") + "`n")
    $t = Read-FriendFileTail -Path $big -MaxBytes 1000
    Assert-True ($t.StartsWith('[... ') -and $t.Contains('line 5000') -and !$t.Contains('line 100' + "`n") -and $t.Length -lt 1100) 'file tail bounded, starts at a line'
    Assert-Equal (Read-FriendFileTail -Path (Join-Path $fixture 'absent.log')) '' 'absent file'

    # ------------------------------------------------------------ Steam library discovery
    $vdf = @'
"libraryfolders"
{
	"0"
	{
		"path"		"C:\\Program Files (x86)\\Steam"
		"apps"
		{
			"287700"		"25000000000"
		}
	}
	"1"
	{
		"path"		"E:\\SteamLibrary"
	}
}
'@
    Assert-Equal ((Get-SteamLibraryPathsFromVdf $vdf) -join '|') 'C:\Program Files (x86)\Steam|E:\SteamLibrary' 'libraryfolders.vdf (current format)'
    Assert-Equal ((Get-SteamLibraryPathsFromVdf "`"LibraryFolders`"`n{`n`t`"TimeNextStatsReport`"`t`t`"123`"`n`t`"1`"`t`t`"D:\\Games`"`n}`n") -join '|') 'D:\Games' 'libraryfolders.vdf (old format)'
    Assert-Equal @(Get-SteamLibraryPathsFromVdf '').Count 0 'empty vdf'
    Assert-Equal (Get-MgsvInstallDir "`"AppState`"`n{`n`t`"installdir`"`t`t`"MGS_TPP_custom`"`n}") 'MGS_TPP_custom' 'appmanifest installdir'
    Assert-Equal (Get-MgsvInstallDir '') 'MGS_TPP' 'default installdir'
    $steamFx = Join-Path $fixture 'discovery/steam'
    $libFx = Join-Path $fixture 'discovery/lib2'
    $null = New-Item -ItemType Directory -Path (Join-Path $steamFx 'steamapps') -Force
    $null = New-Item -ItemType Directory -Path (Join-Path $libFx 'steamapps/common/MGS_ALT') -Force
    [IO.File]::WriteAllText((Join-Path $libFx 'steamapps/common/MGS_ALT/mgsvtpp.exe'), 'INERT')
    [IO.File]::WriteAllText((Join-Path $libFx 'steamapps/appmanifest_287700.acf'), "`"AppState`"`n{`n`t`"installdir`"`t`t`"MGS_ALT`"`n}`n")
    [IO.File]::WriteAllText((Join-Path $steamFx 'steamapps/libraryfolders.vdf'), "`"libraryfolders`"`n{`n`t`"0`"`n`t{`n`t`t`"path`"`t`t`"$($steamFx.Replace('\', '\\'))`"`n`t}`n`t`"1`"`n`t{`n`t`t`"path`"`t`t`"$($libFx.Replace('\', '\\'))`"`n`t}`n}`n")
    $found = @(Find-MgsvGameFolders -SteamRoots @($steamFx) -Candidates @((Join-Path $fixture 'discovery/none'), (Join-Path $libFx 'steamapps/common/MGS_ALT')))
    Assert-Equal ($found -join '|') ([IO.Path]::GetFullPath((Join-Path $libFx 'steamapps/common/MGS_ALT'))) 'game folder found once through the library list'

    # ------------------------------------------------------------ package contents
    $fakeLoader = Join-Path $fixture 'fake-loader.dll'
    [IO.File]::WriteAllText($fakeLoader, 'INERT loader stand-in for the package test')
    $distFx = Join-Path $fixture 'dist'
    $null = & (Join-Path $projectRoot 'tools/package.ps1') -Friend -LoaderPath $fakeLoader -OutputRoot $distFx
    $packages = @(Get-ChildItem -LiteralPath $distFx -Directory)
    Assert-Equal $packages.Count 1 'one friend package staged'
    $pkg = $packages[0].FullName
    Assert-True ($packages[0].Name -cmatch '^mgs-coop-friend-0\.11\.0-[0-9]{8}T[0-9]{6}Z-[0-9a-f]{8}$') 'package folder name'
    Assert-True (Test-Path -LiteralPath ($pkg + '.zip') -PathType Leaf) 'package zip written'
    $required = @('Install-Coop.bat', 'Uninstall-Coop.bat', 'Collect-Logs.bat', 'FRIEND-GUIDE.txt', 'RUNTIME-ARCHIVE.txt', 'package.json', 'source-files.json',
        'payload/dinput8.dll', 'payload/DYNAMITE-LICENSE', 'payload/MinHook-LICENSE.txt', 'payload/tpp-mod-LICENSE', 'payload/THIRD_PARTY_NOTICES.md',
        'scripts/Install-Coop.ps1', 'scripts/Uninstall-Coop.ps1', 'scripts/Collect-Logs.ps1', 'scripts/FriendCommon.psm1', 'scripts/LabCommon.psm1',
        'scripts/install.ps1', 'scripts/set-session-mode.ps1', 'source/tools/friend/Install-Coop.ps1', 'source/tools/lab/LabCommon.psm1', 'source/lua/coop/session_feedback.lua')
    foreach ($r in $required) { Assert-True (Test-Path -LiteralPath (Join-Path $pkg $r) -PathType Leaf) "package contains $r" }
    $manifest = Get-Content -LiteralPath (Join-Path $pkg 'package.json') -Raw | ConvertFrom-Json
    Assert-Equal "$($manifest.product)|$($manifest.version)|$($manifest.flavor)" 'mgs-coop-local-package|0.11.0|friend-test' 'package manifest identity'
    Assert-Equal @($manifest.files).Count 5 'five payload files (loader + licenses)'
    Assert-Equal ($manifest.files | Where-Object target -eq 'dinput8.dll').sha256 (Get-Sha256 $fakeLoader) 'payload loader hash'
    Assert-Equal $manifest.runtime_archive.sha256 '26CE7C2168D655C35BE39E4F17A295206AA0522DE8782CD57A36C4FEEA382F54' 'runtime archive referenced by hash'
    Assert-True (-not (@($manifest.files.target) -contains 'mgs-coop/mgs-coop-player-runtime.dat')) 'runtime archive not bundled'
    $all = @(Get-ChildItem -LiteralPath $pkg -File -Recurse -Force)
    $gameExtensions = @('.dat', '.fpk', '.fpkd', '.fox2', '.mtar', '.qar', '.ftex', '.fmdl', '.subp', '.lng', '.lng2', '.sbp', '.gani', '.frig', '.fv2', '.fsm', '.fpkl')
    Assert-Equal @($all | Where-Object { $gameExtensions -contains $_.Extension.ToLowerInvariant() }).Count 0 'no game archive or asset file types'
    Assert-Equal @($all | Where-Object { $_.Name -like 'mgs-coop-player-runtime*' }).Count 0 'no runtime archive by name'
    Assert-Equal @($all | Where-Object { $_.Length -eq 3720192 }).Count 0 'no file of the runtime archive size'
    $sourceList = Get-Content -LiteralPath (Join-Path $pkg 'source-files.json') -Raw | ConvertFrom-Json
    Assert-Equal @($sourceList | Where-Object { $_.path -match '^(build|work|dist|backups)[\\/]' -or $_.path -match '(?i)package-[0-9]{2}\.(lua|fpkd?)$' }).Count 0 'no build/work output or extracted game scripts in source'
    $zipEntries = @()
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [IO.Compression.ZipFile]::OpenRead($pkg + '.zip')
    try { $zipEntries = @($zip.Entries | ForEach-Object { $_.FullName.Replace('\', '/') }) } finally { $zip.Dispose() }
    $prefix = $packages[0].Name + '/'
    foreach ($r in $required) { Assert-True ($zipEntries -contains ($prefix + $r)) "zip contains $r" }
    Assert-Equal @($zipEntries | Where-Object { $gameExtensions -contains [IO.Path]::GetExtension($_).ToLowerInvariant() }).Count 0 'zip has no game file types'
    foreach ($f in @(Get-ChildItem -LiteralPath (Join-Path $pkg 'scripts') -File) + @(Get-Item -LiteralPath (Join-Path $pkg 'FRIEND-GUIDE.txt'), (Join-Path $pkg 'RUNTIME-ARCHIVE.txt'))) {
        $bytes = [IO.File]::ReadAllBytes($f.FullName)
        Assert-Equal @($bytes | Where-Object { $_ -gt 127 }).Count 0 "$($f.Name) is ASCII (Windows PowerShell 5.1 / Notepad)"
        $text = [Text.Encoding]::ASCII.GetString($bytes)
        Assert-True (!$text.Contains('@VERSION@') -and !$text.Contains('@ARCHIVE_SHA256@')) "$($f.Name) placeholders replaced"
    }
    foreach ($b in 'Install-Coop', 'Uninstall-Coop', 'Collect-Logs') {
        $text = [IO.File]::ReadAllText((Join-Path $pkg "$b.bat"))
        Assert-True ($text.Contains("powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"%~dp0scripts\$b.ps1`" %*") -and $text.Contains("`r`n") -and $text.Contains('pause') -and $text.Contains('set "PSModulePath="')) "$b.bat runs its script with Windows PowerShell (own module path)"
    }
    $guide = [IO.File]::ReadAllText((Join-Path $pkg 'FRIEND-GUIDE.txt'))
    foreach ($needle in 'Ctrl+F8', 'Ctrl+F9', 'Install-Coop.bat', 'Uninstall-Coop.bat', 'Collect-Logs.bat', 'Account details', '7656119', 'RUNTIME-ARCHIVE.txt', 'Version 0.11.0') {
        Assert-True $guide.Contains($needle) "guide mentions $needle"
    }
    # Every in-game message the guide quotes exists verbatim in the Lua sources.
    $lua = [IO.File]::ReadAllText((Join-Path $projectRoot 'lua/coop/client_join.lua')) + [IO.File]::ReadAllText((Join-Path $projectRoot 'lua/coop/session_feedback.lua'))
    $quoted = @([regex]::Matches($guide, '(?m)^\s{4,}(Co-op: [^\r\n]+?)\s*$') | ForEach-Object { $_.Groups[1].Value })
    $quoted += @([regex]::Matches($guide, '(?m)^\s+(?:JOINER|HOST):\s+(Co-op: [^\r\n]+?)\s*$') | ForEach-Object { $_.Groups[1].Value })
    Assert-True ($quoted.Count -ge 5) 'guide quotes the in-game messages'
    foreach ($q in $quoted) { Assert-True $lua.Contains('"' + $q + '"') "guide message exists in Lua: $q" }
    foreach ($partial in 'Co-op: join could not start. Press Ctrl+F9, then Ctrl+F8 to', 'Co-op: join reset.', 'Co-op: still connecting', 'Co-op: session error') {
        Assert-True ($guide.Contains($partial) -and $lua.Contains($partial)) "guide and Lua agree on: $partial"
    }
    $archiveNote = [IO.File]::ReadAllText((Join-Path $pkg 'RUNTIME-ARCHIVE.txt'))
    Assert-True ($archiveNote.Contains('26CE7C2168D655C35BE39E4F17A295206AA0522DE8782CD57A36C4FEEA382F54') -and $archiveNote.Contains('3,720,192')) 'archive note names hash and size'

    # ------------------------------------------------------------ installer fixtures
    # Steam fixture: account 7 with saves; staged package with fixture hashes and
    # the packaged scripts\ layout from the friend package above.
    $steam = Join-Path $fixture 'steam'
    $userdata = Join-Path $steam 'userdata/7'
    foreach ($f in '287700/local/TPP_GAME_DATA0', '287700/local/PERSONAL_DATA0', '287700/remotecache.vdf', '311340/remote/TPP_GAME_DATA') {
        $p = Join-Path $userdata $f
        $null = New-Item -ItemType Directory -Path (Split-Path -Parent $p) -Force
        [IO.File]::WriteAllText($p, "fixture save $f")
    }
    $ownId = '76561197960265735'
    $userdataBefore = Get-TreeHashes $userdata
    $archiveFx = Join-Path $fixture 'runtime-archive-fixture.dat'
    [IO.File]::WriteAllText($archiveFx, 'INERT runtime archive fixture')
    $wrongArchive = Join-Path $fixture 'wrong-archive.bin'
    [IO.File]::WriteAllText($wrongArchive, 'not the archive')
    $templateGame = New-GameFixture 'template'
    $exeHash = Get-Sha256 (Join-Path $templateGame 'mgsvtpp.exe')
    $steamHash = Get-Sha256 (Join-Path $templateGame 'steam_api64.dll')
    $loader1 = Join-Path $fixture 'loader1.dll'; [IO.File]::WriteAllText($loader1, 'INERT friend loader')
    $loaderPrev = Join-Path $fixture 'loader-prev.dll'; [IO.File]::WriteAllText($loaderPrev, 'INERT earlier loader')
    $stage = Join-Path $fixture 'stage'
    $null = New-LabPackage -RepoRoot $projectRoot -Destination $stage -LoaderPath $loader1 -Version '0.11.0' -ExecutableSha256 $exeHash -SteamDllSha256 $steamHash -ArchiveSha256 (Get-Sha256 $archiveFx)
    Copy-Item -LiteralPath (Join-Path $pkg 'scripts') -Destination (Join-Path $stage 'scripts') -Recurse
    foreach ($b in 'Install-Coop.bat', 'Uninstall-Coop.bat', 'Collect-Logs.bat') { Copy-Item -LiteralPath (Join-Path $pkg $b) -Destination $stage }
    # The role command pins the real archive hash; the staged copy accepts the
    # fixture archive instead (its only change).
    $roleCopy = Join-Path $stage 'scripts/set-session-mode.ps1'
    $roleText = [IO.File]::ReadAllText($roleCopy)
    Assert-Equal ([regex]::Matches($roleText, '26CE7C2168D655C35BE39E4F17A295206AA0522DE8782CD57A36C4FEEA382F54').Count) 1 'role command pins the archive hash once'
    [IO.File]::WriteAllText($roleCopy, $roleText.Replace('26CE7C2168D655C35BE39E4F17A295206AA0522DE8782CD57A36C4FEEA382F54', (Get-Sha256 $archiveFx)), [Text.Encoding]::ASCII)
    $install = Join-Path $stage 'scripts/Install-Coop.ps1'
    $uninstall = Join-Path $stage 'scripts/Uninstall-Coop.ps1'
    $collect = Join-Path $stage 'scripts/Collect-Logs.ps1'
    $common = @('-SteamPath', $steam, '-NonInteractive', '-Yes')

    # Refusals change nothing.
    $refuse = New-GameFixture 'refuse'
    $r = Invoke-Friend $install (@('-GameDirectory', $refuse, '-Role', 'Join', '-HostSteamId', '76561198000000001', '-RuntimeArchive', $archiveFx) + $common)
    Assert-Refused $r 'example ID' 'placeholder host id refused'
    $r = Invoke-Friend $install (@('-GameDirectory', $refuse, '-Role', 'Join', '-HostSteamId', $ownId, '-RuntimeArchive', $archiveFx) + $common)
    Assert-Refused $r 'your own SteamID64' 'own SteamID refused as host id'
    $r = Invoke-Friend $install (@('-GameDirectory', $refuse, '-Role', 'Join', '-HostSteamId', '7656119801234567', '-RuntimeArchive', $archiveFx) + $common)
    Assert-Refused $r '17 digits' 'short host id refused'
    $r = Invoke-Friend $install (@('-GameDirectory', $refuse, '-Role', 'Host', '-Character', 'dog', '-RuntimeArchive', $archiveFx) + $common)
    Assert-Refused $r "Unknown character 'dog'" 'unknown character refused'
    $r = Invoke-Friend $install (@('-GameDirectory', $refuse, '-Role', 'Host', '-RuntimeArchive', $wrongArchive) + $common)
    Assert-Refused $r 'not the expected runtime archive' 'wrong archive hash refused'
    $r = Invoke-Friend $install (@('-GameDirectory', $refuse, '-Role', 'Host') + $common)
    Assert-True ($r.Exit -ne 0 -and (Has $r 'Missing input') -and (Has $r 'mgs-coop-player-runtime.dat')) 'no archive in non-interactive mode: no prompt, clear error'
    $r = Invoke-Friend $install (@('-GameDirectory', $refuse, '-RuntimeArchive', $archiveFx) + $common)
    Assert-Refused $r 'Missing input' 'no role in non-interactive mode'
    $r = Invoke-Friend $install (@('-GameDirectory', $refuse, '-Role', 'Host', '-HostSteamId', $good, '-RuntimeArchive', $archiveFx) + $common)
    Assert-Refused $r 'only used with -Role Join' 'host id with Host refused'
    $modified = New-GameFixture 'modified'
    [IO.File]::WriteAllText((Join-Path $modified 'mgsvtpp.exe'), 'a different executable')
    $r = Invoke-Friend $install (@('-GameDirectory', $modified, '-Role', 'Host', '-RuntimeArchive', $archiveFx) + $common)
    Assert-Refused $r '1.0.15.4' 'unexpected executable refused'
    [IO.File]::WriteAllText((Join-Path $modified 'mgsvtpp.exe'), 'INERT executable fixture; never executed')
    [IO.File]::WriteAllText((Join-Path $modified 'steam_api64.dll'), 'patched steam dll')
    $r = Invoke-Friend $install (@('-GameDirectory', $modified, '-Role', 'Host', '-RuntimeArchive', $archiveFx) + $common)
    Assert-Refused $r 'steam_api64.dll' 'modified Steam DLL refused'
    $emptySteam = Join-Path $fixture 'steam-empty'
    $null = New-Item -ItemType Directory -Path (Join-Path $emptySteam 'userdata') -Force
    $r = Invoke-Friend $install @('-GameDirectory', $refuse, '-Role', 'Host', '-RuntimeArchive', $archiveFx, '-SteamPath', $emptySteam, '-NonInteractive', '-Yes')
    Assert-Refused $r 'Steam account' 'no Steam account with saves refused'
    foreach ($g in $refuse, $modified) {
        Assert-Equal @(Get-ChildItem -LiteralPath $g -Force).Count 2 "refusals left $([IO.Path]::GetFileName($g)) untouched"
    }
    $foreign = New-GameFixture 'foreign'
    [IO.File]::WriteAllText((Join-Path $foreign 'dinput8.dll'), 'another mod')
    $r = Invoke-Friend $install (@('-GameDirectory', $foreign, '-Role', 'Host', '-RuntimeArchive', $archiveFx) + $common)
    Assert-Refused $r 'Another mod''s dinput8.dll' 'unrelated loader refused before any change'
    Assert-Equal @(Get-ChildItem -LiteralPath $foreign -Force).Count 3 'unrelated loader: no backup or mod folder created'
    Assert-Equal ([IO.File]::ReadAllText((Join-Path $foreign 'dinput8.dll'))) 'another mod' 'unrelated loader untouched'

    # Host install (Windows PowerShell 5.1, packaged layout).
    $hostGame = New-GameFixture 'host'
    $hp = Get-InstancePaths -GameDirectory $hostGame
    $r = Invoke-Friend $install (@('-GameDirectory', $hostGame, '-Role', 'Host', '-RuntimeArchive', $archiveFx) + $common)
    Assert-Equal $r.Exit 0 "host install succeeds: $($r.Raw)"
    Assert-True ((Has $r "YOUR SteamID64: $ownId")) 'host install prints the own SteamID64 (base + account)'
    Assert-Equal (Get-Sha256 $hp.Loader) (Get-Sha256 $loader1) 'loader installed'
    Assert-Equal (Get-Sha256 $hp.Archive) (Get-Sha256 $archiveFx) 'runtime archive installed'
    Assert-Equal ([IO.File]::ReadAllText($hp.Marker)) "host-v3`n" 'host marker via the role command'
    Assert-True (-not (Test-Path -LiteralPath $hp.ClientHost)) 'no client-host.txt for the host'
    $hostSaveDir = Join-Path $hp.Mod 'coop-saves'
    Assert-Equal ([IO.File]::ReadAllText($hp.LabIni)) (New-FriendLabIniText -Role Host -SaveDir $hostSaveDir) 'host lab.ini'
    Assert-Equal ([IO.File]::ReadAllText($hp.CoopIni)) (New-FriendCoopIniText -Character default) 'host coop.ini (default character, no master_url)'
    Assert-True (Test-Path -LiteralPath (Join-Path $hostSaveDir '287700/local/TPP_GAME_DATA0') -PathType Leaf) 'save copy seeded (local)'
    Assert-True (Test-Path -LiteralPath (Join-Path $hostSaveDir 'remote/TPP_GAME_DATA') -PathType Leaf) 'save copy seeded (remote)'
    $state = Get-Content -LiteralPath (Join-Path $hp.Mod 'friend-install.json') -Raw | ConvertFrom-Json
    Assert-Equal "$($state.role)|$($state.character)|$(@($state.rollback_ids).Count)" 'Host|default|1' 'install record'
    $backupManifest = Get-Content -LiteralPath (Join-Path $state.latest_backup 'manifest.json') -Raw | ConvertFrom-Json
    Assert-Equal @($backupManifest.verified).Count 3 'backup manifest hashes the verified save files'
    Assert-True (Test-PathInside -Child $state.latest_backup -Parent (Join-Path $hp.Mod 'real-save-backups')) 'backup stored under mgs-coop\real-save-backups'
    Assert-Equal (Get-TreeHashes $userdata) $userdataBefore 'real saves untouched by the install'
    Assert-Equal (Get-Sha256 $hp.Exe) $exeHash 'executable untouched'
    Assert-Equal (Get-Sha256 $hp.SteamApi) $steamHash 'Steam DLL untouched'
    Assert-Equal (Get-Content -LiteralPath (Join-Path $stage 'installed-location.json') -Raw | ConvertFrom-Json).game_directory $hostGame 'installed location recorded in the package'

    # Reinstall (character change) reuses the installed archive and keeps the save copy.
    [IO.File]::WriteAllText((Join-Path $hostSaveDir '287700/local/COOP_PROGRESS'), 'progress made in the co-op copy')
    $r = Invoke-Friend $install (@('-GameDirectory', $hostGame, '-Role', 'Host', '-Character', 'quiet') + $common)
    Assert-Equal $r.Exit 0 "host reinstall succeeds: $($r.Raw)"
    Assert-Equal ([IO.File]::ReadAllText($hp.CoopIni)) (New-FriendCoopIniText -Character quiet) 'reinstall updates the character'
    Assert-True (Test-Path -LiteralPath (Join-Path $hostSaveDir '287700/local/COOP_PROGRESS')) 'reinstall keeps the existing save copy'
    $state = Get-Content -LiteralPath (Join-Path $hp.Mod 'friend-install.json') -Raw | ConvertFrom-Json
    Assert-Equal @($state.rollback_ids).Count 2 'both friend install states recorded'
    Assert-Equal @($state.backups).Count 2 'a new save backup per install'

    # Join install under PowerShell 7 (same scripts).
    $joinGame = New-GameFixture 'join'
    $jp = Get-InstancePaths -GameDirectory $joinGame
    $r = Invoke-Friend $install (@('-GameDirectory', $joinGame, '-Role', 'Join', '-HostSteamId', $good, '-RuntimeArchive', $archiveFx) + $common) -HostExe $pwshPath
    Assert-Equal $r.Exit 0 "join install succeeds (PowerShell 7): $($r.Raw)"
    Assert-Equal ([IO.File]::ReadAllText($jp.Marker)) "client-v1`n" 'join marker'
    Assert-Equal ([IO.File]::ReadAllText($jp.ClientHost)) "$good`n" 'client-host.txt holds the host id'
    Assert-Equal ([IO.File]::ReadAllText($jp.LabIni)) (New-FriendLabIniText -Role Join -SaveDir (Join-Path $jp.Mod 'coop-saves')) 'join lab.ini (role guest)'
    Assert-Equal ([IO.File]::ReadAllText($jp.CoopIni)) (New-FriendCoopIniText -Character quiet) 'joiner defaults to Quiet'
    Assert-True (-not (Has $r 'YOUR SteamID64')) 'joiner is not told to share an id'

    # Prompt path (a double-clicked Install-Coop.bat): role 2, the placeholder
    # first (explained, asked again), character 4, the archive path in quotes, Enter.
    $askGame = New-GameFixture 'prompts'
    $ap = Get-InstancePaths -GameDirectory $askGame
    $r = Invoke-FriendAnswers $install @('-GameDirectory', $askGame, '-SteamPath', $steam) @('2', '76561198000000001', $good, '4', ('"' + $archiveFx + '"'), '')
    Assert-Equal $r.Exit 0 "prompted join install succeeds: $($r.Raw)"
    Assert-True ((Has $r 'example ID') -and (Has $r 'Account details')) 'placeholder explained and asked again; SteamID help shown'
    Assert-Equal ([IO.File]::ReadAllText($ap.ClientHost)) "$good`n" 'prompted host id written'
    Assert-Equal ([IO.File]::ReadAllText($ap.CoopIni)) (New-FriendCoopIniText -Character dd_female) 'character chosen by number'
    # Reinstall through the prompts: Host, Enter for the default character, yes, refresh the save copy.
    [IO.File]::WriteAllText((Join-Path $ap.Mod 'coop-saves/287700/local/COOP_PROGRESS'), 'old copy progress')
    $r = Invoke-FriendAnswers $install @('-GameDirectory', $askGame, '-SteamPath', $steam) @('1', '', 'y', 'y')
    Assert-Equal $r.Exit 0 "prompted reinstall succeeds: $($r.Raw)"
    Assert-True (Has $r "YOUR SteamID64: $ownId") 'role switched to host'
    Assert-Equal ([IO.File]::ReadAllText($ap.Marker)) "host-v3`n" 'host marker after the role switch'
    Assert-Equal ([IO.File]::ReadAllText($ap.CoopIni)) (New-FriendCoopIniText -Character default) 'Enter keeps the host default'
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $ap.Mod 'coop-saves/287700/local/COOP_PROGRESS'))) 'save copy refreshed on request'
    Assert-Equal @(Get-ChildItem -LiteralPath (Join-Path $ap.Mod 'coop-saves-replaced') -Recurse -File -Filter COOP_PROGRESS).Count 1 'old save copy moved aside, not deleted'
    # Cancelling at the confirmation changes nothing.
    $before = Get-TreeHashes $askGame
    $r = Invoke-FriendAnswers $install @('-GameDirectory', $askGame, '-SteamPath', $steam) @('2', $good, '', 'n')
    Assert-True ($r.Exit -eq 1 -and (Has $r 'Cancelled. Nothing was changed.')) "cancel at the confirmation: $($r.Text)"
    Assert-Equal (Get-TreeHashes $askGame) $before 'cancel left the folder unchanged'
    $r = Invoke-FriendAnswers $uninstall @('-GameDirectory', $askGame) @('')
    Assert-Equal $r.Exit 0 "prompted uninstall succeeds: $($r.Raw)"
    Assert-True (-not (Test-Path -LiteralPath $ap.Loader) -and -not (Test-Path -LiteralPath $ap.ClientHost)) 'prompted uninstall removed the loader and the host id file'

    # Failure after the loader was installed rolls back (lab.ini cannot be written).
    $failGame = New-GameFixture 'fail'
    $fp = Get-InstancePaths -GameDirectory $failGame
    $null = New-Item -ItemType Directory -Path $fp.LabIni -Force
    $r = Invoke-Friend $install (@('-GameDirectory', $failGame, '-Role', 'Host', '-RuntimeArchive', $archiveFx) + $common)
    Assert-True ($r.Exit -ne 0 -and (Has $r 'undoing the changes') -and (Has $r 'previous loader state was restored')) "failed install rolls back: $($r.Text)"
    Assert-True (-not (Test-Path -LiteralPath $fp.Loader) -and -not (Test-Path -LiteralPath $fp.Record)) 'rollback removed the loader and record'
    Assert-True (-not (Test-Path -LiteralPath $fp.CoopIni)) 'rollback removed the new coop.ini'
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $fp.Mod 'friend-install.json'))) 'no install record after a rollback'

    # ------------------------------------------------------------ Collect-Logs
    [IO.File]::WriteAllText((Join-Path $hp.Mod 'script-observer.log'), "`r`nMGS Co-op script adapter 0.11.0; first run`r`nfirst_session_secret_line`r`n`r`nMGS Co-op script adapter 0.11.0; executable_fingerprint=accepted`r`nmode=host_v3; peer_acceptance=after_mission_start; max_players=2`r`nsession_status=host_ready`r`nlobby=76561198012345678 path=C:\Users\Alice\AppData\x`r`n")
    [IO.File]::WriteAllText((Join-Path $hp.Mod 'probe.log'), ((1..40000 | ForEach-Object { "probe line $_ steam=76561197960265735" }) -join "`r`n") + "`r`n")
    [IO.File]::WriteAllText((Join-Path $hp.Mod 'early.log'), "early=start; version=0.11.0; lab=1; role=host`r`nsaves=redirected; dir=$hostSaveDir`r`n")
    [IO.File]::WriteAllText((Join-Path $hp.Mod 'host-steamid.txt'), "$ownId`n")
    [IO.File]::WriteAllText((Join-Path $hp.Mod 'lab-status.json'), '{"version":1,"role":"host","session_status":"host_ready"}')
    $outDir = Join-Path $fixture 'desktop'
    $null = New-Item -ItemType Directory -Path $outDir
    $r = Invoke-Friend $collect @('-GameDirectory', $hostGame, '-OutputDirectory', $outDir, '-NonInteractive', '-MaxLogBytes', '65536')
    Assert-Equal $r.Exit 0 "collect logs succeeds: $($r.Raw)"
    $zips = @(Get-ChildItem -LiteralPath $outDir -Filter '*.zip')
    Assert-True ($zips.Count -eq 1 -and $zips[0].Name -cmatch '^mgs-coop-logs-host-[0-9]{8}-[0-9]{6}\.zip$') 'zip named by role and time'
    Assert-True ($zips[0].Length -lt 400KB) 'zip size bounded'
    $logZip = [IO.Compression.ZipFile]::OpenRead($zips[0].FullName)
    try { $logEntries = @($logZip.Entries | ForEach-Object { $_.FullName }) } finally { $logZip.Dispose() }
    Assert-True ($logEntries.Count -ge 8 -and @($logEntries | Where-Object { $_.Contains([string][char]92) }).Count -eq 0) 'log zip entries use forward slashes'
    Assert-True (@($logEntries | Where-Object { $_ -notlike ($zips[0].BaseName + '/*') }).Count -eq 0) 'log zip entries inside one folder'
    $extract = Join-Path $fixture 'collected'
    Expand-Archive -LiteralPath $zips[0].FullName -DestinationPath $extract
    $inner = @(Get-ChildItem -LiteralPath $extract -Directory)
    Assert-Equal $inner.Count 1 'zip holds one folder'
    $collected = $inner[0].FullName
    foreach ($n in 'summary.txt', 'script-observer.log', 'probe.log', 'early.log', 'lab-status.json', 'lab.ini', 'coop.ini', 'script-observe.enabled', 'probe-install.json', 'host-steamid.txt') {
        Assert-True (Test-Path -LiteralPath (Join-Path $collected $n) -PathType Leaf) "collected $n"
    }
    $observer = [IO.File]::ReadAllText((Join-Path $collected 'script-observer.log'))
    Assert-True ($observer.StartsWith('MGS Co-op script adapter 0.11.0; executable_fingerprint') -and !$observer.Contains('first_session_secret_line')) 'script-observer.log from the last adapter start'
    Assert-True ((Get-Item -LiteralPath (Join-Path $collected 'probe.log')).Length -le 70000) 'probe.log tail bounded'
    $everything = (Get-ChildItem -LiteralPath $collected -File | ForEach-Object { [IO.File]::ReadAllText($_.FullName) }) -join "`n"
    Assert-True ($everything -notmatch '(?<![0-9])7656119[0-9]{10}(?![0-9])') 'no complete SteamID in the collected files'
    Assert-True ($everything.Contains('7656119XXXXXX5735') -and $everything.Contains('7656119XXXXXX5678')) 'SteamIDs keep their last 4 digits'
    Assert-True (!$everything.Contains('Alice')) 'user name removed'
    $summary = [IO.File]::ReadAllText((Join-Path $collected 'summary.txt'))
    Assert-True ($summary.Contains('role=host') -and $summary.Contains('real_saves=UNCHANGED') -and $summary.Contains('mgsvtpp.exe sha256=' + $exeHash)) 'summary: role, save check, hashes'
    Assert-True ($summary.Contains('matches_install_record=True')) 'summary: loader matches its record'
    Assert-Equal (Get-TreeHashes $userdata) $userdataBefore 'collecting logs reads the saves only'
    # The .bat entry point passes its arguments through (stdin closed for pause).
    $batOut = '' | & cmd.exe /c (Join-Path $stage 'Collect-Logs.bat') -GameDirectory $joinGame -OutputDirectory $outDir -NonInteractive 2>&1 | Out-String
    Assert-True ($LASTEXITCODE -eq 0 -and @(Get-ChildItem -LiteralPath $outDir -Filter 'mgs-coop-logs-join-*.zip').Count -eq 1) "Collect-Logs.bat works: $batOut"

    # ------------------------------------------------------------ Uninstall
    if ($FakeProcess) {
        # A stand-in process named mgsvtpp.exe running from the folder blocks the uninstall.
        $realExe = [IO.File]::ReadAllBytes($hp.Exe)
        Copy-Item -LiteralPath $FakeProcess -Destination $hp.Exe -Force
        $port = 0
        foreach ($i in 1..50) { $candidate = Get-Random -Minimum 49152 -Maximum 65000; if (Test-LocalPortFree -Port $candidate) { $port = $candidate; break } }
        $fakeGame = Start-Process -FilePath $hp.Exe -ArgumentList '-standalone', '-listenAddr', "127.0.0.1:$port" -WorkingDirectory $hostGame -PassThru
        Start-Sleep -Milliseconds 500
        Assert-True (-not $fakeGame.HasExited) 'stand-in game process runs'
        $r = Invoke-Friend $uninstall @('-GameDirectory', $hostGame, '-NonInteractive', '-Yes')
        Assert-Refused $r 'MGSV is running from this folder' 'uninstall refused while the game runs'
        Assert-True (Test-Path -LiteralPath $hp.Loader) 'nothing removed while the game runs'
        Stop-Process -Id $fakeGame.Id -Force
        $fakeGame.WaitForExit(5000) | Out-Null
        $fakeGame = $null
        Start-Sleep -Milliseconds 300
        [IO.File]::WriteAllBytes($hp.Exe, $realExe)
    } else { Write-Warning 'lab_fake_process not supplied; running-game refusal not tested.' }
    $r = Invoke-Friend $uninstall @('-GameDirectory', $hostGame, '-NonInteractive', '-Yes')
    Assert-Equal $r.Exit 0 "host uninstall succeeds: $($r.Raw)"
    Assert-True ((Has $r 'restored 2 install state(s)') -and (Has $r 'UNCHANGED')) 'both friend installs undone, saves verified'
    Assert-True (-not (Test-Path -LiteralPath $hp.Loader) -and -not (Test-Path -LiteralPath $hp.Record)) 'loader and record removed (none before)'
    Assert-True (-not (Test-Path -LiteralPath $hp.LabIni) -and -not (Test-Path -LiteralPath $hp.CoopIni) -and -not (Test-Path -LiteralPath $hp.Marker)) 'configuration disabled'
    Assert-Equal @(Get-ChildItem -LiteralPath $hp.Mod -Filter 'lab.ini.disabled-*').Count 1 'lab.ini renamed, not deleted'
    Assert-Equal @(Get-ChildItem -LiteralPath $hp.Mod -Filter 'coop.ini.disabled-*').Count 1 'coop.ini renamed, not deleted'
    Assert-Equal @(Get-ChildItem -LiteralPath $hp.Mod -Filter 'friend-install.json.uninstalled-*').Count 1 'install record retired'
    Assert-True ((Test-Path -LiteralPath (Join-Path $hostSaveDir '287700/local/COOP_PROGRESS')) -and (Test-Path -LiteralPath $state.latest_backup)) 'save copy and backups kept'
    Assert-True (-not (Test-Path -LiteralPath $hp.Archive)) 'runtime archive removed with the managed files'
    Assert-Equal (Get-TreeHashes $userdata) $userdataBefore 'real saves untouched by the uninstall'
    $r = Invoke-Friend $uninstall @('-GameDirectory', $hostGame, '-NonInteractive', '-Yes')
    Assert-Refused $r 'No friend-test install record' 'second uninstall changes nothing'

    # Changed real saves are reported, never overwritten.
    $savePath = Join-Path $userdata '287700/local/TPP_GAME_DATA0'
    [IO.File]::WriteAllText($savePath, 'played normally in between')
    $r = Invoke-Friend $uninstall @('-GameDirectory', $joinGame, '-NonInteractive', '-Yes')
    Assert-Equal $r.Exit 4 "join uninstall reports changed saves (exit 4): $($r.Raw)"
    Assert-True ((Has $r 'DIFFERENT') -and (Has $r 'changed: 287700/local/TPP_GAME_DATA0') -and (Has $r 'Nothing was overwritten')) 'difference listed'
    Assert-True ((Has $r 'files\287700\*')) 'restore instructions printed'
    Assert-Equal ([IO.File]::ReadAllText($savePath)) 'played normally in between' 'real save not overwritten'
    Assert-True (-not (Test-Path -LiteralPath $jp.Loader) -and -not (Test-Path -LiteralPath $jp.ClientHost)) 'join folder restored, client-host.txt disabled'
    [IO.File]::WriteAllText($savePath, 'fixture save 287700/local/TPP_GAME_DATA0')
    Assert-Equal (Get-TreeHashes $userdata) $userdataBefore 'fixture saves back to the start'

    # An earlier MGS Co-op install (owned loader, own role and lab.ini) is put back.
    $prevGame = New-GameFixture 'previous'
    $pp = Get-InstancePaths -GameDirectory $prevGame
    $stagePrev = Join-Path $fixture 'stage-prev'
    $null = New-LabPackage -RepoRoot $projectRoot -Destination $stagePrev -LoaderPath $loaderPrev -Version '0.11.0' -ExecutableSha256 $exeHash -SteamDllSha256 $steamHash -ArchiveSha256 (Get-Sha256 $archiveFx)
    $null = & (Join-Path $projectRoot 'tools/package-install.ps1') -Action Install -GameDirectory $prevGame -PackageDirectory $stagePrev -RuntimeArchive $archiveFx -NoPrompt
    [IO.File]::WriteAllText($pp.Marker, "host-v3`n")
    [IO.File]::WriteAllText($pp.LabIni, "# my own lab.ini`nenabled=1`nrole=host`n")
    [IO.File]::WriteAllText($pp.CoopIni, "master_url=http://127.0.0.1:6667/tppstm/gate`ncharacter=snake`n")
    $r = Invoke-Friend $install (@('-GameDirectory', $prevGame, '-Role', 'Join', '-HostSteamId', $good, '-Character', 'dd_female') + $common)
    Assert-Equal $r.Exit 0 "install over an earlier owned loader: $($r.Raw)"
    Assert-True (-not (Test-IniHasKey ([IO.File]::ReadAllText($pp.CoopIni)) 'master_url')) 'an earlier master_url does not survive in coop.ini'
    Assert-Equal @(Get-ChildItem -LiteralPath $pp.Mod -Filter 'coop.ini.before-friend-*').Count 1 'earlier coop.ini copied aside'
    $r = Invoke-Friend $uninstall @('-GameDirectory', $prevGame, '-NonInteractive', '-Yes')
    Assert-Equal $r.Exit 0 "uninstall back to the earlier loader: $($r.Raw)"
    Assert-True ((Has $r 'installed BEFORE the friend test was put back')) 'earlier loader reported'
    Assert-Equal (Get-Sha256 $pp.Loader) (Get-Sha256 $loaderPrev) 'earlier loader restored'
    Assert-Equal ([IO.File]::ReadAllText($pp.Marker)) "host-v3`n" 'earlier role marker restored'
    Assert-True (-not (Test-Path -LiteralPath $pp.ClientHost)) 'friend client-host.txt disabled (none before)'
    Assert-True (-not (Test-Path -LiteralPath $pp.LabIni) -and -not (Test-Path -LiteralPath $pp.CoopIni)) 'lab.ini/coop.ini disabled'
    Assert-True ((Has $r 'coop.ini from before the friend test is kept')) 'earlier ini files reported, not restored'
    Assert-Equal (Get-TreeHashes $userdata) $userdataBefore 'real saves untouched overall'
    Write-Output "friend package tests passed: $script:checks checks (scripts run under $winPs and $pwshPath; fixture only, no game, Steam or network)"
} finally {
    if ($fakeGame -and !$fakeGame.HasExited) { Stop-Process -Id $fakeGame.Id -Force -ErrorAction SilentlyContinue; $fakeGame.WaitForExit(5000) | Out-Null }
    Remove-OwnedFixture
}
