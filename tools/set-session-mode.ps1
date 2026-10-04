param(
    [Parameter(Mandatory)][ValidateSet('Host','Join','Diagnostic')][string]$Role,
    [string]$HostSteamId,
    # Any owned installation folder, for example the single-PC lab guest copy
    # D:/Games/steamapps/common/MGS_TPP_guest. Its probe-install.json must name it.
    [string]$GameDirectory='D:/Games/steamapps/common/MGS_TPP'
)
$ErrorActionPreference='Stop'
if((Get-Item -LiteralPath $GameDirectory).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Use the direct MGSV folder, not a filesystem link.' }
$gameRoot=(Resolve-Path -LiteralPath $GameDirectory).Path.TrimEnd('\','/')
# Only an MGSV process started from THIS folder blocks the change (matched by
# executable path), so one lab instance can be configured while the other runs.
foreach($process in @(Get-Process -Name mgsvtpp,mgsvmgo -ErrorAction SilentlyContinue)) {
    $path=$null
    try { $path=$process.Path } catch { $path=$null }
    if(!$path) { try { $path=(Get-CimInstance Win32_Process -Filter "ProcessId=$($process.Id)" -ErrorAction Stop).ExecutablePath } catch { $path=$null } }
    # An unknown location is treated as this folder.
    if(!$path -or (Split-Path -Parent ([IO.Path]::GetFullPath($path))).TrimEnd('\','/').Equals($gameRoot,[StringComparison]::OrdinalIgnoreCase)) {
        throw 'Close MGSV running from this folder normally before changing the session mode.'
    }
}
$modRoot=Join-Path $gameRoot 'mgs-coop'
foreach($path in @($modRoot,(Join-Path $modRoot 'script-observe.enabled'),(Join-Path $modRoot 'client-host.txt'))) {
    if((Test-Path -LiteralPath $path) -and ((Get-Item -LiteralPath $path).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw 'Session configuration must not use filesystem links.'
    }
}
$manifest=Get-Content -LiteralPath (Join-Path $modRoot 'probe-install.json') -Raw | ConvertFrom-Json
if($manifest.product -ne 'mgs-coop-original-loader-probe' -or $manifest.game_directory -ne $gameRoot) { throw 'Installation record does not match this mod and directory.' }
if($manifest.version -notin @('0.10.27','0.10.28','0.10.29','0.10.30','0.10.31','0.10.32','0.10.33')) { throw 'This command requires a supported 0.10.27 through 0.10.33 loader. The installed loader has not been changed.' }
if((Get-FileHash -LiteralPath (Join-Path $gameRoot 'dinput8.dll')).Hash -ne $manifest.dll_sha256) { throw 'Installed loader differs from its ownership record.' }
if((Get-FileHash -LiteralPath (Join-Path $modRoot 'mgs-coop-player-runtime.dat')).Hash -ne '26CE7C2168D655C35BE39E4F17A295206AA0522DE8782CD57A36C4FEEA382F54') { throw 'Expected two-player archive is missing or changed.' }
if($Role -eq 'Join') {
    [UInt64]$identity=0
    if($HostSteamId -cnotmatch '^[0-9]{17}$' -or ![UInt64]::TryParse($HostSteamId,[ref]$identity) -or
       ($identity -shr 32) -ne 0x01100001 -or ($identity -band 0xffffffff) -eq 0) {
        throw 'Supply the host player''s 17-digit SteamID64 (a public individual desktop account).'
    }
} elseif($HostSteamId) { throw 'HostSteamId is used only with -Role Join.' }
# Write the identity before enabling join mode; no account IDs are put in logs.
$marker=Join-Path $modRoot 'script-observe.enabled'
if($Role -eq 'Join') {
    [IO.File]::WriteAllText((Join-Path $modRoot 'client-host.txt'),$HostSteamId+"`n",[Text.Encoding]::ASCII)
    [IO.File]::WriteAllText($marker,"client-v1`n",[Text.Encoding]::ASCII)
    Write-Output 'Join mode selected. Load Mother Base, wait for the host to confirm readiness, then press Ctrl+F8 in MGSV. One attempt per restart; connection is unverified.'
} elseif($Role -eq 'Diagnostic') {
    [IO.File]::WriteAllText($marker,"client-check-v1`n",[Text.Encoding]::ASCII)
    if($manifest.version -in @('0.10.31','0.10.32','0.10.33')) {
        Write-Output 'Diagnostic mode selected. Load Mother Base, then press Ctrl+F8 to check guest input and native prerequisites. No peer connection or fake player is created.'
    } else {
        Write-Output 'Diagnostic loading mode selected. Prepares the two-slot package and checks prerequisites automatically. No peer connection or fake player is created.'
    }
} else {
    [IO.File]::WriteAllText($marker,"host-v3`n",[Text.Encoding]::ASCII)
    Write-Output 'Host mode selected. Load Mother Base before the guest presses Ctrl+F8 to join.'
}
