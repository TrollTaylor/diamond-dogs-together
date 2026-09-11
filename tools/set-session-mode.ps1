param(
    [Parameter(Mandatory)][ValidateSet('Host','Join','Diagnostic')][string]$Role,
    [string]$HostSteamId,
    [string]$GameDirectory='D:/Games/steamapps/common/MGS_TPP'
)
$ErrorActionPreference='Stop'
$gameRoot=(Resolve-Path -LiteralPath $GameDirectory).Path
if(Get-Process mgsvtpp,mgsvmgo -ErrorAction SilentlyContinue) { throw 'Close MGSV normally before changing the session mode.' }
$modRoot=Join-Path $gameRoot 'mgs-coop'
$manifest=Get-Content -LiteralPath (Join-Path $modRoot 'probe-install.json') -Raw | ConvertFrom-Json
if($manifest.version -ne '0.10.27') { throw 'This command requires the 0.10.27 loader. The installed loader has not been changed.' }
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
    Write-Output 'Diagnostic mode selected. Fox Engine local dual-actor memory and packages enabled without network calls.'
} else {
    [IO.File]::WriteAllText($marker,"host-v3`n",[Text.Encoding]::ASCII)
    Write-Output 'Host mode selected. Load Mother Base before the guest presses Ctrl+F8 to join.'
}
