<#
.SYNOPSIS
Two locally controlled MGSV copies, launched from the normal startup screens.
.DESCRIPTION
The release package's local test. No title/menu input or mission commands are
sent. Keyboard/mouse controls the clicked game, controller 0 controls the guest.
Only separate local-test save copies are used. Activation files are restored
from a hash-checked journal after BOTH games exit, including a closed launcher.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$GameDirectory,
    [string]$GuestDirectory,
    [Parameter(Mandatory)][string]$PackageDirectory,
    [ValidateRange(0,3)][int]$World = 3,
    [ValidateSet('native','clips')][string]$Partner = 'native',
    [ValidateSet('','compact','full','off')][string]$Hud = '',
    [switch]$NonInteractive,
    [switch]$Watch,
    [string]$RunDirectory
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
trap { Write-Host ('ERROR: ' + $_.Exception.Message) -ForegroundColor Red; exit 1 }
Import-Module (Join-Path $PSScriptRoot 'FriendCommon.psm1') -Force -DisableNameChecking
Import-Module (Join-Path $PSScriptRoot 'LocalCoopSession.psm1') -Force -DisableNameChecking

function Assert-LocalGamesClosed([string[]]$Roots) {
    foreach ($root in $Roots) {
        if (@(Get-GameProcessInDirectory -Directory $root).Count) {
            throw "Quit MGSV in $root normally before starting or restoring a local test."
        }
    }
}
if ($Watch) {
    # A never-displayed helper: no window APIs, clipboard or game input here.
    $s = Read-LocalCoopSession -RunDirectory $RunDirectory
    if (!$s -or [string]$s.host_root -cne [IO.Path]::GetFullPath($GameDirectory).TrimEnd('\','/')) { throw 'Local cleanup journal does not match the host folder.' }
    while ($true) {
        $s = Read-LocalCoopSession -RunDirectory $RunDirectory
        if (!$s -or [string]$s.state -eq 'off') { exit 0 }
        $running = @((Get-GameProcessInDirectory -Directory $s.host_root)) + @((Get-GameProcessInDirectory -Directory $s.guest_root))
        if (!$running.Count -and !(Test-CoopProcessAlive -Id ([int]$s.launcher_pid) -StartUtc ([string]$s.launcher_start_utc))) {
            Start-Sleep -Seconds 3
            $running = @((Get-GameProcessInDirectory -Directory $s.host_root)) + @((Get-GameProcessInDirectory -Directory $s.guest_root))
            if (!$running.Count) { $null = Restore-LocalCoopSession -Session (Read-LocalCoopSession -RunDirectory $RunDirectory); exit 0 }
        }
        Start-Sleep -Seconds 2
    }
}
Import-Module (Join-Path $PSScriptRoot 'LocalCoopWindows.psm1') -Force -DisableNameChecking
$fixtureOnly = $env:MGS_COOP_TEST_NO_LAUNCH -eq '1'
$interactive = Test-FriendInteractive -NonInteractive:$NonInteractive
$gameRoot = [IO.Path]::GetFullPath((Get-Item -LiteralPath $GameDirectory).FullName).TrimEnd('\','/')
if (!$GuestDirectory) {
    $candidate = $gameRoot + '_guest'
    if (Test-Path -LiteralPath (Join-Path $candidate 'mgsvtpp.exe') -PathType Leaf) { $GuestDirectory = $candidate }
    elseif ($interactive) {
        Write-Host 'Local testing needs a second MGSV game folder (your existing offline guest copy).'
        $GuestDirectory = (Read-Host 'Second game folder containing mgsvtpp.exe (Enter cancels)').Trim().Trim('"')
        if (!$GuestDirectory) { Write-Host 'Cancelled; nothing was changed.'; exit 0 }
    } else { throw 'No second game copy found. Pass -GuestDirectory pointing to your existing MGSV guest folder.' }
}
$guestRoot = [IO.Path]::GetFullPath((Get-Item -LiteralPath $GuestDirectory).FullName).TrimEnd('\','/')
if ($gameRoot.Equals($guestRoot,[StringComparison]::OrdinalIgnoreCase) -or (Test-PathInside -Child $guestRoot -Parent $gameRoot) -or (Test-PathInside -Child $gameRoot -Parent $guestRoot)) { throw 'Choose two separate game folders, neither inside the other.' }
$roots = [ordered]@{host=$gameRoot;guest=$guestRoot}
Assert-LocalGamesClosed @($roots.Values)
foreach ($root in $roots.Values) { $null = Invoke-LocalCoopRecovery -HostRoot $root }
$package = Read-FriendJson (Join-Path $PackageDirectory 'package.json')
if (!$package -or [string]$package.product -ne 'mgs-coop-local-package') { throw 'Run Play-Coop from the complete extracted release package.' }
$payload = @($package.files | Where-Object { $_.target -ceq 'dinput8.dll' })
if ($payload.Count -ne 1) { throw 'The release package has no unique loader entry.' }
$loaderSource = Join-Path $PackageDirectory ([string]$payload[0].source)
if (!(Test-FileHash -Path $loaderSource -Expected ([string]$payload[0].sha256))) { throw 'The release package loader has changed. Extract the ZIP again.' }
$paths = @{}
foreach ($role in $roots.Keys) {
    $p = Get-InstancePaths -GameDirectory $roots[$role]
    Assert-NoReparsePoint -Path $p.Game
    Assert-NoReparsePoint -Path $p.Mod -StopAt $p.Game
    if (Test-CoopPlaySessionLive -GameRoot $p.Game) { throw "[$role] Another co-op launcher is using this game folder. Quit that session first." }
    $null = Invoke-CoopPlayCleanup -GameRoot $p.Game
    if (!(Test-FileHash -Path $p.Exe -Expected ([string]$package.executable_sha256)) -or !(Test-FileHash -Path $p.SteamApi -Expected ([string]$package.steam_dll_sha256))) { throw "[$role] The retail game or Steam DLL does not match this release. Verify the game in Steam first." }
    if (!(Test-FileHash -Path $p.Archive -Expected ([string]$package.runtime_archive.sha256))) { throw "[$role] The checked two-player runtime archive is missing. Install co-op in this game copy first." }
    $record = Read-FriendJson $p.Record
    if (!$record -or [string]$record.product -cne 'mgs-coop-original-loader-probe' -or [string]$record.game_directory -cne $p.Game -or !(Test-FileHash -Path $p.Loader -Expected ([string]$record.dll_sha256))) { throw "[$role] This game copy has no matching owned co-op loader. Install co-op in it first; unrelated loaders are never replaced." }
    if (@($record.PSObject.Properties.Name) -notcontains 'version' -or [string]$record.version -notin @('0.12.0','0.12.1')) { throw "[$role] Install this release in the game copy first." }
    foreach ($name in @('partner.trigger','world.trigger','join.trigger','reset.trigger','worker.ini','worker.enabled')) {
        if (Test-Path -LiteralPath (Join-Path $p.Mod $name)) { throw "[$role] $name belongs to a developer test. Finish that test before local play." }
    }
    $paths[$role] = $p
}
# Preflight BOTH copies completely before updating either owned loader.
foreach ($role in $roots.Keys) {
    $p = $paths[$role]
    if (!(Test-FileHash -Path $p.Loader -Expected ([string]$payload[0].sha256))) {
        Write-Host "[$role] Updating this owned co-op loader to the release in this package."
        $installer = Join-Path $PSScriptRoot 'install.ps1'
        if (!(Test-Path -LiteralPath $installer)) { $installer = Join-Path (Split-Path -Parent $PSScriptRoot) 'package-install.ps1' }
        & $installer -Action Install -GameDirectory $p.Game -PackageDirectory $PackageDirectory -NoPrompt -NoInitialMode
        if (!(Test-FileHash -Path $p.Loader -Expected ([string]$payload[0].sha256))) { throw "[$role] The owned loader upgrade did not finish." }
    }
}
$hostState = Read-FriendJson (Join-Path $paths.host.Mod 'friend-install.json')
if (!$hostState -or [string]$hostState.status -cne 'installed' -or !(Test-Path -LiteralPath $paths.host.CoopSaves -PathType Container)) { throw 'Run Install-Coop.bat for the main game first; its co-op save copy seeds the local test.' }
$hostSteam = 'learn'
if (!$fixtureOnly) {
    $hostSteam = Get-CoopSteamId64 -ActiveUser (Get-SteamActiveUser)
    if (!$hostSteam) { throw 'Start Steam and sign in first. The local link itself stays on this PC.' }
    # The existing offline contract: no server at the master port, and distinct
    # legacy loopback + partner link ports, all on loopback only.
    foreach ($spec in @(@(47899,'tcp'),@(47810,'udp'),@(47811,'udp'),@(47820,'udp'),@(47821,'udp'))) {
        $socket = New-Object Net.Sockets.Socket ([Net.Sockets.AddressFamily]::InterNetwork),([Net.Sockets.SocketType]::Dgram),([Net.Sockets.ProtocolType]::Udp)
        if ($spec[1] -eq 'tcp') { $socket.Dispose(); $socket = New-Object Net.Sockets.Socket ([Net.Sockets.AddressFamily]::InterNetwork),([Net.Sockets.SocketType]::Stream),([Net.Sockets.ProtocolType]::Tcp) }
        try { $socket.ExclusiveAddressUse = $true; $socket.Bind((New-Object Net.IPEndPoint ([Net.IPAddress]::Loopback),([int]$spec[0]))) }
        catch { throw "Local test port $($spec[0]) is in use. Close the earlier local/development test first." }
        finally { $socket.Dispose() }
    }
    $area = Get-LocalCoopWorkingArea
} else { $area = [pscustomobject]@{X=0;Y=0;Width=1920;Height=1080;Dpi=96} }
$layout = Get-LocalCoopLayout -WorkingArea $area
$saveDirs = @{}
foreach ($role in $roots.Keys) {
    $p = $paths[$role]
    $destination = Join-Path $p.Mod 'local-test-saves'
    Assert-NoReparsePoint -Path $destination -StopAt $p.Game
    $problem = Get-FriendSaveDirProblem $destination
    if ($problem) { throw $problem }
    $local = Join-Path $destination '287700\local'
    if (!(Test-Path -LiteralPath (Join-Path $local 'TPP_GAME_DATA0')) -and !(Test-Path -LiteralPath (Join-Path $local 'TPP_GAME_DATA1'))) {
        if (Test-Path -LiteralPath $destination) { throw "[$role] Incomplete local test save copy. Keep it for recovery and choose another test folder; no existing save files were replaced." }
        $temp = $destination + '.seed-' + [Guid]::NewGuid().ToString('N')
        $null = New-Item -ItemType Directory -Path $temp
        # Copy from the MAIN CO-OP COPY only, never Steam userdata or live saves.
        foreach ($file in @(Get-ChildItem -LiteralPath $paths.host.CoopSaves -File -Recurse -Force)) {
            Assert-NoReparsePoint -Path $file.FullName -StopAt $paths.host.CoopSaves
            $relative = Get-RelativeChildPath -Root $paths.host.CoopSaves -Path $file.FullName
            $target = Join-Path $temp $relative
            $null = New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force
            [IO.File]::Copy($file.FullName,$target,$false)
            if ((Get-Sha256 $target) -cne (Get-Sha256 $file.FullName)) { throw 'The co-op save copy changed during local test setup; no game was launched.' }
        }
        Assert-NoReparsePoint -Path $temp -StopAt $p.Game
        [IO.Directory]::Move($temp,$destination)
        Write-Host "[$role] Created a separate local-test save copy; normal co-op progress stays separate."
    }
    foreach ($item in @(Get-ChildItem -LiteralPath $destination -Recurse -Force)) { Assert-NoReparsePoint -Path $item.FullName -StopAt $p.Game }
    $graphics = Join-Path $local 'TPP_GRAPHICS_CONFIG'
    if (!(Test-Path -LiteralPath $graphics)) { throw "[$role] The copied save has no graphics configuration. Start normal co-op once, then retry." }
    $size = $layout.$role
    $width = [Math]::Max(640,[int]$size.ClientWidth)
    $height = [Math]::Max(480,[int]$size.ClientHeight)
    $edited = Set-LocalCoopGraphicsWindowedText -Text ([IO.File]::ReadAllText($graphics)) -Width $width -Height $height
    Write-FriendTextFile -Path $graphics -Text $edited
    $saveDirs[$role] = $destination
}
function Stop-LocalOwnedGames {
    # A startup safety failure closes only the exact processes this launcher made.
    foreach ($role in @($instances.Keys)) {
        $owner = $instances[$role]
        try {
            $p = Get-Process -Id ([int]$owner.ProcessId) -ErrorAction Stop
            $snapshot = @{ProcessId=$p.Id;StartUtc=$p.StartTime.ToUniversalTime().ToString('o');ExecutablePath=$p.Path}
            if (!(Test-LocalCoopOwnerSnapshot -Owner $owner -GameRoot $roots[$role] -Snapshot $snapshot)) { continue }
            $null = $p.CloseMainWindow()
            if (!$p.WaitForExit(2000)) {
                $p = Get-Process -Id ([int]$owner.ProcessId) -ErrorAction Stop
                $snapshot = @{ProcessId=$p.Id;StartUtc=$p.StartTime.ToUniversalTime().ToString('o');ExecutablePath=$p.Path}
                if (Test-LocalCoopOwnerSnapshot -Owner $owner -GameRoot $roots[$role] -Snapshot $snapshot) { Stop-Process -Id $p.Id -Force }
            }
        } catch { Write-Verbose $_ }
    }
}
$run = Join-Path $paths.host.Mod ('local-test-runs\' + [Guid]::NewGuid().ToString('N'))
$s = $null
$instances = @{}
$helper = $null
try {
    # Same per-folder locks as online Play-Coop. Both baselines are journaled
    # independently; e.g. host pass-through and guest's previous offline config.
    $lockRoots = @($roots.Values | Sort-Object)
    Use-CoopPlayMutex -GameRoot $lockRoots[0] -Script {
        Use-CoopPlayMutex -GameRoot $lockRoots[1] -Script {
            Assert-LocalGamesClosed @($roots.Values)
            foreach ($root in $roots.Values) { if (Test-CoopPlaySessionLive -GameRoot $root) { throw 'Another co-op session started during preparation; no local activation files were written.' } }
            $script:s = New-LocalCoopSession -HostRoot $gameRoot -GuestRoot $guestRoot -RunDirectory $run
            $secret = New-CoopSecret
            foreach ($role in $roots.Keys) {
                $text = New-LocalCoopLabIniText -Role $role -SaveDir $saveDirs[$role] -HostPort 47820 -GuestPort 47821 -World $World -Partner $Partner -HostSteamId $hostSteam
                $character = 'quiet'
                if ($role -eq 'host') { $character = [string](Get-FriendProperty $hostState 'character' 'default') }
                if (!(Test-FriendCharacter $character)) { $character = 'default' }
                $coop = New-CoopPlayCoopIniText -Character $character -Hud $Hud
                $utf8 = New-Object Text.UTF8Encoding $false
                Set-LocalCoopSessionFile -Session $s -Role $role -Name 'lab.ini' -Bytes $utf8.GetBytes($text)
                Set-LocalCoopSessionFile -Session $s -Role $role -Name 'link-secret.txt' -Bytes ([Text.Encoding]::ASCII.GetBytes($secret + "`r`n"))
                Set-LocalCoopSessionFile -Session $s -Role $role -Name 'coop.ini' -Bytes $utf8.GetBytes($coop)
                Set-LocalCoopSessionFile -Session $s -Role $role -Name 'script-observe.enabled' -Bytes ([Text.Encoding]::ASCII.GetBytes("offline-v1`n"))
            }
        }
    }
    if ($fixtureOnly) { Write-Host 'TEST: local two-window activation checked; no games launched.'; exit 0 }
    if ($env:MGS_COOP_TEST_NO_WATCHDOG -ne '1') {
        $watchCommand = '& ' + "'" + $PSCommandPath.Replace("'","''") + "' -Watch -GameDirectory '" + $gameRoot.Replace("'","''") + "' -PackageDirectory '" + $PackageDirectory.Replace("'","''") + "' -RunDirectory '" + $run.Replace("'","''") + "'"
        $encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($watchCommand))
        $winPs = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
        $helper = Start-Process -FilePath $winPs -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-EncodedCommand',$encoded) -WindowStyle Hidden -PassThru
    }
    if (!$helper) { throw 'The background cleanup helper could not start; no games were launched.' }
    $s.watchdog_pid = $helper.Id
    $s.watchdog_start_utc = $helper.StartTime.ToUniversalTime().ToString('o')
    Save-LocalCoopSession -Session $s
    Write-Host ''
    if ($layout.Orientation -eq 'vertical') { Write-Host 'LOCAL TWO-WINDOW TEST: Host above, Guest below.' -ForegroundColor Cyan }
    else { Write-Host 'LOCAL TWO-WINDOW TEST: Host on the left, Guest on the right.' -ForegroundColor Cyan }
    Write-Host '1. Click through the normal startup/login notices yourself in each game.'
    Write-Host '2. Continue into the same supported scene in both copies, as two players would.'
    Write-Host '3. The host chooses travel or checkpoint Continue normally; the guest follows.'
    Write-Host 'Keyboard/mouse: click the game you want to control. Controller 0: Guest.'
    Write-Host 'No keys, menus, Continue, teleports or mission selection are automated.'
    Write-Host '[R] Arrange both windows again. Quit BOTH games normally to end the test.'
    Write-Host 'Closing this launcher leaves the games running; the hidden helper restores settings after both exit.'
    $startup = @{}
    foreach ($role in $roots.Keys) {
        $startup[$role] = @{Safe=$false;Started=[DateTime]::UtcNow;Texts=@{};Offsets=@{}}
        foreach ($name in @('early.log','steam-observer.log','script-observer.log')) {
            $startup[$role].Texts[$name] = ''
            $startup[$role].Offsets[$name] = Get-FileLengthOrZero (Join-Path $paths[$role].Mod $name)
        }
    }
    try {
        foreach ($role in @('host','guest')) {
            # GUI launch avoids inheriting the launcher's output pipes.
            # Only the user's explicit Local Test selection reaches this launch.
            $psi = New-Object Diagnostics.ProcessStartInfo
            $psi.FileName = $paths[$role].Exe
            $psi.WorkingDirectory = $roots[$role]
            $psi.UseShellExecute = $true
            $p = [Diagnostics.Process]::Start($psi)
            if (!$p -or $p.HasExited) { throw "[$role] The game exited before its startup could be verified." }
            $owner = @{ProcessId=$p.Id;StartUtc=$p.StartTime.ToUniversalTime().ToString('o');ExecutablePath=$paths[$role].Exe}
            $snapshot = @{ProcessId=$p.Id;StartUtc=$p.StartTime.ToUniversalTime().ToString('o');ExecutablePath=$p.Path}
            if (!(Test-LocalCoopOwnerSnapshot -Owner $owner -GameRoot $roots[$role] -Snapshot $snapshot)) { throw "[$role] The launched process did not match this game copy." }
            $instances[$role] = $owner
        }
    } catch {
        Stop-LocalOwnedGames
        throw
    }
    $windowState = New-LocalCoopWindowLayoutState
    $last = ''
    $windowWarnings = @{} 
    while (@($roots.Values | Where-Object { @(Get-GameProcessInDirectory -Directory $_).Count }).Count) {
        foreach ($role in $roots.Keys) {
            foreach ($runningGame in @(Get-GameProcessInDirectory -Directory $roots[$role])) {
                # Folder discovery returns Id/Path rows; resolve a live process
                # before reading its creation time. A normal exit between polls
                # is harmless and must not prevent cleanup.
                $live = Get-Process -Id $runningGame.Id -ErrorAction SilentlyContinue
                if (!$live) { continue }
                try {
                    $snapshot = @{ProcessId=$live.Id;StartUtc=$live.StartTime.ToUniversalTime().ToString('o');ExecutablePath=$live.Path}
                } catch {
                    if (!(Get-Process -Id $runningGame.Id -ErrorAction SilentlyContinue)) { continue }
                    Stop-LocalOwnedGames
                    throw "[$role] The running game could not be verified. Quit both game copies manually so the cleanup helper can restore settings."
                }
                if (!(Test-LocalCoopOwnerSnapshot -Owner $instances[$role] -GameRoot $roots[$role] -Snapshot $snapshot)) {
                    Stop-LocalOwnedGames
                    throw "[$role] An unexpected game process appeared. Quit both game copies manually; the cleanup helper will restore settings when both are closed."
                }
            }
            if ($startup[$role].Safe) { continue }
            foreach ($name in @('early.log','steam-observer.log','script-observer.log')) {
                $delta = Read-CoopLogDelta -Path (Join-Path $paths[$role].Mod $name) -Offset $startup[$role].Offsets[$name]
                $startup[$role].Texts[$name] += $delta.Text
                $startup[$role].Offsets[$name] = $delta.NewOffset
            }
            $final = ([DateTime]::UtcNow - $startup[$role].Started).TotalSeconds -ge 120
            $proof = Get-LocalCoopLaunchEvidence -Role $role -EarlyLog $startup[$role].Texts['early.log'] -SteamLog $startup[$role].Texts['steam-observer.log'] -ObserverLog $startup[$role].Texts['script-observer.log'] -SaveDir $saveDirs[$role] -Final:$final
            if ($proof.State -eq 'unsafe') { Stop-LocalOwnedGames; throw "[$role] Startup did not confirm save-copy isolation ($($proof.Reason)); only this test's game processes were closed." }
            if ($proof.State -eq 'ok') { $startup[$role].Safe = $true; Write-Host "[$role] Separate local test saves and offline startup confirmed." -ForegroundColor Green }
        }
        $report = Set-LocalCoopWindowLayout -Instances $instances -GameRoots $roots -WorkingArea $area -State $windowState
        foreach ($row in @($report.Results)) {
            if ($row.Reason -in @('placement_refused','game_size_not_applied') -and !$windowWarnings.ContainsKey($row.Role + '/' + $row.Reason)) {
                $windowWarnings[$row.Role + '/' + $row.Reason] = $true
                Write-Warning "[$($row.Role)] The game did not accept its window rectangle. Press R in this console to arrange again, or adjust the game window manually."
            }
        }
        $lines = foreach ($role in $roots.Keys) {
            $status = Read-CoopStatusFile -Path $paths[$role].LinkStatus -ExpectedPid ([int]$instances[$role].ProcessId)
            "$role`: " + (Format-CoopStatusLine -Status $status -Role $role -OwnBuild ([string]$package.version))
        }
        $line = $lines -join ' | '
        if ($line -cne $last) { Write-Host $line; $last = $line }
        try {
            if ([Console]::KeyAvailable -and [Console]::ReadKey($true).Key -eq 'R') {
                $area = Get-LocalCoopWorkingArea
                $null = Set-LocalCoopWindowLayout -Instances $instances -GameRoots $roots -WorkingArea $area -State $windowState -Retile
                Write-Host 'Windows arranged. Click either game to control it.'
            }
        } catch { Write-Verbose $_ }
        Start-Sleep -Milliseconds 500
    }
} finally {
    if ($s) {
        # Never restore underneath a live game. On errors/closed launcher the
        # hidden helper takes over; otherwise this console owns normal cleanup.
        $running = @($roots.Values | Where-Object { @(Get-GameProcessInDirectory -Directory $_).Count })
        if (!$running.Count) {
            $result = Restore-LocalCoopSession -Session (Read-LocalCoopSession -RunDirectory $run)
            if (@($result.Kept).Count) { Write-Warning ('Changed files were kept for review: ' + (@($result.Kept) -join ', ')) }
            else { Write-Host 'Local test ended. Both game folders returned to their previous setup.' -ForegroundColor Green }
        } elseif (!$helper -or $helper.HasExited) {
            Write-Warning 'Keep this launcher open and quit BOTH games normally; settings are restored after both exit.'
            while (@($roots.Values | Where-Object { @(Get-GameProcessInDirectory -Directory $_).Count }).Count) { Start-Sleep -Seconds 2 }
            $null = Restore-LocalCoopSession -Session (Read-LocalCoopSession -RunDirectory $run)
        }
    }
}
exit 0