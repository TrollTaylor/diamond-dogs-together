#Requires -Version 7
<#
.SYNOPSIS
Prepares the host (MGS_TPP) and guest (MGS_TPP_guest) folders for a single-PC lab run.

.DESCRIPTION
Idempotent. For each selected instance:
  1. verifies mgsvtpp.exe and steam_api64.dll hashes and that the game is not
     running from that folder;
  2. installs the built loader through tools/package-install.ps1 (owned install
     with backups and rollback; the guest's probe-install.json records the guest
     path) together with the runtime archive copied from the host;
  3. selects the session role with the same checks and bytes as
     tools/set-session-mode.ps1 (host-v3 / client-v1 + client-host.txt = host SteamID64);
  4. writes mgs-coop/lab.ini and mgs-coop/coop.ini (docs/lab-protocol.md);
  5. backs up the real saves (read-only) with a SHA-256 manifest into
     build/lab/save-backups/<ts>/ and seeds each save_dir with COPIES;
  6. sets the LAB COPY of TPP_GRAPHICS_CONFIG to Windowed 1280x720 with a
     recomputed integrity tag (refuses if the existing tag does not verify).
Nothing is ever written below Steam\userdata, and the game is never launched.
The lab coop.ini redirects the master server to the local fuse even without
lab.ini, so use -Disable to switch the lab off again: it renames the generated
lab.ini and coop.ini and restores your own files that step 4 set aside as
*.user-<ts>.

-Offline prepares the offline lab mode (docs/offline-partner-spike-plan.md 2.5,
section 4) instead: marker offline-v1 on BOTH folders, coop.ini master_url on a
closed local port (lab.json offline.masterPort, default
http://127.0.0.1:47899/tppstm/gate; no fuse, no server is reached) and lab.ini
with the partner spike keys (net=1, net_listen_port/net_peer_port from
lab.json ports.netHost/netGuest, partner=1 and the lab.json "partner"
defaults). -Character overrides the coop.ini character of the offline lab
for this preparation ('name' for both roles, 'host:name' / 'guest:name' for
one): lab.json offline.characters makes the GUEST play Quiet (the animation /
appearance test condition), so a regression rerun of the proven E0-E8 uses
-Character default. -PartnerSetting overrides single lab.ini partner keys for this
preparation ('key=value' for both roles, 'host:key=value' / 'guest:key=value'
for one), for example -PartnerSetting guest:net_latency_ms=100,guest:net_loss_permille=200
for E7. The coop.ini characters come from lab.json offline.characters (default
on both). Running the script again without -Offline switches both folders back
to host-v3 / client-v1 and the fuse URL. -Disable archives an offline-v1
marker as script-observe.enabled.disabled-<ts>, leaving the native script
adapter in pass-through mode; other explicit markers stay as they are.

-Lan (with -Offline) is the one-PC LAN rehearsal of the two-PC play mode
(docs/two-pc-play.md): both lab.ini files get net_bind=any, the guest's
net_peer_addr is this PC's LAN IPv4 (-LanAddress, default: the first IPv4 of
an Up adapter with a default gateway) and a fresh random link secret is
written to mgs-coop\link-secret.txt in both folders (a non-loopback bind
needs it). Without -Lan, and with -Disable, link-secret.txt is deleted.
-WanSim good|bad|awful (with -Offline) adds the lab.json wanSim impairment
preset (one-way per instance), net_seed (host 1, guest 2) and
partner_interp=auto. The firewall may ask about mgsvtpp.exe for -Lan; these
scripts never change firewall settings.

