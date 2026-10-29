#Requires -Version 7
# Shared copy-only source snapshot policy for audited checks and release packaging.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-WorkerSourceHashes([string]$Root) {
    $paths = [ordered]@{}
    $paths['CMakeLists.txt'] = Join-Path $Root 'CMakeLists.txt'
    $minhookPath = Join-Path $Root 'third_party\minhook'
    $provenance = [ordered]@{}
    foreach ($folder in @('cmake', 'native', 'lua', 'tests', 'tools', 'third_party', 'work\lua-5.1.5\src')) {
        $path = Join-Path $Root $folder
        if (!(Test-Path -LiteralPath $path -PathType Container)) { continue }
        if ((Get-Item -LiteralPath $path -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw "Source snapshot refuses an unreviewed directory link: $path"
        }
        foreach ($directory in Get-ChildItem -LiteralPath $path -Recurse -Directory -Force) {
            if (($directory.Attributes -band [IO.FileAttributes]::ReparsePoint) -and $directory.FullName -ine $minhookPath) {
                throw "Source snapshot refuses an unreviewed directory link: $($directory.FullName)"
            }
        }
        foreach ($file in Get-ChildItem -LiteralPath $path -Recurse -File) {
            # The launcher's embedded assets (controller FPKDs, icon, logo, manifest) are compiled into the exe;
            # the native .inc fragments are #included into the loader's sources.
            if ($file.Extension -in @('.cpp','.hpp','.h','.c','.inc','.lua','.ps1','.psm1','.cmake','.in','.def','.rc','.bin','.ico','.png','.manifest') -or $file.Name -eq 'CMakeLists.txt') {
                if ($file.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Source snapshot refuses links: $($file.FullName)" }
                $paths[[IO.Path]::GetRelativePath($Root, $file.FullName)] = $file.FullName
            }
        }
    }
    # A known MinHook directory link may be skipped by ordinary recursion.
    # Resolve it explicitly, preserve it, and hash exactly the dependency tree
    # that add_subdirectory(third_party/minhook) compiles. Unknown links refuse.
    if (!(Test-Path -LiteralPath $minhookPath -PathType Container)) { throw 'MinHook source dependency is missing' }
    $minhook = Get-Item -LiteralPath $minhookPath -Force
    $linked = [bool]($minhook.Attributes -band [IO.FileAttributes]::ReparsePoint)
    $target = if ($linked) { $minhook.ResolveLinkTarget($true) } else { $minhook }
    if (!$target -or !($target.Attributes -band [IO.FileAttributes]::Directory) -or
        ($target.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'MinHook directory link did not resolve to an ordinary source directory' }
    $provenance['third_party\minhook'] = [ordered]@{
        logical_path=$minhookPath; resolved_path=$target.FullName; linked=$linked
        link_type=$(if ($linked) { [string]$minhook.LinkType } else { 'ordinary_directory' })
        link_target=$(if ($linked) { @($minhook.Target) } else { @() })
    }
    foreach ($directory in Get-ChildItem -LiteralPath $target.FullName -Recurse -Directory -Force) {
        if ($directory.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Nested MinHook directory link is unreviewed: $($directory.FullName)" }
    }
    foreach ($file in Get-ChildItem -LiteralPath $target.FullName -Recurse -File) {
        if ($file.Extension -in @('.cpp','.hpp','.h','.c','.cmake','.in','.def','.rc') -or $file.Name -eq 'CMakeLists.txt' -or $file.Name -like 'LICENSE*') {
            if ($file.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Nested MinHook file link is unreviewed: $($file.FullName)" }
            $paths['third_party\minhook\' + [IO.Path]::GetRelativePath($target.FullName, $file.FullName)] = $file.FullName
        }
    }
    $hashes = [ordered]@{}
    foreach ($relative in $paths.Keys | Sort-Object) {
        $hashes[$relative] = (Get-FileHash -LiteralPath $paths[$relative] -Algorithm SHA256).Hash
    }
    return [pscustomobject]@{ hashes=$hashes; dependencies=$provenance }
}
function Assert-WorkerSourceUnchanged($Before, $After) {
    if (($After.dependencies | ConvertTo-Json -Depth 8 -Compress) -cne ($Before.dependencies | ConvertTo-Json -Depth 8 -Compress)) {
        throw 'Source dependency target changed during worker checks'
    }
    if ($After.hashes.Count -ne $Before.hashes.Count) { throw 'Source file set changed during worker checks' }
    foreach ($relative in @($Before.hashes.Keys)) {
        if (!$After.hashes.Contains($relative) -or $After.hashes[$relative] -cne $Before.hashes[$relative]) {
            throw "Source changed during worker checks: $relative"
        }
    }
}
function Assert-WorkerReceiptSource([Collections.IDictionary]$Receipt, $Current) {
    if (!$Receipt.Contains('source_hashes') -or $Receipt.source_hashes -isnot [Collections.IDictionary] -or
        !$Receipt.source_hashes.Count -or !$Receipt.Contains('source_dependencies') -or
        $Receipt.source_dependencies -isnot [Collections.IDictionary] -or !$Receipt.source_dependencies.Count) {
        throw 'Release receipt has no complete source snapshot; rerun Release checks.'
    }
    foreach ($relative in $Receipt.source_hashes.Keys) {
        if ($relative -isnot [string] -or [string]$Receipt.source_hashes[$relative] -cnotmatch '^[A-Fa-f0-9]{64}$') {
            throw 'Release receipt has an invalid source snapshot; rerun Release checks.'
        }
    }
    $before = [pscustomobject]@{ hashes=$Receipt.source_hashes; dependencies=$Receipt.source_dependencies }
    try { Assert-WorkerSourceUnchanged $before $Current }
    catch { throw "Release source does not match check receipt; rerun Release checks. $($_.Exception.Message)" }
}
Export-ModuleMember -Function Get-WorkerSourceHashes, Assert-WorkerSourceUnchanged, Assert-WorkerReceiptSource
