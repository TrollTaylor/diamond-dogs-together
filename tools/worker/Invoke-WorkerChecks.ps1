#Requires -Version 7
<#
.SYNOPSIS
Builds and checks the mod without launching MGSV or using desktop input.
.DESCRIPTION
Uses a separate cache, one checkout coordinator lock and immutable per-run
receipts. Only explicitly audited CTest commands run. Full lab/friend launch
fixtures and installer flows are excluded. This is not a runtime game worker.
-PlanOnly prints the policy without configuring, building or writing files.
-Profile Release is the friend/release loader (docs/two-pc-play.md "Release
build"): exactly the stock46 gun pieces (offline equipment, preload, writer;
runtime-gated on the native gun profile), every other experiment and the
background worker OFF. tools/package.ps1 -Friend packages its loader.
#>
[CmdletBinding()]
param(
    [ValidateSet('Normal', 'Research', 'Release')][string]$Profile = 'Normal',
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [string[]]$EnableExperiment = @(),
    [switch]$EnableBackgroundWorker,
    [string]$BuildDirectory,
    [ValidateRange(1, 3600)][int]$LockTimeoutSec = 120,
    [ValidateRange(1, 7200)][int]$CommandTimeoutSec = 1200,
    [ValidateRange(1, 600)][int]$TestTimeoutSec = 120,
    [ValidateRange(1, 64)][int]$BuildJobs = 8,
    [switch]$PlanOnly
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'WorkerSource.psm1') -Force -DisableNameChecking