.EXAMPLE
pwsh -File tools/lab/Prepare-LabInstance.ps1 -WhatIf
pwsh -File tools/lab/Prepare-LabInstance.ps1
pwsh -File tools/lab/Prepare-LabInstance.ps1 -Role guest -Reseed
pwsh -File tools/lab/Prepare-LabInstance.ps1 -Offline
pwsh -File tools/lab/Prepare-LabInstance.ps1 -Offline -PartnerSetting host:partner_force_exists=1,partner_offset_cm=100
pwsh -File tools/lab/Prepare-LabInstance.ps1 -Offline -Character default
pwsh -File tools/lab/Prepare-LabInstance.ps1 -Offline -Lan -WanSim bad
pwsh -File tools/lab/Prepare-LabInstance.ps1 -Disable
#>
[CmdletBinding(SupportsShouldProcess)]
param(
    [ValidateSet('both', 'host', 'guest')][string]$Role = 'both',
    [string]$ConfigPath = (Join-Path $PSScriptRoot 'lab.json'),
    # Host SteamID64 override (default: lab.json identity.hostSteamId, 'auto' = Steam ActiveUser).
    [string]$HostSteamId,
    [string]$Loader,
    [string]$RuntimeArchive,
    # Replace already seeded lab saves with fresh copies (old lab copies are archived, not deleted).
    [switch]$Reseed,
    [switch]$SkipSaves,
    [switch]$SkipLoader,
    [switch]$SkipGraphics,
    # Replace the guest's hardlinked mgsvtpp.exe with an independent verified copy.
    [switch]$CopyGuestExe,
    # Turn the lab off: rename generated lab.ini/coop.ini to *.disabled-<ts> and
    # restore the newest *.user-<ts> copy of the user's own files.
    [switch]$Disable,
    # Offline lab mode: marker offline-v1, closed-port master URL, partner lab.ini keys.
    [switch]$Offline,
    # Offline only: lab.ini partner key overrides, 'key=value' or 'host:key=value' / 'guest:key=value'.
    [string[]]$PartnerSetting = @(),
    # Offline only: coop.ini character override, 'name' or 'host:name' / 'guest:name'.
    [string[]]$Character = @(),
    # Offline only: one-PC LAN rehearsal (net_bind=any, guest targets the LAN IPv4, random link secret).
    [switch]$Lan,
    # -Lan: this PC's LAN IPv4 (default: detected).
    [string]$LanAddress,
    # Offline only: lab.json wanSim preset (off, good, bad, awful).
    [string]$WanSim = 'off',
    # Also accept a loader owned by the folder's Diamond Dogs Together drop-in
    # mgs-coop\package.json (a drag-and-drop install writes no probe-install.json;
    # tools/package-install.ps1 -AcceptDropInLoader). The private worker
    # (tools/worker/Start-WorkerSession.ps1) passes it; restore is exact.
    [switch]$AcceptDropInLoader
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'LabCommon.psm1') -Force
$script:LabCmdlet = $PSCmdlet
function Confirm-LabAction([string]$Target, [string]$Action) { return $script:LabCmdlet.ShouldProcess($Target, $Action) }

$config = Get-LabConfig -ConfigPath $ConfigPath
$repo = $config.Paths.repo
$labRoot = $config.Paths.runDir
$stamp = New-RunStamp
$roles = if ($Role -eq 'both') { @('host', 'guest') } else { @($Role) }
$dirs = @{ host = $config.Paths.hostDir; guest = $config.Paths.guestDir }
if ($dirs.host.Equals($dirs.guest, [StringComparison]::OrdinalIgnoreCase)) { throw 'lab.json hostDir and guestDir must differ.' }
if (@($PartnerSetting | Where-Object { $_ }).Count -and !$Offline) { throw '-PartnerSetting applies only with -Offline.' }
if (@($Character | Where-Object { $_ }).Count -and !$Offline) { throw '-Character applies only with -Offline (the fuse lab uses lab.json characters).' }
if ($Offline -and $Disable) { throw '-Offline and -Disable cannot be combined (-Disable switches either mode off).' }
if (($Lan -or $LanAddress -or $WanSim -cne 'off') -and !$Offline) { throw '-Lan, -LanAddress and -WanSim apply only with -Offline.' }
if ($LanAddress -and !$Lan) { throw '-LanAddress applies only with -Lan.' }

