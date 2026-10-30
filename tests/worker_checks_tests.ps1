#Requires -Version 7
# Pure worker policy/receipt checks. No build, process launch, game or input APIs.
[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
Import-Module (Join-Path $repo 'tools\worker\WorkerSource.psm1') -Force -DisableNameChecking
$scriptPath = Join-Path $repo 'tools\worker\Invoke-WorkerChecks.ps1'
$tokens = $null; $errors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($scriptPath, [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw 'Worker runner parse failed' }
# Load actual pure boundaries, never the top-level execution pipeline.
$pure = @('Get-WorkerExperiments','Get-WorkerReleaseExperiments','Get-WorkerTests','Get-WorkerShardedTests','Get-WorkerShardNames','Get-WorkerShard','Get-WorkerReleaseOnlyTests','Get-WorkerHash','Test-WorkerUnder','Assert-WorkerPath',
    'Get-WorkerPlan','Assert-WorkerOptions','Assert-WorkerCache','Assert-WorkerCacheOwnership',
    'Assert-WorkerInventory','Assert-WorkerCoverage','Write-WorkerJson','New-WorkerCmakeOverride')
foreach ($name in $pure) {
    $definitions = @($ast.FindAll({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq $name }, $true))
    if ($definitions.Count -ne 1) { throw "Missing or duplicate pure boundary: $name" }
    . ([scriptblock]::Create($definitions[0].Extent.Text))
}
$checks = 0
function Check([bool]$Value, [string]$Message) { $script:checks++; if (!$Value) { throw "Assertion failed: $Message" } }
function Refuses([scriptblock]$Call, [string]$Pattern) {
    $caught = $null
    try { & $Call | Out-Null } catch { $caught = $_ }
    Check ($null -ne $caught -and $caught.Exception.Message -match $Pattern) "refuses $Pattern"
}
$nonce = [Guid]::NewGuid().ToString('N')
$buildRoot = Join-Path $repo 'build'
$fixture = Join-Path $buildRoot ('worker-checks-tests-' + $nonce)
$marker = Join-Path $fixture '.fixture-owner'
$ownedLinks = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
try {
    $null = New-Item -ItemType Directory -Path $fixture
    [IO.File]::WriteAllText($marker, $nonce)
    $normal = Get-WorkerPlan $fixture 'Normal' 'Release' @() $false ''
    $background = Get-WorkerPlan $fixture 'Normal' 'Release' @() $true ''
    $research = Get-WorkerPlan $fixture 'Research' 'Release' @('MGS_LOCAL_ACTION_EVENTS') $false ''
    Check ($normal.flags.Count -eq 19 -and @($normal.flags.Values | Where-Object { $_ -cne 'OFF' }).Count -eq 0) 'normal explicitly disables all 18 local experiments and the separate worker'
    Check ($background.flags.MGS_BACKGROUND_WORKER -ceq 'ON' -and @($background.flags.Keys | Where-Object { $_ -like 'MGS_LOCAL_*' -and $background.flags[$_] -cne 'OFF' }).Count -eq 0) 'background opt-in cannot enable research flags'
    Check ($research.flags.MGS_LOCAL_ACTION_EVENTS -ceq 'ON' -and @($research.flags.Values | Where-Object { $_ -ceq 'ON' }).Count -eq 1) 'research enables exactly the named option'
    $capacity = Get-WorkerPlan $fixture 'Research' 'Release' @('MGS_LOCAL_BUDDY_CAPACITY_TEST') $true ''
    Check ($capacity.flags.MGS_LOCAL_BUDDY_CAPACITY_TEST -ceq 'ON' -and $capacity.flags.MGS_BACKGROUND_WORKER -ceq 'ON' -and
        @($capacity.flags.Values | Where-Object { $_ -ceq 'ON' }).Count -eq 2) 'capacity has only explicit capacity and private worker enabled'
    Refuses { Get-WorkerPlan $fixture 'Research' 'Release' @('MGS_LOCAL_BUDDY_CAPACITY_TEST') $false '' } 'Research/background ON'
    Refuses { Get-WorkerPlan $fixture 'Research' 'Release' @('MGS_LOCAL_BUDDY_CAPACITY_TEST','MGS_LOCAL_GUN_PRELOAD_TEST') $true '' } 'alternate archive'
    $uniqueFlags=@('MGS_LOCAL_OFFLINE_EQUIPMENT_TEST','MGS_LOCAL_STOCK46_PRELOAD_TEST','MGS_LOCAL_STOCK46_WRITER_TEST','MGS_LOCAL_MGO_UNIQUE_REST_OBSERVER')
    $unique=Get-WorkerPlan $fixture 'Research' 'Release' $uniqueFlags $true ''
    Check ($unique.flags.MGS_LOCAL_MGO_UNIQUE_REST_OBSERVER -ceq 'ON' -and @($unique.flags.Values|Where-Object{$_ -ceq 'ON'}).Count -eq 5) 'unique observer enables only exact worker and four named flags'
    Refuses {Get-WorkerPlan $fixture 'Research' 'Release' @('MGS_LOCAL_MGO_UNIQUE_REST_OBSERVER') $true ''} 'exact Research/background'
    Refuses {Get-WorkerPlan $fixture 'Research' 'Release' $uniqueFlags $false ''} 'exact Research/background'
    Refuses {Get-WorkerPlan $fixture 'Research' 'Release' ($uniqueFlags+@('MGS_LOCAL_ACTION_EVENTS')) $true ''} 'exact Research/background'
    Refuses {Get-WorkerPlan $fixture 'Research' 'Release' @('MGS_LOCAL_MGO_UNIQUE_REST_BIND') $true ''} 'Unknown'
    $activationFlags=@('MGS_LOCAL_OFFLINE_EQUIPMENT_TEST','MGS_LOCAL_STOCK46_PRELOAD_TEST','MGS_LOCAL_STOCK46_WRITER_TEST','MGS_LOCAL_NATIVE_ACTIVATION_OBSERVER')
    $activation=Get-WorkerPlan $fixture 'Research' 'Release' $activationFlags $true ''
    Check ($activation.flags.MGS_LOCAL_NATIVE_ACTIVATION_OBSERVER -ceq 'ON' -and @($activation.flags.Values|Where-Object{$_ -ceq 'ON'}).Count -eq 5) 'native activation is explicitly selected with exact private gun base'
    Refuses {Get-WorkerPlan $fixture 'Research' 'Release' @('MGS_LOCAL_NATIVE_ACTIVATION_OBSERVER') $true ''} 'activation'
    Refuses {Get-WorkerPlan $fixture 'Research' 'Release' $activationFlags $false ''} 'activation'
    $standingFlags=@('MGS_LOCAL_OFFLINE_EQUIPMENT_TEST','MGS_LOCAL_STOCK46_PRELOAD_TEST','MGS_LOCAL_STOCK46_WRITER_TEST','MGS_LOCAL_MGO_STANDING_CYCLE_OBSERVER')
    $standing=Get-WorkerPlan $fixture 'Research' 'Release' $standingFlags $true ''
    Check ($standing.flags.MGS_LOCAL_MGO_STANDING_CYCLE_OBSERVER -ceq 'ON' -and $standing.flags.MGS_LOCAL_MGO_STANDING_LOOKUP_OBSERVER -ceq 'OFF' -and @($standing.flags.Values|Where-Object{$_ -ceq 'ON'}).Count -eq 5) 'standing bank profile keeps native lookup observer OFF'
    $lookup=Get-WorkerPlan $fixture 'Research' 'Release' ($standingFlags+@('MGS_LOCAL_MGO_STANDING_LOOKUP_OBSERVER')) $true ''
    Check ($lookup.flags.MGS_LOCAL_MGO_STANDING_LOOKUP_OBSERVER -ceq 'ON' -and @($lookup.flags.Values|Where-Object{$_ -ceq 'ON'}).Count -eq 6 -and $lookup.build_directory -cne $standing.build_directory) 'explicit lookup uses a distinct five-experiment private cache'
    Refuses {Get-WorkerPlan $fixture 'Research' 'Release' @('MGS_LOCAL_MGO_STANDING_LOOKUP_OBSERVER') $true ''} 'exact standing-cycle profile'
    Refuses {Get-WorkerPlan $fixture 'Research' 'Release' $standingFlags $false ''} 'exact Research/background'
    Refuses {Get-WorkerPlan $fixture 'Research' 'Release' ($standingFlags+@('MGS_LOCAL_MGO_UNIQUE_REST_OBSERVER')) $true ''} 'exact Research/background'

    Check ($normal.tests -ccontains 'worker_protocol_capacity_off_tests' -and $normal.tests -ccontains 'worker_protocol_capacity_on_tests') 'both capacity admission/refusal fixtures are audited'
    Check ($normal.build_directory -ine $background.build_directory -and $normal.build_directory -ine $research.build_directory) 'profiles use separate default caches'
    Check ($normal.coordinator_lock -ceq $background.coordinator_lock -and $normal.coordinator_lock -ceq $research.coordinator_lock) 'one checkout coordinator covers every cache/profile'
    $equivalent = Get-WorkerPlan ($fixture.ToUpperInvariant() + '\') 'Normal' 'Release' @() $false ''
    Check ($equivalent.coordinator_lock -ceq $normal.coordinator_lock) 'case/trailing slash cannot bypass the coordinator'
    $orderA = Get-WorkerPlan $fixture 'Research' 'Release' @('MGS_LOCAL_ACTION_EVENTS','MGS_LOCAL_UPPER_PROBE') $false ''
    $orderB = Get-WorkerPlan $fixture 'Research' 'Release' @('MGS_LOCAL_UPPER_PROBE','MGS_LOCAL_ACTION_EVENTS') $false ''
    Check ($orderA.build_directory -ceq $orderB.build_directory) 'explicit experiment order preserves profile identity'
    $custom = Get-WorkerPlan $fixture 'Normal' 'Release' @() $false ('build\space ' + [char]0x96EA)
    Check ($custom.build_directory -ceq (Join-Path $fixture ('build\space ' + [char]0x96EA))) 'spaced/non-ASCII cache remains a literal checkout-relative path'
    Check (!$normal.retail_game_launch -and !$normal.system_input -and !$normal.installation) 'plan makes no runtime/installation claim'
    foreach ($excluded in @('lab_scripts_tests','friend_package_tests','probe_install_recovery')) {
        Check ($normal.tests -cnotcontains $excluded -and $normal.excluded -ccontains $excluded) "unsafe default flow excluded: $excluded"
    }
    Check (@($normal.tests | Select-Object -Unique).Count -eq $normal.tests.Count) 'test allowlist contains no duplicates'
    Check ($normal.tests -ccontains 'worker_session_tests') 'audited detached controller fixture is explicitly included'
    Check ($normal.tests -ccontains 'steam_transport_tests') 'the Steam online fixture (fake Steam interfaces, no Steam call) is audited'
    Refuses { Get-WorkerPlan $fixture 'Normal' 'Release' @('MGS_LOCAL_ACTION_EVENTS') $false '' } 'Normal profile'
    Refuses { Get-WorkerPlan $fixture 'Research' 'Release' @() $false '' } 'requires explicit'
    Refuses { Get-WorkerPlan $fixture 'Research' 'Release' @('MGS_LOCAL_ACTION_EVENTS','MGS_LOCAL_ACTION_EVENTS') $false '' } 'Duplicate'
    Refuses { Get-WorkerPlan $fixture 'Research' 'Release' @('mgs_local_action_events') $false '' } 'noncanonical'
    Refuses { Get-WorkerPlan $fixture 'Research' 'Release' @('MGS_LOCAL_NEW_UNREVIEWED') $false '' } 'Unknown'
    Refuses { Get-WorkerPlan $fixture 'Normal' 'Release' @() $false 'build\native' } 'shared development'
    Refuses { Get-WorkerPlan $fixture 'Normal' 'Release' @() $false 'build\native\child' } 'shared development'
    Refuses { Get-WorkerPlan $fixture 'Normal' 'Release' @() $false 'build\worker-runs\receipt' } 'immutable run'
    Refuses { Get-WorkerPlan $fixture 'Normal' 'Release' @() $false '..\escape' } 'below'
    Refuses { Get-WorkerPlan $fixture 'Normal' 'Release' @() $false (Join-Path $fixture 'build') } 'below'
    # The friend/release loader (docs/two-pc-play.md "Release build"): exactly the stock46 gun pieces, never the worker.
    $release = Get-WorkerPlan $fixture 'Release' 'Release' @() $false ''
    Check ($release.flags.MGS_BACKGROUND_WORKER -ceq 'OFF' -and
        ((@($release.flags.Keys | Where-Object { $release.flags[$_] -ceq 'ON' }) | Sort-Object) -join ',') -ceq 'MGS_LOCAL_OFFLINE_EQUIPMENT_TEST,MGS_LOCAL_STOCK46_PRELOAD_TEST,MGS_LOCAL_STOCK46_WRITER_TEST') 'release: the three gun pieces, worker off'
    Check ($release.profile_key -ceq 'release-worker-off' -and $release.build_directory -ine $research.build_directory -and
        $release.build_directory -ine $normal.build_directory) 'release: its own cache'
    Refuses { Get-WorkerPlan $fixture 'Release' 'Release' @() $true '' } 'never enables the background worker'
    Refuses { Get-WorkerPlan $fixture 'Release' 'Release' @('MGS_LOCAL_ACTION_EVENTS') $false '' } 'fixed experiments'

    Assert-WorkerCacheOwnership $normal
    $ownerFile = Join-Path $normal.build_directory '.worker-check-profile.json'
    Check (Test-Path -LiteralPath $ownerFile -PathType Leaf) 'a fresh cache has an explicit profile owner'
    Assert-WorkerCacheOwnership $normal
    $wrongProfile = Get-WorkerPlan $fixture 'Research' 'Release' @('MGS_LOCAL_ACTION_EVENTS') $false $normal.build_directory
    Refuses { Assert-WorkerCacheOwnership $wrongProfile } 'different profile'
    $unowned = Get-WorkerPlan $fixture 'Normal' 'Release' @() $false 'build\unowned'
    $null = New-Item -ItemType Directory -Path $unowned.build_directory
    [IO.File]::WriteAllText((Join-Path $unowned.build_directory 'CMakeCache.txt'),'CMAKE_BUILD_TYPE:STRING=Release')
    Refuses { Assert-WorkerCacheOwnership $unowned } 'unowned existing'
    $cacheFile = Join-Path $normal.build_directory 'CMakeCache.txt'
    $cacheText = ($normal.flags.Keys | ForEach-Object { $_ + ':BOOL=' + $normal.flags[$_] }) -join "`n"
    [IO.File]::WriteAllText($cacheFile,$cacheText+"`n")
    Assert-WorkerCache $normal.build_directory $normal.flags
    [IO.File]::WriteAllText($cacheFile,$cacheText.Replace('MGS_BACKGROUND_WORKER:BOOL=OFF','MGS_BACKGROUND_WORKER:BOOL=ON')+"`n")
    Refuses { Assert-WorkerCache $normal.build_directory $normal.flags } 'Unexpected cached option'
    $localOptions = @(Get-WorkerExperiments | ForEach-Object { 'option(' + $_ + ' "fixture" OFF)' })
    $fixtureCmake = ($localOptions -join "`n") + "`noption(MGS_BACKGROUND_WORKER `"fixture`" OFF)`n"
    [IO.File]::WriteAllText((Join-Path $fixture 'CMakeLists.txt'),$fixtureCmake)
    Assert-WorkerOptions $fixture
    [IO.File]::AppendAllText((Join-Path $fixture 'CMakeLists.txt'),'option(MGS_LOCAL_EXTRA "new" OFF)'+"`n")
    Refuses { Assert-WorkerOptions $fixture } 'differs from CMake'

    $directory = $normal.build_directory
    $selected = @('partner_driver_tests','host_trial_patch_check','host_trial_lua_check')
    $inventory = [pscustomobject]@{tests=@(
        [pscustomobject]@{name='partner_driver_tests';command=@((Join-Path $directory 'partner_driver_tests.exe'));properties=@([pscustomobject]@{name='WORKING_DIRECTORY';value=$directory})},
        [pscustomobject]@{name='host_trial_patch_check';command=@((Join-Path $directory 'host_trial_patch_check.exe'),(Join-Path $fixture 'build\asset\package-00.lua'),(Join-Path $directory 'lua-check\TppMain-host-trial.lua'));properties=@([pscustomobject]@{name='WORKING_DIRECTORY';value=$directory},[pscustomobject]@{name='FIXTURES_SETUP';value=@('host_output')})},
        [pscustomobject]@{name='host_trial_lua_check';command=@((Join-Path $directory 'lua-fixture\lua51.exe'),(Join-Path $fixture 'tests\host_trial_lua_check.lua'));properties=@([pscustomobject]@{name='WORKING_DIRECTORY';value=$directory},[pscustomobject]@{name='FIXTURES_REQUIRED';value=@('host_output')})}
    )}
    function Copy-Inventory { return ($inventory | ConvertTo-Json -Depth 8 | ConvertFrom-Json) }
    Assert-WorkerInventory $inventory $fixture $directory $selected
    Check $true 'actual command/fixture policy accepts an owned setup-dependent Lua composition'
    $bad = Copy-Inventory; $bad.tests = @($bad.tests)+@($bad.tests[0])
    Refuses { Assert-WorkerInventory $bad $fixture $directory $selected } 'Duplicate CTest'
    $bad = Copy-Inventory; $bad.tests = @($bad.tests[0],$bad.tests[2])
    Refuses { Assert-WorkerInventory $bad $fixture $directory $selected } 'missing'
    $bad = Copy-Inventory; $bad.tests[0].command[0] = Join-Path $fixture 'mgsvtpp.exe'
    Refuses { Assert-WorkerInventory $bad $fixture $directory $selected } 'Unaudited fixture executable'
    $bad = Copy-Inventory; $bad.tests[0].command += '--new-mode'
    Refuses { Assert-WorkerInventory $bad $fixture $directory $selected } 'Unexpected fixture arguments'
    $bad = Copy-Inventory; $bad.tests[1].command[2] = Join-Path $fixture 'foreign-output.lua'
    Refuses { Assert-WorkerInventory $bad $fixture $directory $selected } 'Foreign patch output'
    $bad = Copy-Inventory; $bad.tests[2].command[1] = Join-Path $fixture 'tests\unreviewed.lua'
    Refuses { Assert-WorkerInventory $bad $fixture $directory $selected } 'Unaudited Lua command'
    $bad = Copy-Inventory; $bad.tests[0].properties[0].value = $fixture
    Refuses { Assert-WorkerInventory $bad $fixture $directory $selected } 'Foreign test working'
    $bad = Copy-Inventory; $bad.tests[0].properties += [pscustomobject]@{name='DISABLED';value=$true}
    Refuses { Assert-WorkerInventory $bad $fixture $directory $selected } 'disabled'
    $bad = Copy-Inventory; $bad.tests[0].properties += [pscustomobject]@{name='ENVIRONMENT';value='PATH=foreign'}
    Refuses { Assert-WorkerInventory $bad $fixture $directory $selected } 'Unaudited CTest property'
    $bad = Copy-Inventory; $bad.tests[2].properties += [pscustomobject]@{name='DEPENDS';value=@('host_trial_patch_check')}
    Assert-WorkerInventory $bad $fixture $directory $selected
    Check $true 'CTest ordering dependencies remain inside the audited closure'
    $bad.tests[2].properties[-1].value = @('unaudited_launch')
    Refuses { Assert-WorkerInventory $bad $fixture $directory $selected } 'Unaudited ordered dependency'
    $bad = Copy-Inventory; $bad.tests[1].name = 'unaudited_setup'
    Refuses { Assert-WorkerInventory $bad $fixture $directory @('partner_driver_tests','host_trial_lua_check') } 'Unaudited fixture dependency'
    $bad = Copy-Inventory; $bad.tests[1].properties = @([pscustomobject]@{name='WORKING_DIRECTORY';value=$directory})
    Refuses { Assert-WorkerInventory $bad $fixture $directory $selected } 'Missing fixture provider'
    $bad = Copy-Inventory
    $bad.tests += [pscustomobject]@{name='unaudited_cleanup';command=@('mgsvtpp.exe');properties=@([pscustomobject]@{name='FIXTURES_CLEANUP';value=@('host_output')})}
    Refuses { Assert-WorkerInventory $bad $fixture $directory $selected } 'Unaudited fixture dependency'

    # Detached controller fixtures share the exact six-argument PowerShell
    # contract. Names, script paths, interpreter flags and arity stay explicit.
    $psNames=@('build_profile_tests','worker_session_tests','local_coop_windows_tests','worker_coverage_tests')
    $psInventory=[pscustomobject]@{tests=@($psNames|ForEach-Object {
        [pscustomobject]@{name=$_;command=@('C:\fixture\pwsh.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',(Join-Path $fixture ('tests\'+$_+'.ps1')));properties=@()}
    })}
    function Copy-PsInventory { return ($psInventory|ConvertTo-Json -Depth 8|ConvertFrom-Json) }
    Assert-WorkerInventory $psInventory $fixture $directory $psNames
    Check $true 'all audited PowerShell fixtures accept exact registered six-argument command'
    foreach($index in @(0..($psNames.Count-1))) {
        $bad=Copy-PsInventory;$bad.tests[$index].command[0]='C:\fixture\powershell.exe'
        Refuses {Assert-WorkerInventory $bad $fixture $directory $psNames} 'Unaudited PowerShell command'
        $bad=Copy-PsInventory;$bad.tests[$index].command[5]=Join-Path $fixture 'tests\unreviewed-controller.ps1'
        Refuses {Assert-WorkerInventory $bad $fixture $directory $psNames} 'Unaudited PowerShell command'
        $bad=Copy-PsInventory;$bad.tests[$index].command+=@('-RunDirectory','C:\real-game')
        Refuses {Assert-WorkerInventory $bad $fixture $directory $psNames} 'Unaudited PowerShell command'
        $bad=Copy-PsInventory;$bad.tests[$index].command[1]='-NonInteractive'
        Refuses {Assert-WorkerInventory $bad $fixture $directory $psNames} 'Unaudited PowerShell command'
    }
    $bad=Copy-PsInventory;$bad.tests[1].properties=@([pscustomobject]@{name='DEPENDS';value=@('build_profile_tests')})
    Assert-WorkerInventory $bad $fixture $directory $psNames
    Check $true 'controller fixture ordered dependencies remain audited'
    $bad.tests[1].properties[0].value=@('unaudited_launch')
    Refuses {Assert-WorkerInventory $bad $fixture $directory $psNames} 'Unaudited ordered dependency'

    # Diamond Dogs Together (the Release receipt hashes and keeps DiamondDogsTogether.exe):
    # the core and UI fixtures are plain executables; the exe itself runs only --selftest.
    Check (@(@('ddt_archive_tests','ddt_core_tests_1','ddt_core_tests_5','launcher_ui_tests','ddt_launcher_selftest') | Where-Object { $release.tests -cnotcontains $_ }).Count -eq 0) 'release inventory audits the launcher fixtures and its --selftest'
    Check ($release.tests -cnotcontains 'ddt_core_tests' -and $release.tests -cnotcontains 'ddt_core_tests_6') 'the core fixture is audited as its five CTest shards (cmake/test_shards.cmake), not as one entry'
    Check ($release.tests -cnotcontains 'ddt_runtime_dat_integration' -and $release.tests -cnotcontains 'ddt_launcher_screens') 'the game-folder .dat build and the window screenshots stay outside the audited set'
    $ddtNames=@('ddt_launcher_selftest') + @(1..5 | ForEach-Object { 'ddt_core_tests_' + $_ })
    $ddtInventory=[pscustomobject]@{tests=@(
        [pscustomobject]@{name='ddt_launcher_selftest';command=@((Join-Path $directory 'DiamondDogsTogether.exe'),'--selftest');properties=@()}
    ) + @(1..5 | ForEach-Object { [pscustomobject]@{name=('ddt_core_tests_' + $_);command=@((Join-Path $directory 'ddt_core_tests.exe'),'--shard',"$_/5");properties=@([pscustomobject]@{name='TIMEOUT';value=300})} })}
    Assert-WorkerInventory $ddtInventory $fixture $directory $ddtNames
    Check $true 'the launcher --selftest and the five core fixture shards are accepted'
    # Shards (cmake/test_shards.cmake): exactly the base fixture's command plus its own --shard K/N.
    Check ($null -eq (Get-WorkerShard 'ddt_core_tests_6') -and $null -eq (Get-WorkerShard 'ddt_core_tests_0') -and $null -eq (Get-WorkerShard 'ddt_core_tests') -and
        (Get-WorkerShard 'ddt_core_tests_3').base -ceq 'ddt_core_tests' -and (Get-WorkerShard 'ddt_core_tests_3').count -eq 5) 'shard names map to their base fixture and count'
    $bad=$ddtInventory|ConvertTo-Json -Depth 8|ConvertFrom-Json;$bad.tests[1].command[2]='2/5'
    Refuses {Assert-WorkerInventory $bad $fixture $directory $ddtNames} 'Unaudited shard arguments'
    $bad=$ddtInventory|ConvertTo-Json -Depth 8|ConvertFrom-Json;$bad.tests[1].command[2]='1/4'
    Refuses {Assert-WorkerInventory $bad $fixture $directory $ddtNames} 'Unaudited shard arguments'
    $bad=$ddtInventory|ConvertTo-Json -Depth 8|ConvertFrom-Json;$bad.tests[1].command=@((Join-Path $directory 'ddt_core_tests.exe'))
    Refuses {Assert-WorkerInventory $bad $fixture $directory $ddtNames} 'Unaudited shard arguments'
    $bad=$ddtInventory|ConvertTo-Json -Depth 8|ConvertFrom-Json;$bad.tests[1].command=@((Join-Path $directory 'ddt_core_tests.exe'),'--local-watch-process','x','--shard','1/5')
    Refuses {Assert-WorkerInventory $bad $fixture $directory $ddtNames} 'Unexpected fixture arguments'
    $bad=$ddtInventory|ConvertTo-Json -Depth 8|ConvertFrom-Json;$bad.tests[1].command[0]=Join-Path $directory 'ddt_launcher.exe'
    Refuses {Assert-WorkerInventory $bad $fixture $directory $ddtNames} 'Unaudited fixture executable'
    $bad=$ddtInventory|ConvertTo-Json -Depth 8|ConvertFrom-Json;$bad.tests=@($bad.tests|Select-Object -First 5)
    Refuses {Assert-WorkerInventory $bad $fixture $directory $ddtNames} 'Required audited test is missing: ddt_core_tests_5'
    $bad=$ddtInventory|ConvertTo-Json -Depth 8|ConvertFrom-Json;$bad.tests[0].command[1]='--screenshot-all'
    Refuses {Assert-WorkerInventory $bad $fixture $directory $ddtNames} 'only its --selftest'
    $bad=$ddtInventory|ConvertTo-Json -Depth 8|ConvertFrom-Json;$bad.tests[0].command=@((Join-Path $directory 'DiamondDogsTogether.exe'),'--write-session','host')
    Refuses {Assert-WorkerInventory $bad $fixture $directory $ddtNames} 'only its --selftest'
    $bad=$ddtInventory|ConvertTo-Json -Depth 8|ConvertFrom-Json;$bad.tests[0].command[0]=Join-Path $fixture 'DiamondDogsTogether.exe'
    Refuses {Assert-WorkerInventory $bad $fixture $directory $ddtNames} 'Unaudited fixture executable'

    # 0.14.4 review (package-release-receipt-omits-new-shipped-tests): the Release receipt audits the fixture of every
    # module that ships in dinput8.dll or DiamondDogsTogether.exe (and the Lua it carries), and refuses a registered
    # test it neither runs nor excludes by name, so a new fixture is never silently left out of the receipt.
    $shipped=@('dog_guard_tests','partner_act_log_tests','partner_ammo_leak_tests','partner_dm_overlay_tests','partner_ride_seat_tests',
        'world_outcome_attclass_lua_check','world_companion_lua_check','world_buddy_companion_lua_check','world_companion_guard_lua_check',
        'world_relief_lua_check','world_relief_sync_lua_check','launcher_icon_tests','ddt_local_watch_process')
    Check (@($shipped | Where-Object { $release.tests -cnotcontains $_ }).Count -eq 0) 'release inventory audits every shipped module''s fixture (dog guard, companion/relief/outcome Lua, act log, ammo leak, overlay, ride seat, launcher icon, local-watch helper)'
    foreach ($left in @('ddt_runtime_dat_integration','ddt_launcher_screens','launcher_assets_current')) {
        Check ($release.tests -cnotcontains $left -and $release.excluded -ccontains $left) "excluded by name: $left"
    }
    # Round 2: the preflight of every hidden game run (Normal/Research, Invoke-WorkerScenario) builds no launcher for a
    # player, so it leaves out the launcher's real-process helper test (8-10 s, timing-sensitive) and its icon check;
    # only the Release receipt (the packaged build) runs them. The sub-second loader and Lua fixtures run everywhere.
    $launcherOnly=@('launcher_icon_tests','ddt_local_watch_process')
    foreach ($plan in @($normal,$background,$research)) {
        Check (@($launcherOnly | Where-Object { $plan.tests -ccontains $_ }).Count -eq 0) "a $($plan.profile_key) preflight skips the launcher's helper-process and icon checks"
        Check (@($shipped | Where-Object { $launcherOnly -cnotcontains $_ -and $plan.tests -cnotcontains $_ }).Count -eq 0) "a $($plan.profile_key) preflight runs the shipped loader and Lua fixtures"
    }
    Check ($release.tests.Count -eq $normal.tests.Count + $launcherOnly.Count -and @($normal.tests | Where-Object { $release.tests -cnotcontains $_ }).Count -eq 0) 'the Release receipt runs the preflight list plus the two launcher checks'
    $shipNames=@('ddt_local_watch_process','launcher_icon_tests','partner_dm_overlay_tests')
    $shipInventory=[pscustomobject]@{tests=@(
        [pscustomobject]@{name='ddt_local_watch_process';command=@((Join-Path $directory 'ddt_core_tests.exe'),'--local-watch-process',(Join-Path $directory 'DiamondDogsTogether.exe'));properties=@([pscustomobject]@{name='TIMEOUT';value=180})},
        [pscustomobject]@{name='launcher_icon_tests';command=@((Join-Path $directory 'launcher_icon_tests.exe'),(Join-Path $fixture 'native\launcher\resources\ddt_launcher.ico'),(Join-Path $fixture 'native\launcher\resources\ddt_logo.png'),(Join-Path $fixture 'docs\readme\logo.png'),(Join-Path $directory 'DiamondDogsTogether.exe'));properties=@()},
        [pscustomobject]@{name='partner_dm_overlay_tests';command=@((Join-Path $directory 'partner_dm_overlay_tests.exe'),(Join-Path $fixture 'lua\coop\partner_sync.lua'));properties=@()}
    )}
    function Copy-ShipInventory { return ($shipInventory|ConvertTo-Json -Depth 8|ConvertFrom-Json) }
    Assert-WorkerInventory $shipInventory $fixture $directory $shipNames
    Check $true 'the local-watch helper, icon and overlay fixtures are accepted with their exact arguments'
    $bad=Copy-ShipInventory;$bad.tests[0].command[1]='--write-session'
    Refuses {Assert-WorkerInventory $bad $fixture $directory $shipNames} 'local-watch helper fixture'
    $bad=Copy-ShipInventory;$bad.tests[0].command[2]=Join-Path $fixture 'DiamondDogsTogether.exe'
    Refuses {Assert-WorkerInventory $bad $fixture $directory $shipNames} 'local-watch helper fixture'
    $bad=Copy-ShipInventory;$bad.tests[0].command=@($bad.tests[0].command[0..1])
    Refuses {Assert-WorkerInventory $bad $fixture $directory $shipNames} 'local-watch helper fixture'
    $bad=Copy-ShipInventory;$bad.tests[1].command[4]=Join-Path $fixture 'other.exe'
    Refuses {Assert-WorkerInventory $bad $fixture $directory $shipNames} 'icon fixture'
    $bad=Copy-ShipInventory;$bad.tests[1].command[2]=Join-Path $fixture 'other.png'
    Refuses {Assert-WorkerInventory $bad $fixture $directory $shipNames} 'icon fixture'
    $bad=Copy-ShipInventory;$bad.tests[1].command+=@('--extra')
    Refuses {Assert-WorkerInventory $bad $fixture $directory $shipNames} 'icon fixture'
    $bad=Copy-ShipInventory;$bad.tests[2].command[1]=Join-Path $fixture 'lua\coop\other.lua'
    Refuses {Assert-WorkerInventory $bad $fixture $directory $shipNames} 'overlay fixture'
    $bad=Copy-ShipInventory;$bad.tests[2].command=@($bad.tests[2].command[0])
    Refuses {Assert-WorkerInventory $bad $fixture $directory $shipNames} 'overlay fixture'
    $coverage=[pscustomobject]@{tests=@(@($release.tests)+@($release.excluded) | ForEach-Object { [pscustomobject]@{name=$_;command=@('x.exe');properties=@()} })}
    Assert-WorkerCoverage $coverage $release
    Check $true 'a Release inventory of exactly the audited and the excluded tests is accepted'
    $coverage.tests += [pscustomobject]@{name='new_module_tests';command=@('x.exe');properties=@()}
    Refuses {Assert-WorkerCoverage $coverage $release} 'neither audits nor excludes: new_module_tests'
    Assert-WorkerCoverage $coverage $normal
    Check $true 'only the Release receipt (the packaged build) refuses an unclassified test'

    # Local session fixtures additionally require the one parser in this exact
    # owned build directory. No fixture can introduce runtime flags or a foreign tool.
    Check ($release.tests -ccontains 'local_coop_windows_tests') 'release inventory admits the inert window geometry fixture'
    Check ($release.tests -ccontains 'local_coop_session_tests') 'release inventory admits the inert activation journal fixture'
    $localNames=@('local_coop_windows_tests','local_coop_session_tests')
    $localInventory=[pscustomobject]@{tests=@(
        [pscustomobject]@{name='local_coop_windows_tests';command=@('C:\fixture\pwsh.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',(Join-Path $fixture 'tests\local_coop_windows_tests.ps1'));properties=@()},
        [pscustomobject]@{name='local_coop_session_tests';command=@('C:\fixture\pwsh.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',(Join-Path $fixture 'tests\local_coop_session_tests.ps1'),'-IniCheck',(Join-Path $directory 'lab_ini_check.exe'));properties=@()}
    )}
    function Copy-LocalInventory { return ($localInventory|ConvertTo-Json -Depth 8|ConvertFrom-Json) }
    Assert-WorkerInventory $localInventory $fixture $directory $localNames
    Check $true 'local fixtures admit exactly six window arguments and eight journal arguments'
    $bad=Copy-LocalInventory;$bad.tests[1].command[0]='C:\fixture\powershell.exe'
    Refuses {Assert-WorkerInventory $bad $fixture $directory $localNames} 'Unaudited local-session fixture command'
    $bad=Copy-LocalInventory;$bad.tests[1].command[5]=Join-Path $fixture 'tests\worker_session_tests.ps1'
    Refuses {Assert-WorkerInventory $bad $fixture $directory $localNames} 'Unaudited local-session fixture command'
    $bad=Copy-LocalInventory;$bad.tests[1].command+=@('-RunDirectory','C:\real-game')
    Refuses {Assert-WorkerInventory $bad $fixture $directory $localNames} 'Unaudited local-session fixture command'
    $bad=Copy-LocalInventory;$bad.tests[1].command[7]=Join-Path $fixture 'foreign\lab_ini_check.exe'
    Refuses {Assert-WorkerInventory $bad $fixture $directory $localNames} 'Unaudited local-session fixture command'
    $bad=Copy-LocalInventory;$bad.tests[1].command[7]=Join-Path $directory 'unreviewed_parser.exe'
    Refuses {Assert-WorkerInventory $bad $fixture $directory $localNames} 'Unaudited local-session fixture command'
    $bad=Copy-LocalInventory;$bad.tests[1].command[6]='-RunDirectory'
    Refuses {Assert-WorkerInventory $bad $fixture $directory $localNames} 'Unaudited local-session fixture command'
    $bad=Copy-LocalInventory;$bad.tests[1].command[6]='-inicheck'
    Refuses {Assert-WorkerInventory $bad $fixture $directory $localNames} 'Unaudited local-session fixture command'
    $bad=Copy-LocalInventory;$bad.tests[1].command[1]='-NonInteractive'
    Refuses {Assert-WorkerInventory $bad $fixture $directory $localNames} 'Unaudited local-session fixture command'
    $bad=Copy-LocalInventory;$bad.tests[1].command=@($bad.tests[1].command[0..6])
    Refuses {Assert-WorkerInventory $bad $fixture $directory $localNames} 'Unaudited local-session fixture command'
    $bad=Copy-LocalInventory;$bad.tests[1].command=@($bad.tests[1].command[0..5])
    Refuses {Assert-WorkerInventory $bad $fixture $directory $localNames} 'Unaudited local-session fixture command'
    $bad=Copy-LocalInventory;$bad.tests[0].command+=@('-IniCheck',(Join-Path $directory 'lab_ini_check.exe'))
    Refuses {Assert-WorkerInventory $bad $fixture $directory $localNames} 'Unaudited PowerShell command'

    $receipt = Join-Path $fixture 'receipt.json'
    Write-WorkerJson $receipt ([ordered]@{arguments=@('space value',('line1'+"`n"+'line2'));status='first'})
    Write-WorkerJson $receipt ([ordered]@{arguments=@('space value',('line1'+"`n"+'line2'));status='final'})
    $read = [IO.File]::ReadAllText($receipt) | ConvertFrom-Json
    Check ($read.status -ceq 'final' -and $read.arguments[0] -ceq 'space value' -and $read.arguments[1] -ceq "line1`nline2" -and !(Test-Path -LiteralPath ($receipt+'.tmp'))) 'atomic receipt replacement preserves literal arguments/newlines'

    $sourceRoot=Join-Path $fixture 'source-project'
    $null=New-Item -ItemType Directory -Path (Join-Path $sourceRoot 'third_party'),(Join-Path $sourceRoot 'native') -Force
    [IO.File]::WriteAllText((Join-Path $sourceRoot 'CMakeLists.txt'),'project(fixture LANGUAGES NONE)')
    [IO.File]::WriteAllText((Join-Path $sourceRoot 'native\fixture_impl.inc'),'// #included loader fragment')
    $targetA=Join-Path $fixture 'dependency-a'; $targetB=Join-Path $fixture 'dependency-b'
    foreach($target in @($targetA,$targetB)) {
        $null=New-Item -ItemType Directory -Path (Join-Path $target 'src') -Force
        [IO.File]::WriteAllText((Join-Path $target 'CMakeLists.txt'),'add_library(fixture STATIC src/hook.c)')
        [IO.File]::WriteAllText((Join-Path $target 'src\hook.c'),'int fixture_hook=1;')
        [IO.File]::WriteAllText((Join-Path $target 'LICENSE.txt'),'INERT dependency license fixture')
    }
    $knownLink=Join-Path $sourceRoot 'third_party\minhook'
    $null=New-Item -ItemType Junction -Path $knownLink -Target $targetA
    $null=$ownedLinks.Add($knownLink)
    $before=Get-WorkerSourceHashes $sourceRoot
    Check ($before.dependencies['third_party\minhook'].linked -and $before.dependencies['third_party\minhook'].resolved_path -ieq $targetA) 'snapshot records the actual resolved MinHook junction target'
    Check ($before.hashes.Contains('third_party\minhook\src\hook.c') -and $before.hashes['third_party\minhook\src\hook.c'] -ceq (Get-FileHash -LiteralPath (Join-Path $targetA 'src\hook.c')).Hash) 'snapshot hashes actual linked dependency bytes under their logical source path'
    Check ($before.hashes.Contains('third_party\minhook\LICENSE.txt') -and (Get-Item -LiteralPath $knownLink).Attributes -band [IO.FileAttributes]::ReparsePoint) 'dependency attribution is hashed and the existing link remains intact'
    Check ($before.hashes.Contains('native\fixture_impl.inc') -and $before.hashes['native\fixture_impl.inc'] -ceq (Get-FileHash -LiteralPath (Join-Path $sourceRoot 'native\fixture_impl.inc')).Hash) 'snapshot hashes the .inc fragments native sources #include'
    Assert-WorkerSourceUnchanged $before (Get-WorkerSourceHashes $sourceRoot)
    [IO.File]::WriteAllText((Join-Path $targetA 'src\hook.c'),'int fixture_hook=2;')
    Refuses { Assert-WorkerSourceUnchanged $before (Get-WorkerSourceHashes $sourceRoot) } 'Source changed'
    [IO.File]::WriteAllText((Join-Path $targetA 'src\hook.c'),'int fixture_hook=1;')
    # Remove only this fixture-owned link, never its target; no recursive delete.
    Remove-Item -LiteralPath $knownLink -Force
    $null=New-Item -ItemType Junction -Path $knownLink -Target $targetB
    $retargeted=Get-WorkerSourceHashes $sourceRoot
    Check ($retargeted.hashes['third_party\minhook\src\hook.c'] -ceq $before.hashes['third_party\minhook\src\hook.c']) 'replacement target has intentionally identical source bytes'
    Refuses { Assert-WorkerSourceUnchanged $before $retargeted } 'dependency target changed'
    $unknownLink=Join-Path $sourceRoot 'tools\foreign'
    $null=New-Item -ItemType Directory -Path (Split-Path -Parent $unknownLink) -Force
    $null=New-Item -ItemType Junction -Path $unknownLink -Target $targetA
    $null=$ownedLinks.Add($unknownLink)
    Refuses { Get-WorkerSourceHashes $sourceRoot } 'unreviewed directory link'

    # Execute the actual command wrapper with ONLY its Process constructor
    # replaced. ProcessStartInfo/argument lists, receipts and finally blocks
    # are real; the recording process class has no native/process APIs.
    Add-Type -TypeDefinition @'
using System;
using System.Diagnostics;
using System.Threading.Tasks;
public sealed class MgsCoopWorkerStreamFixture {
    readonly string text;
    public MgsCoopWorkerStreamFixture(string value) { text=value; }
    public Task<string> ReadToEndAsync() { return Task.FromResult(text); }
}
public sealed class MgsCoopWorkerProcessFixture : IDisposable {
    public static string Mode="success";
    public static ProcessStartInfo LastInfo;
    public static int KillCalls, DisposeCalls;
    public static bool EntireTree;
    public ProcessStartInfo StartInfo { get; set; }
    public bool HasExited { get; private set; }
    public int Id { get { return 731; } }
    public DateTime StartTime { get { return new DateTime(2026,10,7,1,2,3,DateTimeKind.Utc); } }
    public int ExitCode { get { return Mode=="nonzero" ? 17 : 0; } }
    public MgsCoopWorkerStreamFixture StandardOutput { get { return new MgsCoopWorkerStreamFixture("stdout\nline2\n"); } }
    public MgsCoopWorkerStreamFixture StandardError { get { return new MgsCoopWorkerStreamFixture("stderr\n"); } }
    public bool Start() { LastInfo=StartInfo; return Mode!="start_refused"; }
    public bool WaitForExit(int milliseconds) {
        if(Mode=="abort" && !HasExited) throw new InvalidOperationException("fixture interrupted");
        if(Mode=="timeout" && !HasExited) return false;
        HasExited=true; return true;
    }
    public void WaitForExit() { HasExited=true; }
    public void Kill(bool entireProcessTree) { ++KillCalls; EntireTree=entireProcessTree; HasExited=true; }
    public void Dispose() { ++DisposeCalls; }
}
'@
    $nativeDefinition = @($ast.FindAll({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq 'Invoke-WorkerCommand' },$true))
    Check ($nativeDefinition.Count -eq 1 -and [regex]::Matches($nativeDefinition[0].Extent.Text,[regex]::Escape('[Diagnostics.Process]::new()')).Count -eq 1) 'only the real Process constructor is replaced for detached forwarding tests'
    . ([scriptblock]::Create($nativeDefinition[0].Extent.Text.Replace('[Diagnostics.Process]::new()','[MgsCoopWorkerProcessFixture]::new()')))
    $script:projectRoot=$fixture; $script:runDirectory=$fixture
    $script:receiptPath=Join-Path $fixture 'command-receipt.json'
    $script:receipt=[ordered]@{commands=[Collections.Generic.List[object]]::new()}
    $commandContext=[pscustomobject]@{
        receipt=$script:receipt; receipt_path=$script:receiptPath; run_directory=$fixture; project_root=$fixture
        write_json=${function:Write-WorkerJson}; invoke_command=${function:Invoke-WorkerCommand}
        cmake_path='fixture-only.exe'; build_jobs=3; command_timeout=2
    }
    $arguments=@('space value',"line1`nline2",'literal$(never-run)')
    $result=Invoke-WorkerCommand $commandContext 'fixture-only.exe' $arguments 'success' 2
    $info=[MgsCoopWorkerProcessFixture]::LastInfo
    Check (!$info.UseShellExecute -and $info.CreateNoWindow -and $info.RedirectStandardOutput -and $info.RedirectStandardError) 'real start-info requires hidden shell-free redirected execution'
    Check ($info.ArgumentList.Count -eq 3 -and $info.ArgumentList[0] -ceq $arguments[0] -and $info.ArgumentList[1] -ceq $arguments[1] -and $info.ArgumentList[2] -ceq $arguments[2]) 'literal spaces/newlines/command-substitution characters remain typed arguments'
    Check ($result.Output -ceq "stdout`nline2`n" -and $result.Error -ceq "stderr`n" -and $result.ExitCode -eq 0) 'wrapper returns exactly the original stdout/stderr/exit without progress contamination'
    $last=$script:receipt.commands[-1]
    Check ([IO.File]::ReadAllText($last.stdout) -ceq $result.Output -and [IO.File]::ReadAllText($last.stderr) -ceq $result.Error -and $last.exit_code -eq 0 -and $last.pid -eq 731) 'successful command gets exact output and process identity receipt'
    Check ([MgsCoopWorkerProcessFixture]::DisposeCalls -eq 1 -and [MgsCoopWorkerProcessFixture]::KillCalls -eq 0) 'finished command disposes without killing another process'
    [MgsCoopWorkerProcessFixture]::Mode='nonzero'
    Refuses { Invoke-WorkerCommand $commandContext 'fixture-only.exe' @() 'nonzero' 2 } 'exit 17'
    Check ($script:receipt.commands[-1].exit_code -eq 17 -and [IO.File]::ReadAllText($script:receipt.commands[-1].stderr) -ceq "stderr`n") 'failed command retains its real error stream and exit code'
    [MgsCoopWorkerProcessFixture]::Mode='timeout'
    Refuses { Invoke-WorkerCommand $commandContext 'fixture-only.exe' @() 'timeout' 2 } 'timed out'
    Check ($script:receipt.commands[-1].timed_out -and [MgsCoopWorkerProcessFixture]::KillCalls -eq 1 -and [MgsCoopWorkerProcessFixture]::EntireTree) 'timeout cleans the owned child tree and remains a failed receipt'
    [MgsCoopWorkerProcessFixture]::Mode='start_refused'
    Refuses { Invoke-WorkerCommand $commandContext 'fixture-only.exe' @() 'start-refused' 2 } 'Cannot start'
    Check ($null -eq $script:receipt.commands[-1].pid -and $null -eq $script:receipt.commands[-1].exit_code -and [MgsCoopWorkerProcessFixture]::KillCalls -eq 1) 'start failure cannot kill a nonexistent/unowned process'
    [MgsCoopWorkerProcessFixture]::Mode='abort'
    Refuses { Invoke-WorkerCommand $commandContext 'fixture-only.exe' @() 'interrupted' 2 } 'fixture interrupted'
    Check ([MgsCoopWorkerProcessFixture]::KillCalls -eq 2 -and [MgsCoopWorkerProcessFixture]::EntireTree -and [MgsCoopWorkerProcessFixture]::DisposeCalls -eq 5) 'interrupted command cleans its child tree and every path disposes the process object'
    $stored=[IO.File]::ReadAllText($script:receiptPath)|ConvertFrom-Json
    Check ($stored.commands.Count -eq 5 -and $stored.commands[0].arguments[2] -ceq 'literal$(never-run)' -and $stored.commands[2].timed_out) 'JSON keeps failed/completed command history and exact arguments'
    # Exercise nested child-script -> cmake -> actual command body. Former
    # script-scope names and closure inputs deliberately have wrong values.
    $child=Join-Path $fixture 'nested-build-fixture.ps1'
    [IO.File]::WriteAllText($child, @'
Set-StrictMode -Version Latest
$script:receipt=$null; $script:runDirectory='wrong'; $script:receiptPath='wrong'; $script:projectRoot='wrong'
$Context=$null; $workerCommandContext=$null; $cmakePath='wrong'; $BuildJobs=999; $CommandTimeoutSec=999
cmake -S 'literal source space' -B 'literal cache space'
cmake --build 'literal cache space' --parallel
if($LASTEXITCODE -ne 0) { throw 'nested native exit was not preserved' }
'@)
    [MgsCoopWorkerProcessFixture]::Mode='success'
    Set-Item -Path Function:cmake -Value (New-WorkerCmakeOverride $commandContext)
    & $child
    Check ($commandContext.receipt.commands.Count -eq 7 -and $commandContext.receipt.commands[-2].tag -ceq 'configure-bootstrap' -and $commandContext.receipt.commands[-1].tag -ceq 'build-loader-bootstrap') 'nested calls retain the owner receipt and both command identities'
    Check ([MgsCoopWorkerProcessFixture]::LastInfo.WorkingDirectory -ceq $fixture -and [MgsCoopWorkerProcessFixture]::LastInfo.FileName -ceq 'fixture-only.exe') 'nested lexical capture keeps owner executable and working directory'
    Check ([MgsCoopWorkerProcessFixture]::LastInfo.ArgumentList.Count -eq 4 -and [MgsCoopWorkerProcessFixture]::LastInfo.ArgumentList[1] -ceq 'literal cache space' -and [MgsCoopWorkerProcessFixture]::LastInfo.ArgumentList[3] -ceq '3') 'nested build keeps literal paths and captured bounded jobs'
    Check (([IO.File]::ReadAllText($commandContext.receipt_path)|ConvertFrom-Json).commands.Count -eq 7 -and [MgsCoopWorkerProcessFixture]::DisposeCalls -eq 7) 'nested completion writes owner JSON and disposes both fake processes'
    [MgsCoopWorkerProcessFixture]::Mode='nonzero'
    Refuses { & $child } 'exit 17'
    Check ($commandContext.receipt.commands.Count -eq 8 -and $commandContext.receipt.commands[-1].exit_code -eq 17 -and [MgsCoopWorkerProcessFixture]::DisposeCalls -eq 8) 'nested failure retains native exit and stops before second command'
    Write-Output "Worker policy checks passed: $checks. No process launch, build, game, installation or system input ran."
} finally {
    foreach($link in $ownedLinks) {
        if(!(Test-WorkerUnder $link $fixture)) { throw 'Fixture link cleanup escaped its owned root' }
        if(Test-Path -LiteralPath $link) {
            if(!((Get-Item -LiteralPath $link -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Fixture link identity changed; refusing cleanup' }
            Remove-Item -LiteralPath $link -Force
        }
    }
    if (Test-Path -LiteralPath $fixture) {
        $resolved = (Resolve-Path -LiteralPath $fixture).Path
        if ((Split-Path -Parent $resolved) -ine [IO.Path]::GetFullPath($buildRoot) -or
            (Split-Path -Leaf $resolved) -cne ('worker-checks-tests-'+$nonce) -or
            !(Test-Path -LiteralPath $marker -PathType Leaf) -or [IO.File]::ReadAllText($marker) -cne $nonce) {
            throw 'Worker fixture cleanup ownership check failed'
        }
        foreach ($item in @((Get-Item -LiteralPath $resolved)) + @(Get-ChildItem -LiteralPath $resolved -Recurse -Force)) {
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Worker fixture refuses link cleanup' }
        }
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
