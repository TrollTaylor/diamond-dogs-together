#Requires -Version 5.1
<#
.SYNOPSIS
Prepare pinned source-only build dependencies without touching MGSV.
.DESCRIPTION
Initializes the exact MinHook submodule and fetches the verified Lua 5.1.5
source used only by fixture tests. Existing sources are never overwritten.
No retail assets, loader installation, build, game or desktop input is involved.
#>
[CmdletBinding()]
param(
    [ValidateNotNullOrEmpty()][string]$ProjectRoot = (Split-Path -Parent $PSScriptRoot),
    [ValidateNotNullOrEmpty()][string]$LuaArchivePath
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$taskLuaUri = 'https://www.lua.org/ftp/lua-5.1.5.tar.gz'
$taskLuaSha256 = '2640FC56A795F29D28EF15E13C34A47E223960B0240E8CB0A82D9B0738695333'
$taskMinHookRevision = 'c3fcafdc10146beb5919319d0683e44e3c30d537'

function Resolve-DependencyApplication {
    [CmdletBinding()]
    param([Parameter(Mandatory=$true)][ValidateNotNullOrEmpty()][string]$Name)
    # Get-Command can return the same executable name from multiple PATH entries.
    # Keep PATH precedence and return one literal executable path for the call operator.
    $command = Get-Command -Name $Name -CommandType Application -ErrorAction Stop | Select-Object -First 1
    if (-not $command -or -not $command.PSObject.Properties['Source'] -or [string]::IsNullOrWhiteSpace([string]$command.Source)) {
        throw "No executable application was found for dependency tool: $Name"
    }
    return [string]$command.Source
}

function Assert-DescendantPath {
    param([string]$Root, [string]$Path)
    $rootFull = [IO.Path]::GetFullPath($Root).TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
    $pathFull = [IO.Path]::GetFullPath($Path)
    if (-not $pathFull.StartsWith($rootFull + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Dependency path is outside the intended directory: $pathFull"
    }
    return $pathFull
}

function Assert-NoReparsePath {
    param([string]$Root, [string]$Path)
    $rootFull = [IO.Path]::GetFullPath($Root).TrimEnd([char[]]"\/")
    $pathFull = Assert-DescendantPath -Root $rootFull -Path $Path
    $current = $pathFull
    while ($current -and ($current -eq $rootFull -or $current.StartsWith($rootFull + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase))) {
        if (Test-Path -LiteralPath $current) {
            $item = Get-Item -LiteralPath $current -Force
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "Dependency path has a link or reparse point: $current"
            }
        }
        if ($current -eq $rootFull) { break }
        $current = Split-Path -Parent $current
    }
}

function Assert-LuaTarListing {
    param([string[]]$Names, [string[]]$Details)
    if (-not $Names -or -not $Details -or $Names.Count -ne $Details.Count) {
        throw 'Lua archive listing is empty or inconsistent.'
    }
    foreach ($name in $Names) {
        if (-not $name -or $name.Contains('\') -or $name.Contains(':') -or $name.StartsWith('/') -or $name -match '[\x00-\x1f]') {
            throw 'Lua archive contains an unsafe path.'
        }
        $parts = @($name.TrimEnd('/').Split('/'))
        if ($parts[0] -cne 'lua-5.1.5' -or @($parts | Where-Object { $_ -eq '..' -or $_ -eq '.' -or $_ -eq '' }).Count -ne 0) {
            throw 'Lua archive contains a path outside its source directory.'
        }
    }
    foreach ($detail in $Details) {
        if ($detail -notmatch '^[d-][rwxstST-]{9}\s') {
            throw 'Lua archive contains a link, special file, or unrecognized entry.'
        }
    }
}

function Assert-LuaSourceTree {
    param([string]$Root)
    $header = Join-Path $Root 'src\lua.h'
    if (-not (Test-Path -LiteralPath $header -PathType Leaf) -or
        -not (Select-String -LiteralPath $header -Pattern '^#define\s+LUA_RELEASE\s+"Lua 5\.1\.5"' -Quiet)) {
        throw 'The extracted dependency is not Lua 5.1.5.'
    }
    $required = @('lapi.c','lauxlib.c','lbaselib.c','lcode.c','ldblib.c','ldebug.c','ldo.c','ldump.c',
        'lfunc.c','lgc.c','linit.c','liolib.c','llex.c','lmathlib.c','lmem.c','loadlib.c','lobject.c',
        'lopcodes.c','lparser.c','lstate.c','lstring.c','lstrlib.c','ltable.c','ltablib.c','ltm.c',
        'lua.c','lundump.c','lvm.c','lzio.c')
    foreach ($name in $required) {
        if (-not (Test-Path -LiteralPath (Join-Path $Root ('src\' + $name)) -PathType Leaf)) {
            throw "Lua 5.1.5 source is incomplete: $name"
        }
    }
    foreach ($item in @(Get-ChildItem -LiteralPath $Root -Force -Recurse)) {
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw 'Lua source contains a link or reparse point.'
        }
    }
}

$ProjectRoot = [IO.Path]::GetFullPath($ProjectRoot).TrimEnd([char[]]"\/")
foreach ($relative in @('CMakeLists.txt','.gitmodules','third_party\lua-fixture\provenance.json')) {
    if (-not (Test-Path -LiteralPath (Join-Path $ProjectRoot $relative) -PathType Leaf)) {
        throw "Run this from a complete project source checkout; missing $relative"
    }
}
$git = Resolve-DependencyApplication -Name 'git'
$tar = Resolve-DependencyApplication -Name 'tar'
$topLevel = @(& $git -C $ProjectRoot rev-parse --show-toplevel 2>$null)
if ($LASTEXITCODE -ne 0 -or $topLevel.Count -ne 1 -or [IO.Path]::GetFullPath($topLevel[0]) -ne $ProjectRoot) {
    throw 'MinHook setup requires the project Git checkout, not an exported source folder.'
}
$gitLink = @(& $git -C $ProjectRoot ls-files --stage -- third_party/minhook)
if ($LASTEXITCODE -ne 0 -or $gitLink.Count -ne 1 -or $gitLink[0] -notmatch ('^160000 ' + $taskMinHookRevision + ' 0\s+third_party/minhook$')) {
    throw 'The project does not pin the expected MinHook revision; source setup refused.'
}
$minHookRoot = Join-Path $ProjectRoot 'third_party\minhook'
Assert-NoReparsePath -Root $ProjectRoot -Path $minHookRoot
$minHookInitialized = Test-Path -LiteralPath (Join-Path $minHookRoot '.git')
if ($minHookInitialized) {
    $existingRevision = @(& $git -C $minHookRoot rev-parse HEAD)
    if ($LASTEXITCODE -ne 0 -or $existingRevision.Count -ne 1 -or $existingRevision[0] -cne $taskMinHookRevision) {
        throw 'Existing MinHook is at a different revision; it will not be replaced.'
    }
    $dirty = @(& $git -C $minHookRoot status --porcelain)
    if ($LASTEXITCODE -ne 0 -or $dirty.Count -ne 0) {
        throw 'Existing MinHook contains local changes; they will not be overwritten.'
    }
} elseif ((Test-Path -LiteralPath $minHookRoot) -and @(Get-ChildItem -LiteralPath $minHookRoot -Force).Count -ne 0) {
    throw 'An unregistered MinHook source directory already exists; it will not be overwritten.'
}
$provenance = Get-Content -LiteralPath (Join-Path $ProjectRoot 'third_party\lua-fixture\provenance.json') -Raw | ConvertFrom-Json
if ($provenance.source -cne $taskLuaUri -or $provenance.archive_sha256 -ine $taskLuaSha256) {
    throw 'Lua source provenance does not match the pinned official archive.'
}
$workRoot = Join-Path $ProjectRoot 'work'
$luaRoot = Assert-DescendantPath -Root $workRoot -Path (Join-Path $workRoot 'lua-5.1.5')
Assert-NoReparsePath -Root $ProjectRoot -Path $workRoot
Assert-NoReparsePath -Root $ProjectRoot -Path $luaRoot
if ((Test-Path -LiteralPath $luaRoot) -and -not (Test-Path -LiteralPath $luaRoot -PathType Container)) {
    throw 'Lua source destination already exists as a file; it will not be replaced.'
}
New-Item -ItemType Directory -Path $workRoot -Force | Out-Null
$taskTempRoot = Assert-DescendantPath -Root $workRoot -Path (Join-Path $workRoot ('.dependency-setup-' + [Guid]::NewGuid().ToString('N')))
New-Item -ItemType Directory -Path $taskTempRoot -ErrorAction Stop | Out-Null
try {
    $taskArchive = Join-Path $taskTempRoot 'lua-5.1.5.tar.gz'
    if ($LuaArchivePath) {
        $archiveInput = (Get-Item -LiteralPath $LuaArchivePath -ErrorAction Stop).FullName
        if ((Get-FileHash -LiteralPath $archiveInput -Algorithm SHA256).Hash -ine $taskLuaSha256) {
            throw 'The supplied Lua archive does not match the pinned SHA-256.'
        }
        Copy-Item -LiteralPath $archiveInput -Destination $taskArchive -ErrorAction Stop
    } else {
        Write-Host 'Downloading the pinned Lua 5.1.5 fixture source.'
        $taskPreviousTls = [Net.ServicePointManager]::SecurityProtocol
        try {
            [Net.ServicePointManager]::SecurityProtocol = $taskPreviousTls -bor [Net.SecurityProtocolType]::Tls12
            Invoke-WebRequest -Uri $taskLuaUri -OutFile $taskArchive -UseBasicParsing -ErrorAction Stop
        } finally {
            [Net.ServicePointManager]::SecurityProtocol = $taskPreviousTls
        }
    }
    if ((Get-FileHash -LiteralPath $taskArchive -Algorithm SHA256).Hash -ine $taskLuaSha256) {
        throw 'The downloaded Lua archive does not match the pinned SHA-256.'
    }
    $names = @(& $tar -tzf $taskArchive)
    if ($LASTEXITCODE -ne 0) { throw 'Unable to read the pinned Lua archive.' }
    $details = @(& $tar -tvzf $taskArchive)
    if ($LASTEXITCODE -ne 0) { throw 'Unable to inspect the pinned Lua archive entry types.' }
    Assert-LuaTarListing -Names $names -Details $details
    & $tar -xzf $taskArchive -C $taskTempRoot
    if ($LASTEXITCODE -ne 0) { throw 'Unable to extract the pinned Lua source.' }
    $extractedLua = Join-Path $taskTempRoot 'lua-5.1.5'
    Assert-LuaSourceTree -Root $extractedLua
    $taskExistingLuaValidated = $false
    if (Test-Path -LiteralPath $luaRoot) {
        Assert-LuaSourceTree -Root $luaRoot
        $sourcePrefix = $extractedLua.TrimEnd('\') + '\'
        foreach ($file in @(Get-ChildItem -LiteralPath $extractedLua -File -Recurse)) {
            $relative = $file.FullName.Substring($sourcePrefix.Length)
            $existing = Assert-DescendantPath -Root $luaRoot -Path (Join-Path $luaRoot $relative)
            Assert-NoReparsePath -Root $ProjectRoot -Path $existing
            if (-not (Test-Path -LiteralPath $existing -PathType Leaf) -or
                (Get-FileHash -LiteralPath $existing -Algorithm SHA256).Hash -ine (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash) {
                throw "Existing Lua source differs from the pinned archive; it will not be overwritten: $relative"
            }
        }
        $taskExistingLuaValidated = $true
    }
    if (-not $minHookInitialized) {
        Write-Host 'Initializing the pinned MinHook source submodule.'
        $taskPreviousErrorPreference = $ErrorActionPreference
        try {
            # Windows PowerShell 5.1 turns native progress on stderr into error records.
            $ErrorActionPreference = 'Continue'
            $taskGitOutput = @(& $git -C $ProjectRoot submodule update --init --recursive -- third_party/minhook 2>&1)
            $taskGitExit = $LASTEXITCODE
        } finally {
            $ErrorActionPreference = $taskPreviousErrorPreference
        }
        foreach ($line in $taskGitOutput) { Write-Host $line }
        if ($taskGitExit -ne 0) { throw 'MinHook source initialization failed.' }
        $revision = @(& $git -C $minHookRoot rev-parse HEAD)
        if ($LASTEXITCODE -ne 0 -or $revision.Count -ne 1 -or $revision[0] -cne $taskMinHookRevision) {
            throw 'The initialized MinHook source is not the pinned revision.'
        }
    }
    Assert-NoReparsePath -Root $ProjectRoot -Path $luaRoot
    if (Test-Path -LiteralPath $luaRoot) {
        if (-not $taskExistingLuaValidated) {
            throw 'Lua destination appeared during setup; it will not be replaced or assumed verified.'
        }
    } else {
        $extractedLua = Assert-DescendantPath -Root $workRoot -Path $extractedLua
        Assert-NoReparsePath -Root $ProjectRoot -Path $extractedLua
        # Directory.Move fails atomically if another source tree appears at the destination.
        [IO.Directory]::Move($extractedLua, $luaRoot)
    }
    Write-Host 'Source dependencies ready: MinHook v1.3.4 and verified Lua 5.1.5.'
    Write-Host 'Retail Lua composition tests still require exact scripts extracted locally from your own game.'
    Write-Host 'Without those game inputs, CMake reports their checks as skipped; fixture success does not verify gameplay.'
} finally {
    # Only remove this invocation's checked, private directory under project/work.
    $taskTempRoot = Assert-DescendantPath -Root $workRoot -Path $taskTempRoot
    Assert-NoReparsePath -Root $ProjectRoot -Path $taskTempRoot
    foreach ($item in @(Get-ChildItem -LiteralPath $taskTempRoot -Force -Recurse)) {
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Temporary dependency source contains a link; cleanup refused: $taskTempRoot"
        }
    }
    Remove-Item -LiteralPath $taskTempRoot -Recurse -Force -ErrorAction Stop
}