# ---------------------------------------------------------------- -Disable
if ($Disable) {
    foreach ($r in $roles) {
        $p = Get-InstancePaths -GameDirectory $dirs[$r]
        if (@(Get-GameProcessInDirectory -Directory $p.Game).Count) { throw "MGSV is running from $($p.Game); close it first." }
        foreach ($file in $p.LabIni, $p.CoopIni) {
            $leaf = Split-Path -Leaf $file
            $cleared = !(Test-Path -LiteralPath $file -PathType Leaf)
            if (!$cleared) {
                if (!(Test-LabGeneratedText ([IO.File]::ReadAllText($file)))) { Write-Warning "Not generated by the lab scripts; left unchanged: $file"; continue }
                $target = "$file.disabled-$stamp"
                if (Confirm-LabAction $file "Rename to $target") { Move-Item -LiteralPath $file -Destination $target; Write-Output "[$r] disabled $file" }
                $cleared = !(Test-Path -LiteralPath $file -PathType Leaf) -or $WhatIfPreference
            }
            # Put back the user's own file that Prepare set aside as <name>.user-<ts>.
            $names = @(Get-ChildItem -LiteralPath $p.Mod -File -Filter "$leaf.user-*" -ErrorAction SilentlyContinue | ForEach-Object { $_.Name })
            $newest = Select-NewestUserBackup -FileName $leaf -Candidates $names
            if ($cleared -and $newest) {
                $source = Join-Path $p.Mod $newest
                if (Confirm-LabAction $source "Restore as $file") { Move-Item -LiteralPath $source -Destination $file; Write-Output "[$r] restored your previous $leaf (from $newest)" }
            }
        }
        # The -Lan link secret goes with the lab configuration.
        if (Test-Path -LiteralPath $p.LinkSecret -PathType Leaf) {
            Assert-NoReparsePoint -Path $p.LinkSecret -StopAt $p.Game
            if (Confirm-LabAction $p.LinkSecret 'Delete the -Lan link secret') { Remove-Item -LiteralPath $p.LinkSecret -Force; Write-Output "[$r] deleted link-secret.txt" }
        }
        # Archive the offline marker so a later normal launch stays pass-through.
        # The legacy online role markers enable sessions independently of lab.ini.
        $current = if (Test-Path -LiteralPath $p.Marker -PathType Leaf) { [IO.File]::ReadAllText($p.Marker) } else { '' }
        $markerAction = Get-LabDisabledMarker -Role $r -Marker $current
        if ($markerAction -ceq 'archive') {
            Assert-NoReparsePoint -Path $p.Marker -StopAt $p.Game
            $markerArchive = [IO.Path]::GetFullPath("$($p.Marker).disabled-$stamp")
            if (!(Test-PathInside -Child $p.Marker -Parent $p.Mod) -or !(Test-PathInside -Child $markerArchive -Parent $p.Mod)) { throw 'Role marker archive escaped the mod folder.' }
            if (Confirm-LabAction $p.Marker "Archive offline-v1 marker as $markerArchive") {
                Move-Item -LiteralPath $p.Marker -Destination $markerArchive
                Write-Output "[$r] offline-v1 marker archived; normal launches use pass-through (no co-op session)."
            }
        } elseif ($current.Trim() -ceq 'host-v1') {
            Write-Warning "[$r] the role marker is host-v1 (the offline lab fallback marker, if lab.json offline.marker selected it); left unchanged."
        }
    }
    Write-Output 'Lab configuration disabled (lab.ini and coop.ini renamed, your previous files restored, link secrets deleted, offline-v1 markers archived). The loaders, other explicit markers and lab save copies were left in place.'
    return
}

