param(
    [ValidateSet('Host','Join','Diagnostic')][string]$Role,
    [string]$HostSteamId,
    [string]$GameDirectory
)
$ErrorActionPreference='Stop'
if(!$GameDirectory) {
    $location=Join-Path $PSScriptRoot 'installed-location.json'
    if(Test-Path -LiteralPath $location -PathType Leaf) {
        $GameDirectory=(Get-Content -LiteralPath $location -Raw | ConvertFrom-Json).game_directory
    }
    if(!$GameDirectory) { $GameDirectory=Read-Host 'Installed MGSV folder containing mgsvtpp.exe' }
}
if(!$Role) {
    Write-Host '1: Host   2: Join   3: Diagnostic loading'
    switch(Read-Host 'Select role') {
        '1' { $Role='Host' }
        '2' { $Role='Join' }
        '3' { $Role='Diagnostic' }
        default { throw 'Select 1, 2 or 3. No configuration was changed.' }
    }
}
if($Role -eq 'Join' -and !$HostSteamId) { $HostSteamId=Read-Host "Host player's 17-digit SteamID64" }
# All validation and exact ASCII/LF marker writes happen in the shared command.
# This always targets the installed mod, not the extracted package directory.
& (Join-Path $PSScriptRoot 'set-session-mode.ps1') -Role $Role -HostSteamId $HostSteamId -GameDirectory $GameDirectory
