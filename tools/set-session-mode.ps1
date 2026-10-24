param(
    [Parameter(Mandatory)][ValidateSet('Host','Join','Diagnostic')][string]$Role,
    [string]$HostSteamId,
    # Any owned installation folder, for example the single-PC lab guest copy
    # D:/Games/steamapps/common/MGS_TPP_guest. Its probe-install.json must name it.
    [string]$GameDirectory='D:/Games/steamapps/common/MGS_TPP'
)
$ErrorActionPreference='Stop'
# Validate the identity before touching the filesystem. PowerShell reads the
# literal 0xffffffff as Int32 -1, so the account part is taken with an unsigned
# modulus (account 0 is not an individual account).
if($Role -eq 'Join') {
    [UInt64]$identity=0
    if($HostSteamId -cnotmatch '^[0-9]{17}$' -or ![UInt64]::TryParse($HostSteamId,[ref]$identity) -or
       ($identity -shr 32) -ne 0x01100001 -or ($identity % [UInt64]4294967296) -eq 0) {
        throw 'Supply the host player''s 17-digit SteamID64 (a public individual desktop account).'
    }
} elseif($HostSteamId) { throw 'HostSteamId is used only with -Role Join.' }
if((Get-Item -LiteralPath $GameDirectory).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Use the direct MGSV folder, not a filesystem link.' }
$gameRoot=(Resolve-Path -LiteralPath $GameDirectory).Path.TrimEnd('\','/')
# Only an MGSV process started from THIS folder blocks the change (matched by
# executable path), so one lab instance can be configured while the other runs.
foreach($process in @(Get-Process -Name mgsvtpp,mgsvmgo -ErrorAction SilentlyContinue)) {
    $path=$null
    try { $path=$process.Path } catch { $path=$null }
    if(!$path) { try { $path=(Get-CimInstance Win32_Process -Filter "ProcessId=$($process.Id)" -ErrorAction Stop).ExecutablePath } catch { $path=$null } }
    # A process that exited after the enumeration has no folder; any other unknown
    # location is treated as this folder.
    if(!$path) { $exited=$false; try { $exited=$process.HasExited } catch { $exited=$false }; if($exited) { continue } }
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
$manifest=Get-Content -LiteralPath (Join-Path $modRoot 'probe-install.json') -Raw -Encoding UTF8 | ConvertFrom-Json
if($manifest.product -ne 'mgs-coop-original-loader-probe' -or $manifest.game_directory -ne $gameRoot) { throw 'Installation record does not match this mod and directory.' }
if($manifest.version -notin @('0.10.27','0.10.28','0.10.29','0.10.30','0.10.31','0.10.32','0.10.33','0.11.0','0.11.1','0.12.0','0.12.1','0.12.2','0.12.3','0.12.4','0.12.5','0.12.6','0.13.0','0.13.1','0.13.2','0.13.3','0.13.4','0.13.5','0.13.6','0.14.0','0.14.1','0.14.2')) { throw 'This command requires a supported 0.10.27 through 0.12.5 loader. The installed loader has not been changed.' }
if((Get-FileHash -LiteralPath (Join-Path $gameRoot 'dinput8.dll')).Hash -ne $manifest.dll_sha256) { throw 'Installed loader differs from its ownership record.' }
if((Get-FileHash -LiteralPath (Join-Path $modRoot 'mgs-coop-player-runtime.dat')).Hash -ne '87BBE82BB1E907CF405200155DA2794BF7E1F5A3D45BF66426A38CDD31A91F96') { throw 'Expected two-player archive is missing or changed.' }
# Write the identity before enabling join mode; no account IDs are put in logs.
$marker=Join-Path $modRoot 'script-observe.enabled'
if($Role -eq 'Join') {
    [IO.File]::WriteAllText((Join-Path $modRoot 'client-host.txt'),$HostSteamId+"`n",[Text.Encoding]::ASCII)
    [IO.File]::WriteAllText($marker,"client-v1`n",[Text.Encoding]::ASCII)
    Write-Output 'Join mode selected. Load Mother Base, wait for the host to confirm readiness, then press Ctrl+F8 in MGSV. One attempt per restart; connection is unverified.'
} elseif($Role -eq 'Diagnostic') {
    [IO.File]::WriteAllText($marker,"client-check-v1`n",[Text.Encoding]::ASCII)
    if($manifest.version -in @('0.10.31','0.10.32','0.10.33','0.11.0','0.11.1','0.12.0','0.12.1','0.12.2','0.12.3','0.12.4','0.12.5','0.12.6','0.13.0','0.13.1','0.13.2','0.13.3','0.13.4','0.13.5','0.13.6','0.14.0','0.14.1','0.14.2')) {
        Write-Output 'Diagnostic mode selected. Load Mother Base, then press Ctrl+F8 to check guest input and native prerequisites. No peer connection or fake player is created.'
    } else {
        Write-Output 'Diagnostic loading mode selected. Prepares the two-slot package and checks prerequisites automatically. No peer connection or fake player is created.'
    }
} else {
    [IO.File]::WriteAllText($marker,"host-v3`n",[Text.Encoding]::ASCII)
    Write-Output 'Host mode selected. Load Mother Base before the guest presses Ctrl+F8 to join.'
}