# ---------------------------------------------------------------- checks (no writes)
$identityOverride = if ($HostSteamId) { $HostSteamId } else { [string](Get-ConfigValue $config.Identity 'hostSteamId' 'auto') }
$identity = Get-HostIdentity -OverrideSteamId $identityOverride -SteamPath $config.Paths.steamRoot
Write-Output "Host SteamID64 resolved (source=$($identity.Source)); it is written only to lab.ini/client-host.txt."
# docs/lab-protocol.md: the host writes its own id to host-steamid.txt. A value
# from an earlier run that differs means the guest would target the wrong host.
$previousHostId = (Read-SharedText -Path (Get-InstancePaths -GameDirectory $dirs.host).HostId).Trim()
if ($previousHostId -and $previousHostId -cne $identity.SteamId64) {
    Write-Warning ("The host's host-steamid.txt from an earlier run names a different Steam account than the one resolved now (source=$($identity.Source)). " +
        'If the host runs under that account, log Steam into it or pass -HostSteamId. Start-Lab and the smoke test stop with host_id_mismatch when they differ during a run.')
}
$loaderPath = if ($Loader) { [IO.Path]::GetFullPath($Loader) } else { $config.Paths.loader }
$archivePath = if ($RuntimeArchive) { [IO.Path]::GetFullPath($RuntimeArchive) } else { $config.Paths.runtimeArchive }
$expected = @{ exe = [string]$config.Hashes.exe; steam = [string]$config.Hashes.steamApi; archive = [string]$config.Hashes.runtimeArchive }
$linkSecret = $null
if ($Lan) {
    if (!$LanAddress) {
        $LanAddress = Select-CoopLanAddress -Configurations @(Get-CoopLocalConfigurations)
        if (!$LanAddress) { throw 'No LAN IPv4 address found; pass -LanAddress.' }
    }
    if (!(Test-CoopPlayPeerAddress $LanAddress)) { throw "-LanAddress must be a dotted IPv4 address of this PC (not $LanAddress)." }
    Write-Output "LAN rehearsal: the guest targets $LanAddress (net_bind=any on both). Windows Firewall may ask about mgsvtpp.exe; allow it, or the copies cannot reach each other. These scripts change no firewall settings."
    if ($roles.Count -eq 2) { $linkSecret = New-CoopSecret }
    else {
        # One role only: keep the other copy's secret, both must use the same one.
        $other = if ($roles[0] -eq 'host') { 'guest' } else { 'host' }
        $otherSecret = (Get-InstancePaths -GameDirectory $dirs[$other]).LinkSecret
        $bytes = if (Test-Path -LiteralPath $otherSecret -PathType Leaf) { [IO.File]::ReadAllBytes($otherSecret) } else { $null }
        if ((Get-CoopSecretFileStatus $bytes) -ne 'ok') { throw "-Lan -Role $($roles[0]) needs the $other copy's link-secret.txt; prepare both roles with -Lan." }
        $linkSecret = [Text.Encoding]::ASCII.GetString($bytes, 0, 20)
    }
}
if (!$SkipLoader) {
    if (!(Test-Path -LiteralPath $loaderPath -PathType Leaf)) { throw "Loader not found: $loaderPath. Build first: pwsh -File tools/build.ps1 -Configuration Release" }
    if (!(Test-FileHash -Path $archivePath -Expected $expected.archive)) { throw "Runtime archive missing or hash differs (expected $($expected.archive)): $archivePath" }
}
$instances = @{}
foreach ($r in $roles) {
    $p = Get-InstancePaths -GameDirectory $dirs[$r]
    if (!(Test-Path -LiteralPath $p.Game -PathType Container)) { throw "[$r] game folder not found: $($p.Game)" }
    Assert-NoReparsePoint -Path $p.Game
    if (Test-Path -LiteralPath $p.Mod) { Assert-NoReparsePoint -Path $p.Mod -StopAt $p.Game }
    if (@(Get-GameProcessInDirectory -Directory $p.Game).Count) { throw "[$r] MGSV is running from $($p.Game); close it normally first." }
    if (!(Test-FileHash -Path $p.Exe -Expected $expected.exe)) { throw "[$r] mgsvtpp.exe is missing or not 1.0.15.4 ($($expected.exe)): $($p.Exe)" }
    if (!(Test-FileHash -Path $p.SteamApi -Expected $expected.steam)) { throw "[$r] steam_api64.dll is missing or not the shipped DLL ($($expected.steam))" }
    $overrides = if ($Offline) { ConvertFrom-LabPartnerSettingArgs -Items $PartnerSetting -Role $r } else { $null }
    $characterOverride = if ($Offline) { ConvertFrom-LabCharacterArgs -Items $Character -Role $r } else { $null }
    $settings = Get-LabInstanceSettings -Role $r -Config $config -GameDirectory $p.Game -HostSteamId64 $identity.SteamId64 -Offline:$Offline -PartnerOverrides $overrides -CharacterOverride $characterOverride `
        -Lan:$Lan -LanAddress $(if ($Lan) { $LanAddress } else { '' }) -WanSim $WanSim
    if ($Offline) { Write-Output "[$r] coop.ini character=$($settings.Character)" }
    if (Test-PathInside -Child $settings.SaveDir -Parent $identity.UserData) { throw 'Internal error: save_dir inside Steam userdata.' }
    $instances[$r] = [pscustomobject]@{ Role = $r; Paths = $p; Settings = $settings }
}

# ---------------------------------------------------------------- guest exe hardlink
if ($instances.ContainsKey('guest')) {
    $gp = $instances.guest.Paths
    $link = (Get-Item -LiteralPath $gp.Exe).LinkType
    if ($link -eq 'HardLink') {
        if ($CopyGuestExe) {
            $hostExe = (Get-InstancePaths -GameDirectory $dirs.host).Exe
            if (!(Test-FileHash -Path $hostExe -Expected $expected.exe)) { throw 'Host mgsvtpp.exe hash differs; not copying.' }
            if (Confirm-LabAction $gp.Exe 'Replace the hardlink with an independent copy of the host executable') {
                $temp = Join-Path $gp.Game ('mgsvtpp.exe.' + [Guid]::NewGuid().ToString('N') + '.tmp')
                [IO.File]::Copy($hostExe, $temp, $false)
                if (!(Test-FileHash -Path $temp -Expected $expected.exe)) { Remove-Item -LiteralPath $temp; throw 'Copied executable hash differs.' }
                # Removing the guest's directory entry leaves the host's link (and data) intact.
                Remove-Item -LiteralPath $gp.Exe
                Move-Item -LiteralPath $temp -Destination $gp.Exe
                if (!(Test-FileHash -Path $hostExe -Expected $expected.exe) -or !(Test-FileHash -Path $gp.Exe -Expected $expected.exe)) { throw 'Executable hash check failed after the copy.' }
                Write-Output '[guest] mgsvtpp.exe is now an independent copy (hash verified on both folders).'
            }
        } else {
            Write-Warning '[guest] mgsvtpp.exe is a hardlink to the host executable. Nothing in the lab writes it, but -CopyGuestExe makes it an independent copy.'
        }
    }
}

# ---------------------------------------------------------------- 1. loader
if (!$SkipLoader) {
    $version = Get-ProjectVersion -RepoRoot $repo
    $loaderHash = Get-Sha256 $loaderPath
    $stage = $null
    foreach ($r in $roles) {
        $p = $instances[$r].Paths
        $record = if (Test-Path -LiteralPath $p.Record -PathType Leaf) { Get-Content -LiteralPath $p.Record -Raw | ConvertFrom-Json } else { $null }
        $installed = $record -and [string](Get-ConfigValue $record 'product' '') -eq 'mgs-coop-original-loader-probe' -and
            [string](Get-ConfigValue $record 'game_directory' '') -eq $p.Game -and [string](Get-ConfigValue $record 'dll_sha256' '') -eq $loaderHash -and
            [string](Get-ConfigValue $record 'version' '') -eq $version -and (Test-FileHash -Path $p.Loader -Expected $loaderHash) -and
            (Test-FileHash -Path $p.Archive -Expected $expected.archive)
        if ($installed) { Write-Output "[$r] loader already installed ($($loaderHash.Substring(0, 12))...)."; continue }
        $arguments = @{ Action = 'Install'; GameDirectory = $p.Game; RuntimeArchive = $archivePath; NoPrompt = $true }
        if ($AcceptDropInLoader) { $arguments.AcceptDropInLoader = $true }
        if ($record -and [string](Get-ConfigValue $record 'game_directory' '') -ne $p.Game) {
            $recorded = [string](Get-ConfigValue $record 'game_directory' '')
            if ($r -eq 'guest' -and $recorded.Equals($dirs.host, [StringComparison]::OrdinalIgnoreCase)) {
                # The guest folder was created as a copy of the host installation.
                $arguments.AcceptRecordCopiedFrom = $recorded
            } elseif (!$AcceptDropInLoader) { throw "[$r] probe-install.json names another folder ($recorded); refusing." }
            # -AcceptDropInLoader: the installer decides (a drop-in owned loader, or its own refusal).
        }
        if (!(Confirm-LabAction $p.Game "Install loader $($loaderHash.Substring(0, 12))... and runtime archive (owned install with rollback)")) { continue }
        if (!$stage) {
            $stage = Join-Path $labRoot ('package/' + $version + '-' + $loaderHash.Substring(0, 8) + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8))
            $null = New-LabPackage -RepoRoot $repo -Destination $stage -LoaderPath $loaderPath -Version $version `
                -ExecutableSha256 $expected.exe -SteamDllSha256 $expected.steam -ArchiveSha256 $expected.archive
        }
        & (Join-Path $repo 'tools/package-install.ps1') @arguments -PackageDirectory $stage | ForEach-Object { Write-Output "[$r] $_" }
        if (!(Test-FileHash -Path $p.Loader -Expected $loaderHash)) { throw "[$r] loader hash differs after installation." }
    }
}

