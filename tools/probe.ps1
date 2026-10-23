param(
    [ValidateSet('Status','Install','Remove')][string]$Action = 'Status',
    [string]$GameDirectory = 'D:\Games\steamapps\common\MGS_TPP'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$gameRoot = (Resolve-Path -LiteralPath $GameDirectory).Path
if ($Action -eq 'Install' -and !(Test-Path -LiteralPath (Join-Path $gameRoot 'mgsvtpp.exe') -PathType Leaf)) { throw 'MGSV executable not found' }
if ((Get-Item -LiteralPath $gameRoot).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Use the direct game directory, not a filesystem link' }
$targetDll = Join-Path $gameRoot 'dinput8.dll'
$probeDirectory = Join-Path $gameRoot 'mgs-coop'
$manifestPath = Join-Path $probeDirectory 'probe-install.json'
if ((Test-Path -LiteralPath $probeDirectory) -and ((Get-Item -LiteralPath $probeDirectory).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Probe directory must not be a filesystem link' }
if ($Action -eq 'Status') {
    [pscustomobject]@{ Game = $gameRoot; ProxyPresent = Test-Path -LiteralPath $targetDll; InstallationRecord = Test-Path -LiteralPath $manifestPath; Log = Join-Path $probeDirectory 'probe.log' }
    return
}
if (Get-Process mgsvtpp,mgsvmgo -ErrorAction SilentlyContinue) { throw 'Close MGSV before changing the loader probe' }
if ((Split-Path -Parent $targetDll) -ne $gameRoot) { throw 'Unexpected target path' }
if ($Action -eq 'Install') {
    $sourceDll = Join-Path $projectRoot 'build/native/dinput8.dll'
    if (!(Test-Path -LiteralPath $sourceDll -PathType Leaf)) { throw 'Build the project first' }
    if ((Test-Path -LiteralPath $targetDll) -or (Test-Path -LiteralPath $manifestPath)) { throw 'An existing loader or installation record is present; nothing was replaced' }
    $sourceHash = (Get-FileHash -LiteralPath $sourceDll -Algorithm SHA256).Hash
    $record = [ordered]@{
        product = 'mgs-coop-original-loader-probe'; version = '0.13.6'; game_directory = $gameRoot;
        dll_sha256 = $sourceHash; executable_sha256 = (Get-FileHash -LiteralPath (Join-Path $gameRoot 'mgsvtpp.exe') -Algorithm SHA256).Hash;
        installed_utc = [DateTime]::UtcNow.ToString('o'); scope = 'Diagnostics and exact-source Lua experiments; host-v2 creates an experimental native host; host-v3 additionally opens admission after mission startup and closes it before termination; client-v1 requests native joining after mission startup using client-host.txt; no verified playable co-op'
    }
    New-Item -ItemType Directory -Path $probeDirectory -Force | Out-Null
    $stagedRecord = Join-Path $probeDirectory ("probe-install." + [Guid]::NewGuid().ToString('N') + '.tmp')
    $copiedByThisAttempt = $false
    try {
        $record | ConvertTo-Json | Set-Content -LiteralPath $stagedRecord -Encoding utf8
        [IO.File]::Copy($sourceDll, $targetDll, $false)
        $copiedByThisAttempt = $true
        if ((Get-FileHash -LiteralPath $targetDll -Algorithm SHA256).Hash -ne $sourceHash) { throw 'Installed hash mismatch' }
        [IO.File]::Move($stagedRecord, $manifestPath)
    } catch {
        if ($copiedByThisAttempt -and (Test-Path -LiteralPath $targetDll) -and (Get-FileHash -LiteralPath $targetDll -Algorithm SHA256).Hash -eq $sourceHash) { Remove-Item -LiteralPath $targetDll }
        if (Test-Path -LiteralPath $stagedRecord) { Remove-Item -LiteralPath $stagedRecord }
        throw
    }
    Write-Output 'Original diagnostic probe installed. This is not a playable co-op mod.'
    return
}
if (!(Test-Path -LiteralPath $manifestPath -PathType Leaf)) { throw 'No owned installation record; refusing to remove an unknown loader' }
$record = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($record.product -ne 'mgs-coop-original-loader-probe' -or $record.game_directory -ne $gameRoot) { throw 'Installation record does not match this probe and directory' }
if (Test-Path -LiteralPath $targetDll) {
    if ((Get-FileHash -LiteralPath $targetDll -Algorithm SHA256).Hash -ne $record.dll_sha256) { throw 'The loader has changed; refusing to remove it' }
    Remove-Item -LiteralPath $targetDll
}
Remove-Item -LiteralPath $manifestPath
Write-Output 'Owned diagnostic loader removed. Diagnostic logs are retained.'