function Get-WorkerExperiments {
    return @(
        'MGS_LOCAL_NATIVE_ACTIVATION_OBSERVER', 'MGS_LOCAL_MGO_UNIQUE_REST_OBSERVER', 'MGS_LOCAL_MGO_STANDING_CYCLE_OBSERVER', 'MGS_LOCAL_MGO_STANDING_LOOKUP_OBSERVER', 'MGS_LOCAL_MGO_STANDING_FRAME_OBSERVER', 'MGS_LOCAL_BUDDY_CAPACITY_TEST', 'MGS_LOCAL_GUN_PRELOAD_TEST', 'MGS_LOCAL_OFFLINE_EQUIPMENT_TEST',
        'MGS_LOCAL_STOCK46_PRELOAD_TEST', 'MGS_LOCAL_STOCK46_WRITER_TEST', 'MGS_LOCAL_STOCK46_AMMO_TEST',
        'MGS_LOCAL_ACTION_EVENTS', 'MGS_LOCAL_UPPER_END_OBSERVE',
        'MGS_LOCAL_UPPER_PHASE_OBSERVE', 'MGS_LOCAL_UPPER_MANUAL_IDLE',
        'MGS_LOCAL_UPPER_PROBE', 'MGS_LOCAL_UPPER_CHANNEL2', 'MGS_LOCAL_UPPER_FIRE_ZERO_BLEND'
    )
}
# The friend/release loader's experiments: the stock46 gun pieces only.
function Get-WorkerReleaseExperiments {
    return @('MGS_LOCAL_OFFLINE_EQUIPMENT_TEST', 'MGS_LOCAL_STOCK46_PRELOAD_TEST', 'MGS_LOCAL_STOCK46_WRITER_TEST')
}
function Get-WorkerTests {
    # Fixed names: a newly registered test never silently becomes executable.
    # Native calls in these fixtures target recording/fake backends, not MGSV.
    return @(
        'session_release_tests', 'mission_termination_tests',
        'equipment_allocation_probe_tests', 'equipment_stock_probe_tests',
        'equipment_stock_contract_tests', 'equipment_stock_writer_tests',
        'client_session_request_tests', 'heap_trial_tests', 'lab_config_tests', 'lab_status_tests',
        'link_protocol_tests', 'link_session_tests', 'link_crypto_tests', 'world_wire_tests', 'world_receipt_tests',
        'save_handoff_wire_tests', 'save_handoff_core_tests', 'save_handoff_io_tests',
        'buddy_pose_tests', 'buddy_ride_read_off_tests', 'buddy_ride_read_on_tests', 'ride_player_observer_tests', 'carry_info_tests', 'carry_plugin_tests', 'carry_provider_tests', 'sleep_plugin_tests', 'sleep_graph_tests', 'vehicle_controller_read_tests', 'camera_mode_window_tests', 'camera_mode_carry_composition_tests', 'carry_request_payload_tests', 'sleep_body0_lease_tests', 'carry_owned_handshake_tests', 'sleep_body0_transition_tests', 'sleep_worker_controls_tests', 'carry_request_retirement_tests', 'carry_request_retirement_read_tests', 'carry_queued_abort_tests', 'carry_cold_rollback_tests', 'native_pose_exclusion_tests', 'native_pose_birth_tests', 'native_pose_activation_tests', 'whole_player_lifetime_tests', 'carry_model_preflight_tests', 'native_pose_selected_export_tests', 'native_pose_hold_owners_tests', 'native_pose_carry_participants_tests', 'native_pose_carry_retirement_tests', 'native_pose_camera_reader_tests', 'vehicle_entry_camera_tests', 'route_carry_slot_tests', 'native_work_timing_tests', 'native_observer_negative_work_tests', 'native_pose_reject_diagnostic_tests', 'native_pose_teardown_diagnostic_tests', 'native_pose_parts_diagnostic_tests', 'sleep_partner_bridge_tests', 'sleep_partner_lease_tests', 'sleep_partner_owned_tests', 'sleep_parts_tests', 'sleep_parts_diagnostic_tests', 'sleep_recovery_tests', 'sleep_carry_context_tests', 'sleep_rest_maintenance_tests', 'sleep_body0_rest_tests', 'stock_sleep_release_tests', 'coop_carry_wire_tests', 'coop_carry_original_view_tests', 'native_pose_birth_census_tests', 'native_pose_vehicle_core_tests', 'sleep_native_bank_trace_off_tests', 'sleep_native_bank_trace_on_tests', 'vehicle_root_wiring_tests', 'vehicle_driver_wiring_tests', 'vehicle_native_protection_tests', 'vehicle_mirror_idle_work_tests', 'vehicle_native_protection_gate_tests', 'vehicle_mirror_tests', 'vehicle_mirror_hook_tests', 'vehicle_mirror_close_tests', 'reliable_origin_tests', 'prologue_provider_tests', 'vehicle_lifetime_read_tests', 'vehicle_resource_lifetime_tests', 'vehicle_cleanup_camera_read_tests', 'horse_cargo_native_tests', 'horse_player_cargo_bridge_tests', 'horse_cargo_pair_tests', 'horse_cargo_natural_flow_tests', 'horse_finder_entry_tests', 'npc_pickup_entry_tests', 'horse_physical_natural_tests', 'vehicle_registry_read_tests', 'qualified_sleep_lookup_observer_tests','standing_selection_off_tests','standing_selection_on_tests','standing_selection_bank_off_tests','standing_selection_bank_on_tests','native_motion_frame_off_tests','native_motion_frame_on_tests','native_motion_stack_off_tests','native_motion_stack_on_tests','mgo_standing_cycle_profile_off_tests','mgo_standing_cycle_profile_on_tests','sleep_standing_cycle_asset_off_tests','sleep_standing_cycle_asset_on_tests','mgo_unique_rest_profile_off_tests','mgo_unique_rest_profile_on_tests','sleep_unique_bind_off_tests','sleep_unique_bind_on_tests','sleep_unique_bind_wrapper_off_tests','sleep_unique_bind_wrapper_on_tests','sleep_unique_asset_read_tests','sleep_motion_asset_read_tests', 'engine_census_tests', 'coop_team_wake_tests', 'horse_owned_cargo_tests', 'horse_player_physical_tests', 'coop_passenger_tests', 'coop_sleep_policy_tests', 'sleep_deferred_carry_tests', 'sleep_queued_carry_tests', 'player_tree_closure_tests', 'carry_target_service_tests', 'carry_vars_pair_word_tests', 'native_pose_message_lifetime_tests', 'native_pose_local_carry_slice_tests', 'carry_dispatch_lifetime_tests', 'native_pose_hook_groups_tests', 'dual_parts_tests', 'dual_parts_cold_profile_tests', 'owned_flag_map_tests', 'carry_op0_origin_tests', 'carry_queue_lifetime_tests', 'carry_p238_cold_read_tests', 'carry_deferred_copy_tests', 'carry_op1_owned_tests', 'carry_op2_shared_tests', 'row4_storage_tests', 'buddy_capacity_census_off_tests', 'buddy_capacity_census_on_tests', 'buddy_capacity_walker_off_tests', 'buddy_capacity_walker_on_tests', 'partner_receipt_tests', 'partner_input_tests', 'partner_recon_tests', 'partner_dmmap_tests',
        'pose_interp_tests', 'partner_source_turns_tests', 'partner_action_events_tests',
        'partner_action_link_tests', 'partner_upper_pipeline_tests', 'client_reset_tests',
        'session_tests', 'import_table_tests', 'steam_runtime_tests', 'steam_lifecycle_tests',
        'steam_getter_tests', 'typed_payload_tests', 'player_sync_tests', 'input_receiver_tests',
        'steam_packet_tests', 'four_player_lab', 'peer_router_tests', 'allocation_bridge_tests',
        'session_allocation_reuse_tests', 'script_load_bridge_tests', 'player_ghost_tests',
        'link_udp_tests', 'steam_loopback_tests', 'steam_shim_patches_tests', 'steam_identity_tests', 'steam_transport_tests',
        'partner_upper_end_off_tests', 'partner_upper_end_on_tests',
        'partner_upper_phase_off_tests', 'partner_upper_phase_on_tests', 'partner_upper_phase_combined_tests',
        'partner_upper_manual_idle_off_tests', 'partner_upper_manual_idle_on_tests',
        'partner_upper_probe_tests', 'partner_upper_probe_forwarding_tests',
        'partner_upper_channel2_tests', 'partner_upper_fire_zero_blend_tests',
        'partner_cbox_tests',
        'partner_player_tests', 'teammate_targeting_tests', 'downed_damage_tests', 'downed_player_tests', 'partner_driver_tests', 'partner_input_hook_tests',
        'partner_support_equip_tests', 'partner_fire_support_tests', 'partner_support_policy_tests', 'partner_support_guard_fixture', 'partner_placed_census_tests', 'partner_placed_policy_tests',
        'partner_native_tests', 'partner_equip_wire_tests', 'partner_ride_wire_tests', 'partner_traverse_tests', 'partner_icon_tests', 'partner_gesture_tests', 'partner_gesture_hook_tests', 'partner_vehicle_tests', 'partner_board_tests', 'partner_pass_camera_tests', 'partner_defer_tests', 'partner_defer_pose_nested_foreign_tests', 'partner_defer_pose_held_generation_tests',
        'partner_mount_tests', 'partner_body_safety_tests', 'log_budget_tests',
        'debug_menu_tests', 'debug_overlay_tests', 'soldier_warp_tests', 'win_import_patch_tests', 'probe_forwarding',
        'master_server_patch_tests', 'master_server_patch_retarget_tests', 'master_server_patch_retarget_loopback_tests',
        'worker_scenario_off_tests', 'worker_scenario_on_tests', 'worker_protocol_tests', 'worker_protocol_capacity_off_tests', 'worker_protocol_capacity_on_tests', 'worker_debug_tests', 'worker_input_fixture_tests',
        'build_profile_tests', 'mgo_standing_cycle_session_tests', 'mgo_standing_frame_policy_tests', 'mgo_unique_rest_session_tests', 'worker_session_tests', 'worker_loading_tests', 'local_coop_windows_tests', 'local_coop_session_tests',
        'host_admission_lua_check', 'equipment_allocation_lua_check', 'equipment_stock_preload_lua_check',
        'equipment_observation_lua_check', 'session_lifecycle_lua_check', 'client_join_lua_check',
        'session_feedback_lua_check', 'damage_parameters_lua_check', 'damage_lifecycle_lua_check',
        'partner_sync_lua_check', 'partner_actions_lua_check', 'debug_menu_lua_check', 'debug_buddy_lua_check', 'debug_vehicle_lua_check',
        'world_sync_lua_check', 'world_downed_lua_check', 'world_revive_input_lua_check', 'world_buddy_lua_check', 'world_ride_pose_lua_check', 'world_horse_cargo_lua_check', 'world_buddy_ride_lua_check', 'world_follow_lua_check', 'world_handoff_lua_check', 'world_goto_lua_check', 'world_align_lua_check',
        'world_follow_game_lua_check', 'world_env_game_lua_check', 'debug_menu_game_lua_check',
        'bootstrap_patch_check', 'bootstrap_lua_check', 'host_trial_patch_check', 'host_trial_lua_check',
        'partner_sync_composed_lua_check', 'world_sync_composed_lua_check',
        'mission_trace_patch_check', 'mission_trace_lua_check', 'death_policy_lua_check',
        'death_policy_patch_check', 'death_policy_composed_lua_check',
        'player_package_patch_check', 'player_package_lua_check', 'player_package_mission_list_lua_check', 'damage_parameters_patch_check',
        'damage_parameters_composed_lua_check', 'character_receipt_tests', 'player_type_patch_tests',
        'character_lua_check', 'character_game_lua_check', 'character_composed_lua_check'
    )
}
function Get-WorkerHash([string]$Text) {
    return [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($Text)))
}
function Test-WorkerUnder([string]$Path, [string]$Root) {
    $full = [IO.Path]::GetFullPath($Path).TrimEnd([char]92, [char]47)
    $base = [IO.Path]::GetFullPath($Root).TrimEnd([char]92, [char]47)
    return $full.StartsWith($base + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)
}
function Assert-WorkerPath([string]$Path, [string]$Root) {
    if (!(Test-WorkerUnder $Path $Root)) { throw "Worker path must remain below $Root" }
    $cursor = [IO.Path]::GetFullPath($Path)
    while ($cursor) {
        if (Test-Path -LiteralPath $cursor) {
            if ((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Worker path refuses filesystem links: $cursor"
            }
        }
        $cursor = [IO.Path]::GetDirectoryName($cursor)
    }
}
function Get-WorkerPlan {
    param([string]$Root, [string]$SelectedProfile, [string]$SelectedConfiguration,
        [string[]]$Experiments, [bool]$Background, [string]$Directory)
    $Root = [IO.Path]::GetFullPath($Root).TrimEnd([char]92, [char]47)
    $known = @(Get-WorkerExperiments)
    if ($SelectedProfile -eq 'Release') {
        # Fixed experiments, never the background worker (a friend DLL can never run the rehearsal).
        if (@($Experiments).Count) { throw 'Release profile has fixed experiments (the stock46 gun pieces)' }
        if ($Background) { throw 'Release profile never enables the background worker' }
        $Experiments = @(Get-WorkerReleaseExperiments)
    }
    $enabled = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($name in @($Experiments)) {
        if ($known -cnotcontains $name) { throw "Unknown or noncanonical experiment: $name" }
        if (!$enabled.Add($name)) { throw "Duplicate experiment: $name" }
    }
    if ($SelectedProfile -eq 'Normal' -and $enabled.Count) { throw 'Normal profile cannot enable research experiments' }
    if ($SelectedProfile -eq 'Research' -and !$enabled.Count) { throw 'Research profile requires explicit experiments' }
    if ($enabled.Contains('MGS_LOCAL_BUDDY_CAPACITY_TEST') -and (!$Background -or $SelectedProfile -cne 'Research')) {
        throw 'Buddy capacity requires an explicit Research/background ON build'
    }
    if ($enabled.Contains('MGS_LOCAL_NATIVE_ACTIVATION_OBSERVER')) {
        $base=@('MGS_LOCAL_OFFLINE_EQUIPMENT_TEST','MGS_LOCAL_STOCK46_PRELOAD_TEST','MGS_LOCAL_STOCK46_WRITER_TEST')
        $allowed=@($base)+@('MGS_LOCAL_NATIVE_ACTIVATION_OBSERVER','MGS_LOCAL_MGO_UNIQUE_REST_OBSERVER','MGS_LOCAL_MGO_STANDING_CYCLE_OBSERVER','MGS_LOCAL_MGO_STANDING_LOOKUP_OBSERVER','MGS_LOCAL_MGO_STANDING_FRAME_OBSERVER')
        if (!$Background -or $SelectedProfile -cne 'Research' -or @($base|Where-Object{!$enabled.Contains($_)}).Count -or @($enabled|Where-Object{$_ -cnotin $allowed}).Count) {
            throw 'Activation observer requires explicit Research/background ON stock46 base plus only qualified optional MGO observers.'
        }
    }
    if ($enabled.Contains('MGS_LOCAL_MGO_UNIQUE_REST_OBSERVER')) {
        $expected=@('MGS_LOCAL_OFFLINE_EQUIPMENT_TEST','MGS_LOCAL_STOCK46_PRELOAD_TEST','MGS_LOCAL_STOCK46_WRITER_TEST','MGS_LOCAL_MGO_UNIQUE_REST_OBSERVER')
        if($enabled.Contains('MGS_LOCAL_NATIVE_ACTIVATION_OBSERVER')){$expected+='MGS_LOCAL_NATIVE_ACTIVATION_OBSERVER'}
        if (!$Background -or $SelectedProfile -cne 'Research' -or $enabled.Count -ne $expected.Count -or @($expected|Where-Object{!$enabled.Contains($_)}).Count) {
            throw 'Unique MGO observer requires exact Research/background ON stock46-only observer flags; append remains OFF'
        }
    }
    if ($enabled.Contains('MGS_LOCAL_MGO_STANDING_CYCLE_OBSERVER')) {
        $expected=@('MGS_LOCAL_OFFLINE_EQUIPMENT_TEST','MGS_LOCAL_STOCK46_PRELOAD_TEST','MGS_LOCAL_STOCK46_WRITER_TEST','MGS_LOCAL_MGO_STANDING_CYCLE_OBSERVER')
        if($enabled.Contains('MGS_LOCAL_MGO_STANDING_LOOKUP_OBSERVER')){$expected+='MGS_LOCAL_MGO_STANDING_LOOKUP_OBSERVER'}
        if($enabled.Contains('MGS_LOCAL_MGO_STANDING_FRAME_OBSERVER')){$expected+='MGS_LOCAL_MGO_STANDING_FRAME_OBSERVER'}
        if($enabled.Contains('MGS_LOCAL_NATIVE_ACTIVATION_OBSERVER')){$expected+='MGS_LOCAL_NATIVE_ACTIVATION_OBSERVER'}
        if (!$Background -or $SelectedProfile -cne 'Research' -or $enabled.Count -ne $expected.Count -or @($expected|Where-Object{!$enabled.Contains($_)}).Count) {
            throw 'Standing cycle observer requires exact Research/background ON stock46-only observer flags; append remains OFF'
        }
    }
    if($enabled.Contains('MGS_LOCAL_MGO_STANDING_LOOKUP_OBSERVER') -and !$enabled.Contains('MGS_LOCAL_MGO_STANDING_CYCLE_OBSERVER')){throw 'Lookup observer requires the exact standing-cycle profile.'}
    if($enabled.Contains('MGS_LOCAL_MGO_STANDING_FRAME_OBSERVER') -and (!$enabled.Contains('MGS_LOCAL_MGO_STANDING_CYCLE_OBSERVER') -or !$enabled.Contains('MGS_LOCAL_MGO_STANDING_LOOKUP_OBSERVER'))){throw 'Frame observer requires the exact standing-cycle and original-forward lookup observers.'}
    if ($enabled.Contains('MGS_LOCAL_BUDDY_CAPACITY_TEST') -and $enabled.Contains('MGS_LOCAL_GUN_PRELOAD_TEST')) {
        throw 'Buddy capacity cannot combine alternate archive catalogs'
    }
    $sorted = @($enabled | Sort-Object)
    $profileKey = $SelectedProfile.ToLowerInvariant()
    if ($enabled.Count -and $SelectedProfile -ne 'Release') { $profileKey += '-' + (Get-WorkerHash ($sorted -join "`n")).Substring(0, 12) }
    $profileKey += $(if ($Background) { '-worker-on' } else { '-worker-off' })
    $buildRoot = Join-Path $Root 'build'
    $cache = if ($Directory) { [IO.Path]::GetFullPath($Directory, $Root) }
        else { Join-Path $buildRoot ('worker-checks\' + $profileKey + '-' + $SelectedConfiguration.ToLowerInvariant()) }
    Assert-WorkerPath $cache $buildRoot
    $legacy = Join-Path $buildRoot 'native'
    if ($cache.Equals($legacy, [StringComparison]::OrdinalIgnoreCase) -or (Test-WorkerUnder $cache $legacy)) {
        throw 'Worker checks refuse the shared development build/native cache'
    }
    $runs = Join-Path $buildRoot 'worker-runs'
    if ($cache.Equals($runs, [StringComparison]::OrdinalIgnoreCase) -or (Test-WorkerUnder $cache $runs)) {
        throw 'Worker cache cannot overlap immutable run receipts'
    }
    $flags = [ordered]@{}
    foreach ($name in $known) { $flags[$name] = $(if ($enabled.Contains($name)) { 'ON' } else { 'OFF' }) }
    $flags.MGS_BACKGROUND_WORKER = $(if ($Background) { 'ON' } else { 'OFF' })
    return [pscustomobject][ordered]@{
        schema = 1; profile = $SelectedProfile; configuration = $SelectedConfiguration; profile_key = $profileKey
        project = $Root; build_directory = $cache; flags = $flags
        coordinator_lock = 'Local\mgs-coop-checks-' + (Get-WorkerHash ([IO.Path]::GetFullPath($Root).ToUpperInvariant())).Substring(0, 16)
        tests = @(Get-WorkerTests)
        excluded = @('lab_scripts_tests', 'friend_package_tests', 'probe_install_recovery')
        pure_input_script = 'tests\lab_scripts_tests.ps1 -InputOnly'
        retail_game_launch = $false; system_input = $false; installation = $false
        evidence = 'Automated fixture checks only; runtime worker and gameplay are not validated.'
    }
}
function Assert-WorkerOptions([string]$Root) {
    $source = [IO.File]::ReadAllText((Join-Path $Root 'CMakeLists.txt'))
    $declared = @([regex]::Matches($source, '(?m)^option\((MGS_LOCAL_[A-Z0-9_]+) [^\r\n]+\)') |
        ForEach-Object { $_.Groups[1].Value })
    $known = @(Get-WorkerExperiments)
    if ($declared.Count -ne $known.Count -or @($declared | Where-Object { $known -cnotcontains $_ }).Count) {
        throw 'Local experiment policy differs from CMake; review the runner allowlist before building'
    }
    if ($source -notmatch '(?m)^option\(MGS_BACKGROUND_WORKER ') { throw 'MGS_BACKGROUND_WORKER option is not declared' }
}
function Assert-WorkerCache([string]$Directory, $Flags) {
    $text = [IO.File]::ReadAllText((Join-Path $Directory 'CMakeCache.txt'))
    foreach ($name in $Flags.Keys) {
        if ($text -notmatch ('(?m)^' + [regex]::Escape($name) + ':BOOL=' + $Flags[$name] + '\r?$')) {
            throw "Unexpected cached option: $name (expected $($Flags[$name]))"
        }
    }
}
function Assert-WorkerCacheOwnership($Plan) {
    $marker = Join-Path $Plan.build_directory '.worker-check-profile.json'
    if (Test-Path -LiteralPath $marker -PathType Leaf) {
        $owner = [IO.File]::ReadAllText($marker) | ConvertFrom-Json
        if ($owner.project -ine $Plan.project -or $owner.profile_key -cne $Plan.profile_key -or
            $owner.configuration -cne $Plan.configuration) { throw 'Worker cache belongs to a different profile or checkout' }
    } elseif (Test-Path -LiteralPath (Join-Path $Plan.build_directory 'CMakeCache.txt')) {
        throw 'Worker checks cannot adopt an unowned existing CMake cache'
    } else {
        $null = New-Item -ItemType Directory -Path $Plan.build_directory -Force
        Write-WorkerJson $marker ([ordered]@{ schema=1; project=$Plan.project; profile_key=$Plan.profile_key; configuration=$Plan.configuration })
    }
}
function Assert-WorkerInventory {
    param($Inventory, [string]$Root, [string]$Directory, [string[]]$Allowed)
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $allow = [Collections.Generic.HashSet[string]]::new([string[]]$Allowed, [StringComparer]::Ordinal)
    $byName = @{}
    foreach ($test in @($Inventory.tests)) {
        if (!$seen.Add([string]$test.name)) { throw "Duplicate CTest name: $($test.name)" }
        $byName[$test.name] = $test
    }
    $luaAliases = @{
        partner_sync_composed_lua_check='partner_sync_lua_check'; world_sync_composed_lua_check='world_sync_lua_check'
        death_policy_composed_lua_check='death_policy_lua_check'; damage_parameters_composed_lua_check='damage_parameters_lua_check'
        character_composed_lua_check='character_lua_check'; character_game_lua_check='character_lua_check'
        player_package_mission_list_lua_check='player_package_lua_check'
    }
    $exeAliases = @{ four_player_lab='coop_lab'; probe_forwarding='probe_smoke'; partner_defer_pose_nested_foreign_tests='partner_defer_tests'; partner_defer_pose_held_generation_tests='partner_defer_tests';
        master_server_patch_retarget_tests='master_server_patch_tests'; master_server_patch_retarget_loopback_tests='master_server_patch_tests' }
    foreach ($name in $Allowed) {
        if (!$byName.ContainsKey($name)) { throw "Required audited test is missing: $name" }
        $test = $byName[$name]
        $command = @($test.command)
        if (!$command.Count) { throw "Missing audited command: $name" }
        if ($name -cin @('build_profile_tests', 'mgo_standing_cycle_session_tests', 'mgo_standing_frame_policy_tests', 'mgo_unique_rest_session_tests', 'worker_session_tests', 'worker_loading_tests', 'local_coop_windows_tests')) {
            if ([IO.Path]::GetFileName($command[0]) -cne 'pwsh.exe' -or $command.Count -ne 6 -or
                ($command[1..4] -join '|') -cne '-NoProfile|-ExecutionPolicy|Bypass|-File' -or
                [IO.Path]::GetFullPath($command[5]) -ine (Join-Path $Root ('tests\' + $name + '.ps1'))) {
                throw "Unaudited PowerShell command: $name"
            }
        } elseif ($name -ceq 'local_coop_session_tests') {
            if ([IO.Path]::GetFileName($command[0]) -cne 'pwsh.exe' -or $command.Count -ne 8 -or
                ($command[1..4] -join '|') -cne '-NoProfile|-ExecutionPolicy|Bypass|-File' -or
                [IO.Path]::GetFullPath($command[5]) -ine (Join-Path $Root 'tests\local_coop_session_tests.ps1') -or
                $command[6] -cne '-IniCheck' -or [IO.Path]::GetFullPath($command[7]) -ine (Join-Path $Directory 'lab_ini_check.exe')) {
                throw "Unaudited local-session fixture command: $name"
            }
        } elseif ($name -like '*_lua_check') {
            $scriptName = if ($luaAliases.ContainsKey($name)) { $luaAliases[$name] } else { $name }
            if ($command.Count -lt 2 -or [IO.Path]::GetFullPath($command[0]) -ine (Join-Path $Directory 'lua-fixture\lua51.exe') -or
                [IO.Path]::GetFullPath($command[1]) -ine (Join-Path $Root ('tests\' + $scriptName + '.lua'))) {
                throw "Unaudited Lua command: $name"
            }
        } else {
            $binaryName = if ($exeAliases.ContainsKey($name)) { $exeAliases[$name] } else { $name }
            if ([IO.Path]::GetFullPath($command[0]) -ine (Join-Path $Directory ($binaryName + '.exe'))) {
                throw "Unaudited fixture executable: $name"
            }
            if ($name -like '*_patch_check') {
                $outputs = @{
                    bootstrap_patch_check=@('Tpp-bootstrap.lua','Tpp-archive.lua'); host_trial_patch_check=@('TppMain-host-trial.lua')
                    mission_trace_patch_check=@('TppMain-trace.lua'); death_policy_patch_check=@('TppMission-death-policy.lua')
                    player_package_patch_check=@('TppPackList.lua'); damage_parameters_patch_check=@('DamageParameterTables.lua')
                }
                if (!$outputs.ContainsKey($name) -or $command.Count -ne (2 + $outputs[$name].Count) -or
                    !(Test-WorkerUnder $command[1] (Join-Path $Root 'build')) -or [IO.Path]::GetExtension($command[1]) -cne '.lua') {
                    throw "Unaudited patch input: $name"
                }
                for ($i=0; $i -lt $outputs[$name].Count; ++$i) {
                    if ([IO.Path]::GetFullPath($command[$i+2]) -ine (Join-Path $Directory ('lua-check\' + $outputs[$name][$i]))) {
                        throw "Foreign patch output: $name"
                    }
                }
            } elseif ($name -eq 'probe_forwarding') {
                if ($command.Count -ne 2 -or [IO.Path]::GetFullPath($command[1]) -ine (Join-Path $Directory 'dinput8.dll')) {
                    throw 'Proxy forwarding must load only this worker artifact'
                }
            } elseif ($exeAliases.ContainsKey($name) -and $name -like 'master_server_patch_retarget*') {
                $mode = if ($name -eq 'master_server_patch_retarget_tests') { 'retarget' } else { 'retarget_loopback' }
                if ($command.Count -ne 2 -or $command[1] -cne $mode) { throw "Unaudited patch fixture mode: $name" }
            } elseif ($name -eq 'partner_defer_pose_nested_foreign_tests') {
                if ($command.Count -ne 2 -or $command[1] -cne '--pose-nested-foreign') { throw 'Unexpected pose-window fixture mode' }
            } elseif ($name -eq 'partner_defer_pose_held_generation_tests') {
                if ($command.Count -ne 2 -or $command[1] -cne '--pose-held-gen-drift') { throw 'Unexpected held-generation fixture mode' }
            } elseif ($command.Count -ne 1) { throw "Unexpected fixture arguments: $name" }
        }
        foreach ($property in @($test.properties)) {
            if ($property.name -notin @('WORKING_DIRECTORY', 'FIXTURES_SETUP', 'FIXTURES_REQUIRED', 'FIXTURES_CLEANUP', 'DEPENDS',
                'DISABLED', 'TIMEOUT', 'LABELS', 'RUN_SERIAL', 'RESOURCE_LOCK', 'PROCESSORS')) {
                throw "Unaudited CTest property: $name/$($property.name)"
            }
            if ($property.name -eq 'WORKING_DIRECTORY' -and [IO.Path]::GetFullPath([string]$property.value) -ine $Directory -and
                !(Test-WorkerUnder ([string]$property.value) $Directory)) { throw "Foreign test working directory: $name" }
            if ($property.name -eq 'DISABLED' -and $property.value) { throw "Audited test is disabled: $name" }
            if ($property.name -eq 'DEPENDS') {
                foreach ($dependency in @($property.value)) {
                    if (!$allow.Contains([string]$dependency) -or !$byName.ContainsKey([string]$dependency)) {
                        throw "Unaudited ordered dependency: $dependency"
                    }
                }
            }
            if ($property.name -eq 'FIXTURES_REQUIRED') {
                foreach ($fixture in @($property.value)) {
                    $providers = @($Inventory.tests | Where-Object {
                        @($_.properties | Where-Object { $_.name -in @('FIXTURES_SETUP', 'FIXTURES_CLEANUP') -and @($_.value) -contains $fixture }).Count
                    })
                    if (!@($providers | Where-Object { @($_.properties | Where-Object { $_.name -eq 'FIXTURES_SETUP' -and @($_.value) -contains $fixture }).Count }).Count) {
                        throw "Missing fixture provider: $fixture"
                    }
                    foreach ($provider in $providers) {
                        if (!$allow.Contains([string]$provider.name)) { throw "Unaudited fixture dependency: $($provider.name)" }
                    }
                }
            }
        }
    }
}
function Write-WorkerJson([string]$Path, $Value) {
    $tmp = $Path + '.tmp'
    [IO.File]::WriteAllText($tmp, ($Value | ConvertTo-Json -Depth 16) + "`n", [Text.UTF8Encoding]::new($false))
    [IO.File]::Move($tmp, $Path, $true)
}
function Invoke-WorkerCommand {
    param($Context, [string]$Executable, [string[]]$Arguments, [string]$Tag, [int]$Timeout)
    $index = $Context.receipt.commands.Count + 1
    $prefix = '{0:D3}-{1}' -f $index, $Tag
    $stdout = Join-Path $Context.run_directory ($prefix + '.stdout.log')
    $stderr = Join-Path $Context.run_directory ($prefix + '.stderr.log')
    $row = [ordered]@{ tag=$Tag; executable=$Executable; arguments=$Arguments; started_utc=[DateTime]::UtcNow.ToString('o');
        stdout=$stdout; stderr=$stderr; exit_code=$null; timed_out=$false; finished_utc=$null; pid=$null }
    $Context.receipt.commands.Add($row)
    & $Context.write_json $Context.receipt_path $Context.receipt
    Write-Host "Worker check: $Tag"
    $psi = [Diagnostics.ProcessStartInfo]::new()
    $psi.FileName = $Executable; $psi.WorkingDirectory = $Context.project_root
    $psi.UseShellExecute = $false; $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true
    foreach ($argument in $Arguments) { $psi.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::new(); $process.StartInfo = $psi
    $output = ''; $errorText = ''
    try {
        if (!$process.Start()) { throw "Cannot start audited command: $Tag" }
        $row.pid = $process.Id
        $row.process_started_utc = $process.StartTime.ToUniversalTime().ToString('o')
        $outTask = $process.StandardOutput.ReadToEndAsync(); $errTask = $process.StandardError.ReadToEndAsync()
        if (!$process.WaitForExit($Timeout * 1000)) { $row.timed_out = $true; $process.Kill($true); $process.WaitForExit() }
        $output = $outTask.GetAwaiter().GetResult(); $errorText = $errTask.GetAwaiter().GetResult()
        $row.exit_code = $process.ExitCode
        if ($row.timed_out) { throw "Audited command timed out: $Tag" }
        if ($row.exit_code -ne 0) { throw "Audited command failed: $Tag (exit $($row.exit_code)); see $stderr" }
    } finally {
        if ($row.pid -and !$process.HasExited) { $process.Kill($true); $null = $process.WaitForExit(5000) }
        [IO.File]::WriteAllText($stdout, $output, [Text.UTF8Encoding]::new($false))
        [IO.File]::WriteAllText($stderr, $errorText, [Text.UTF8Encoding]::new($false))
        $row.finished_utc = [DateTime]::UtcNow.ToString('o')
        & $Context.write_json $Context.receipt_path $Context.receipt
        $process.Dispose()
    }
    return [pscustomobject]@{ Output=$output; Error=$errorText; ExitCode=$row.exit_code }
}
function New-WorkerCmakeOverride($Context) {
    # tools/build.ps1 has its own script scope. Capture the owner's explicit
    # context before its nested cmake calls, including command/JSON bodies.
    return {
        $nativeArguments = @($args)
        $tag = if ($nativeArguments[0] -eq '-S') { 'configure-bootstrap' } else { 'build-loader-bootstrap' }
        if ($nativeArguments[0] -eq '--build' -and $nativeArguments[-1] -eq '--parallel') { $nativeArguments += "$($Context.build_jobs)" }
        $null = & $Context.invoke_command $Context $Context.cmake_path $nativeArguments $tag $Context.command_timeout
        $global:LASTEXITCODE = 0
    }.GetNewClosure()
}

$projectRoot = [IO.Path]::GetFullPath((Split-Path -Parent (Split-Path -Parent $PSScriptRoot)))
$plan = Get-WorkerPlan $projectRoot $Profile $Configuration $EnableExperiment $EnableBackgroundWorker.IsPresent $BuildDirectory
if ($PlanOnly) { $plan | ConvertTo-Json -Depth 8; return }
Assert-WorkerOptions $projectRoot
$cmakePath = (Get-Command cmake -CommandType Application -ErrorAction Stop).Source
$ctestPath = (Get-Command ctest -CommandType Application -ErrorAction Stop).Source
$pwshPath = (Get-Process -Id $PID).Path
$runRoot = Join-Path $projectRoot 'build\worker-runs'
$runId = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N')
$runDirectory = Join-Path $runRoot $runId
Assert-WorkerPath $runDirectory (Join-Path $projectRoot 'build')
$null = New-Item -ItemType Directory -Path $runDirectory
$receiptPath = Join-Path $runDirectory 'receipt.json'
$receipt = [ordered]@{ schema=1; run_id=$runId; plan=$plan; status='waiting_for_coordinator';
    started_utc=[DateTime]::UtcNow.ToString('o'); finished_utc=$null; failure=$null;
    commands=[Collections.Generic.List[object]]::new(); source_hashes=[ordered]@{}; artifacts=[ordered]@{} }
[IO.File]::WriteAllText((Join-Path $runDirectory '.worker-owner'), $runId, [Text.UTF8Encoding]::new($false))
Write-WorkerJson $receiptPath $receipt
$workerCommandContext = [pscustomobject]@{
    receipt=$receipt; receipt_path=$receiptPath; run_directory=$runDirectory; project_root=$projectRoot
    write_json=${function:Write-WorkerJson}; invoke_command=${function:Invoke-WorkerCommand}
    cmake_path=$cmakePath; build_jobs=$BuildJobs; command_timeout=$CommandTimeoutSec
}
$mutex = [Threading.Mutex]::new($false, $plan.coordinator_lock)
$owned = $false
$savedEnvironment = @{}
foreach ($name in @('PATH', 'INCLUDE', 'LIB')) { $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
try {
    try { $owned = $mutex.WaitOne([TimeSpan]::FromSeconds($LockTimeoutSec)) }
    catch [Threading.AbandonedMutexException] {
        $mutex.ReleaseMutex()
        throw 'Previous coordinator abandoned its lock; audit leftover build processes before retrying'
    }
    if (!$owned) { throw 'Worker check coordinator lock timed out' }
    Assert-WorkerCacheOwnership $plan
    $receipt.status = 'running'
    $sourceSnapshot = Get-WorkerSourceHashes $projectRoot
    $receipt.source_hashes = $sourceSnapshot.hashes
    $receipt.source_dependencies = $sourceSnapshot.dependencies
    Write-WorkerJson $receiptPath $receipt
    Set-Item -Path Function:cmake -Value (New-WorkerCmakeOverride $workerCommandContext)
    & (Join-Path $projectRoot 'tools\build.ps1') -Configuration $Configuration -BuildOnly -DisableLocalExperiments -BuildDirectory $plan.build_directory
    # Configure every option explicitly; research/worker ON cannot leak from a
    # previous run and compilation below uses only this final profile.
    $flagArguments = @('-S', $projectRoot, '-B', $plan.build_directory) + @($plan.flags.Keys | ForEach-Object { '-D' + $_ + '=' + $plan.flags[$_] })
    $null = Invoke-WorkerCommand $workerCommandContext $cmakePath $flagArguments 'configure-profile' $CommandTimeoutSec
    Assert-WorkerCache $plan.build_directory $plan.flags
    $null = Invoke-WorkerCommand $workerCommandContext $cmakePath @('--build', $plan.build_directory, '--parallel', "$BuildJobs") 'build-fixtures' $CommandTimeoutSec
    $inventoryResult = Invoke-WorkerCommand $workerCommandContext $ctestPath @('--test-dir', $plan.build_directory, '--show-only=json-v1') 'inventory' 60
    $inventory = $inventoryResult.Output | ConvertFrom-Json
    Assert-WorkerInventory $inventory $projectRoot $plan.build_directory $plan.tests
    Write-WorkerJson (Join-Path $runDirectory 'ctest-inventory.json') $inventory
    $regex = '^(' + (($plan.tests | ForEach-Object { [regex]::Escape($_) }) -join '|') + ')$'
    # CTest retains setup/cleanup scheduling; the dependency audit above refuses
    # any provider outside the allowlist before CTest can execute it.
    $selectedResult = Invoke-WorkerCommand $workerCommandContext $ctestPath @('--test-dir', $plan.build_directory, '--show-only=json-v1', '-R', $regex) 'selected-inventory' 60
    $selected = $selectedResult.Output | ConvertFrom-Json
    if (@($selected.tests).Count -ne $plan.tests.Count -or @($selected.tests | Where-Object { $plan.tests -cnotcontains $_.name }).Count) {
        throw 'CTest selected an unexpected test/dependency set'
    }
    Write-WorkerJson (Join-Path $runDirectory 'ctest-selected.json') $selected
    $null = Invoke-WorkerCommand $workerCommandContext $ctestPath @('--test-dir', $plan.build_directory, '-R', $regex, '--output-on-failure', '-j', '1',
        '--timeout', "$TestTimeoutSec", '--output-junit', (Join-Path $runDirectory 'ctest-results.xml')) 'ctest-audited' $CommandTimeoutSec
    $null = Invoke-WorkerCommand $workerCommandContext $pwshPath @('-NoProfile', '-NonInteractive', '-File', (Join-Path $projectRoot 'tests\lab_scripts_tests.ps1'), '-InputOnly') 'pure-input-fixture' $TestTimeoutSec
    $null = Invoke-WorkerCommand $workerCommandContext $pwshPath @('-NoProfile', '-NonInteractive', '-File', (Join-Path $projectRoot 'tests\worker_checks_tests.ps1')) 'worker-policy-fixture' $TestTimeoutSec
    Assert-WorkerCache $plan.build_directory $plan.flags
    $finalSnapshot = Get-WorkerSourceHashes $projectRoot
    Assert-WorkerSourceUnchanged $sourceSnapshot $finalSnapshot
    $loader = Join-Path $plan.build_directory 'dinput8.dll'
    $receipt.artifacts.loader_sha256 = (Get-FileHash -LiteralPath $loader -Algorithm SHA256).Hash
    [IO.File]::Copy($loader, (Join-Path $runDirectory 'dinput8.dll'), $false)
    [IO.File]::Copy((Join-Path $plan.build_directory 'CMakeCache.txt'), (Join-Path $runDirectory 'CMakeCache.txt'), $false)
    $receipt.status = 'passed'
} catch {
    $receipt.status = 'failed'; $receipt.failure = $_.Exception.Message
    throw
} finally {
    foreach ($name in $savedEnvironment.Keys) { [Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name], 'Process') }
    $receipt.finished_utc = [DateTime]::UtcNow.ToString('o')
    try { Write-WorkerJson $receiptPath $receipt }
    finally {
        if ($owned) { $mutex.ReleaseMutex() }
        $mutex.Dispose()
        Write-Output "Worker check receipt: $receiptPath"
    }
}