# ---------------------------------------------------------------- 2. role markers
foreach ($r in $roles) {
    $p = $instances[$r].Paths
    $marker = $instances[$r].Settings.Marker
    $current = if (Test-Path -LiteralPath $p.Marker -PathType Leaf) { [IO.File]::ReadAllText($p.Marker) } else { '' }
    $clientHost = if (Test-Path -LiteralPath $p.ClientHost -PathType Leaf) { [IO.File]::ReadAllText($p.ClientHost) } else { '' }
    $wantHost = if ($r -eq 'guest') { $identity.SteamId64 + "`n" } else { $clientHost }
    if ($current -ceq "$marker`n" -and $clientHost -ceq $wantHost) { Write-Output "[$r] role marker already $marker."; continue }
    if (!(Confirm-LabAction $p.Marker "Select role $marker")) { continue }
    # Same checks and exact ASCII/LF bytes as tools/set-session-mode.ps1, with the
    # archive hash taken from lab.json: only an owned, unmodified loader gets a role.
    $record = if (Test-Path -LiteralPath $p.Record -PathType Leaf) { Get-Content -LiteralPath $p.Record -Raw | ConvertFrom-Json } else { $null }
    if (!$record -or [string](Get-ConfigValue $record 'product' '') -ne 'mgs-coop-original-loader-probe' -or
        [string](Get-ConfigValue $record 'game_directory' '') -ne $p.Game -or
        !(Test-FileHash -Path $p.Loader -Expected ([string](Get-ConfigValue $record 'dll_sha256' '0')))) {
        throw "[$r] the loader is not an owned installation for this folder; install it first (without -SkipLoader)."
    }
    if (!(Test-FileHash -Path $p.Archive -Expected $expected.archive)) { throw "[$r] runtime archive missing or changed." }
    foreach ($path in $p.Marker, $p.ClientHost) { if (Test-Path -LiteralPath $path) { Assert-NoReparsePoint -Path $path -StopAt $p.Game } }
    if ($r -eq 'guest') { [IO.File]::WriteAllText($p.ClientHost, $identity.SteamId64 + "`n", [Text.Encoding]::ASCII) }
    [IO.File]::WriteAllText($p.Marker, "$marker`n", [Text.Encoding]::ASCII)
    Write-Output "[$r] role marker set to $marker."
}

# ---------------------------------------------------------------- 3. lab.ini / coop.ini
foreach ($r in $roles) {
    $p = $instances[$r].Paths
    $s = $instances[$r].Settings
    $texts = [ordered]@{}
    $texts[$p.LabIni] = New-LabIniFromSettings -Settings $s
    $texts[$p.CoopIni] = New-CoopIniText -MasterUrl $s.MasterUrl -Character $s.Character -Offline:$s.Offline
    foreach ($file in $texts.Keys) {
        $text = $texts[$file]
        $existing = if (Test-Path -LiteralPath $file -PathType Leaf) { [IO.File]::ReadAllText($file) } else { $null }
        if ($null -ne $existing -and $existing -ceq $text) { Write-Output "[$r] $(Split-Path -Leaf $file) unchanged."; continue }
        if (!(Confirm-LabAction $file 'Write lab configuration')) { continue }
        if ($null -ne $existing -and !(Test-LabGeneratedText $existing)) {
            Copy-Item -LiteralPath $file -Destination "$file.user-$stamp"
            Write-Warning "[$r] kept your previous $(Split-Path -Leaf $file) as $file.user-$stamp"
        }
        $null = Write-TextFileIfChanged -Path $file -Text $text
        Write-Output "[$r] wrote $file"
    }
    # The link secret: written for -Lan (a non-loopback bind needs it), deleted otherwise.
    if (Test-Path -LiteralPath $p.LinkSecret) { Assert-NoReparsePoint -Path $p.LinkSecret -StopAt $p.Game }
    if ($linkSecret) {
        if (Confirm-LabAction $p.LinkSecret 'Write the -Lan link secret') {
            [IO.File]::WriteAllText($p.LinkSecret, $linkSecret + "`r`n", [Text.Encoding]::ASCII)
            Write-Output "[$r] wrote link-secret.txt (-Lan)"
        }
    } elseif (Test-Path -LiteralPath $p.LinkSecret -PathType Leaf) {
        if (Confirm-LabAction $p.LinkSecret 'Delete the link secret (no -Lan)') { Remove-Item -LiteralPath $p.LinkSecret -Force; Write-Output "[$r] deleted link-secret.txt (no -Lan)" }
    }
}

# ---------------------------------------------------------------- 4. saves
if (!$SkipSaves) {
    # Back-to-back preparations may occur within the same second. Keep each
    # read-only backup distinct rather than colliding with an earlier run.
    $backupDir = Join-Path $labRoot ('save-backups/' + $stamp + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8))
    if (Confirm-LabAction $identity.UserData "Back up (read-only) 287700 and 311340 to $backupDir") {
        $manifest = Backup-RealSaves -UserData $identity.UserData -Destination $backupDir
        Write-JsonFile -Path (Join-Path $labRoot 'latest-save-backup.json') -Value ([ordered]@{ backup_dir = $backupDir; created_utc = $manifest.created_utc })
        Write-Output "Real saves backed up: $(@($manifest.verified).Count) verified files -> $backupDir"
        foreach ($r in $roles) {
            $s = $instances[$r].Settings
            if (!(Confirm-LabAction $s.SaveDir 'Seed lab save copies')) { continue }
            $seed = Copy-SaveSeed -BackupDirectory $backupDir -SaveDir $s.SaveDir `
                -LocalSubdir ([string]$config.Saves.localSubdir) -RemoteSubdir ([string]$config.Saves.remoteSubdir) `
                -Reseed:$Reseed -ArchiveDirectory (Join-Path $labRoot ("save-archive/$stamp/$r"))
            if ($seed.Seeded) { Write-Output "[$r] seeded $($seed.Files) save copies into $($s.SaveDir)" }
            else { Write-Output "[$r] lab saves already seeded ($($s.SaveDir)); pass -Reseed to refresh them from the new backup." }
        }
    } else { Write-Output 'Skipping save seeding (no backup in -WhatIf mode).' }
}

# ---------------------------------------------------------------- 5. graphics (lab copies only)
if (!$SkipGraphics) {
    $mode = [string](Get-ConfigValue $config.Graphics 'windowMode' 'Windowed')
    $width = [int](Get-ConfigValue $config.Graphics 'width' 1280)
    $height = [int](Get-ConfigValue $config.Graphics 'height' 720)
    foreach ($r in $roles) {
        $s = $instances[$r].Settings
        $file = Join-Path (Join-Path $s.SaveDir ([string]$config.Saves.localSubdir)) 'TPP_GRAPHICS_CONFIG'
        if (!(Test-PathInside -Child $file -Parent $s.SaveDir) -or (Test-PathInside -Child $file -Parent $identity.UserData)) { throw 'Internal error: graphics path outside the lab save copy.' }
        $manual = "[$r] Set $mode $($width)x$($height) once in this instance's OPTIONS > GRAPHICS (it only changes the lab copy)."
        if (!(Test-Path -LiteralPath $file -PathType Leaf)) { Write-Output "[$r] no lab TPP_GRAPHICS_CONFIG yet. $manual"; continue }
        $text = [IO.File]::ReadAllText($file)
        $info = Get-GraphicsConfigInfo -Text $text
        if ($info.Valid -and $info.WindowMode -ceq $mode -and $info.Width -eq $width -and $info.Height -eq $height) { Write-Output "[$r] lab graphics already $mode $($width)x$($height)."; continue }
        try { $edited = Set-GraphicsConfigWindowedText -Text $text -Width $width -Height $height -WindowMode $mode }
        catch { Write-Warning "[$r] $($_.Exception.Message) $manual"; continue }
        if (!(Confirm-LabAction $file "Set lab copy to $mode $($width)x$($height) (tag recomputed)")) { continue }
        $original = "$file.lab-original"
        if (!(Test-Path -LiteralPath $original)) { Copy-Item -LiteralPath $file -Destination $original }
        [IO.File]::WriteAllText($file, $edited, [Text.UTF8Encoding]::new($false))
        Write-Output "[$r] lab graphics set to $mode $($width)x$($height) (was $($info.WindowMode) $($info.Width)x$($info.Height))."
    }
}

Write-Output ''
if ($WhatIfPreference) { Write-Output 'WhatIf: nothing was changed.'; return }
if ($Offline) {
    $url = $instances[$roles[0]].Settings.MasterUrl
    $startArgs = if ($Lan) { '-Offline -Lan' } else { '-Offline' }
    Write-Output "Prepared for the OFFLINE lab mode (offline-v1, no fuse). Next: pwsh -File tools/lab/Start-Lab.ps1 $startArgs, then tools/lab/Test-PartnerSpike.ps1 -Experiment E0."
    if ($WanSim -cne 'off') { Write-Output "WAN rehearsal preset '$WanSim' (lab.json wanSim) is in both lab.ini files." }
    Write-Output "The folders now point the master server at a closed local port ($url, coop.ini); run this script with -Disable before normal online play."
    return
}
Write-Output 'Prepared. Next: pwsh -File tools/lab/Start-Lab.ps1, then tools/lab/Invoke-LabSmoke.ps1 -Assist.'
Write-Output 'The host folder now redirects the master server to the local fuse (coop.ini); run this script with -Disable before normal online play.'
