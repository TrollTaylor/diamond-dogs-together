# Lua 5.1 fixture interpreter, Lua fragment checks and exact-source patch checks.
# Included at the end of the top-level CMakeLists.txt (after the WIN32 block
# that defines mgs_bootstrap_patch and the *_patch_check executables).
#
# Nothing here can fail a configure: a missing input only skips the tests that
# need it and prints a STATUS line naming them.
#
# Inputs (each is a cache variable seeded from the environment variable of the
# same name, so tools/build.ps1 picks them up without new parameters):
#   MGS_LUA_SOURCE_DIR  pinned Lua 5.1.5 source tree (contains src/lua.h).
#                       Default <source>/work/lua-5.1.5.
#   MGS_GAME_LUA_DIR    folder holding the extracted game scripts in the
#                       <inspection>/package-00.lua layout. Default <source>/build.
# Resolution per configure: the cache value, then the environment variable,
# then MGS_MAIN_CHECKOUT_FALLBACK (when it exists; set it empty to disable).
# Game inputs must also match the SHA-256 that the native exact-source gates
# pin; a missing or different file skips only the tests that need it.

set(MGS_MAIN_CHECKOUT_FALLBACK "" CACHE PATH
  "Main checkout whose work/lua-5.1.5 and build/ inputs are used when the configured folders lack them (empty disables)")

if(DEFINED ENV{MGS_LUA_SOURCE_DIR} AND NOT "$ENV{MGS_LUA_SOURCE_DIR}" STREQUAL "")
  set(_mgs_default "$ENV{MGS_LUA_SOURCE_DIR}")
else()
  set(_mgs_default "${CMAKE_CURRENT_SOURCE_DIR}/work/lua-5.1.5")
endif()
set(MGS_LUA_SOURCE_DIR "${_mgs_default}" CACHE PATH "Pinned Lua 5.1.5 source tree used to build the lua51 test interpreter")
if(DEFINED ENV{MGS_GAME_LUA_DIR} AND NOT "$ENV{MGS_GAME_LUA_DIR}" STREQUAL "")
  set(_mgs_default "$ENV{MGS_GAME_LUA_DIR}")
else()
  set(_mgs_default "${CMAKE_CURRENT_SOURCE_DIR}/build")
endif()
set(MGS_GAME_LUA_DIR "${_mgs_default}" CACHE PATH "Folder with extracted game Lua inputs (<inspection>/package-00.lua)")

# --- Lua 5.1.5 interpreter --------------------------------------------------
set(_mgs_lua_sources lapi.c lauxlib.c lbaselib.c lcode.c ldblib.c ldebug.c ldo.c ldump.c
  lfunc.c lgc.c linit.c liolib.c llex.c lmathlib.c lmem.c loadlib.c lobject.c lopcodes.c
  loslib.c lparser.c lstate.c lstring.c lstrlib.c ltable.c ltablib.c ltm.c lua.c lundump.c
  lvm.c lzio.c)
set(_mgs_lua_candidates "${MGS_LUA_SOURCE_DIR}")
if(DEFINED ENV{MGS_LUA_SOURCE_DIR} AND NOT "$ENV{MGS_LUA_SOURCE_DIR}" STREQUAL "")
  list(APPEND _mgs_lua_candidates "$ENV{MGS_LUA_SOURCE_DIR}")
endif()
if(MGS_MAIN_CHECKOUT_FALLBACK)
  list(APPEND _mgs_lua_candidates "${MGS_MAIN_CHECKOUT_FALLBACK}/work/lua-5.1.5")
endif()
set(MGS_LUA_EFFECTIVE_DIR "")
foreach(_mgs_dir IN LISTS _mgs_lua_candidates)
  if(MGS_LUA_EFFECTIVE_DIR OR NOT EXISTS "${_mgs_dir}/src/lua.h")
    continue()
  endif()
  file(STRINGS "${_mgs_dir}/src/lua.h" _mgs_release REGEX "^#define[ \t]+LUA_RELEASE[ \t]+\"Lua 5\\.1\\.5\"")
  if(NOT _mgs_release)
    message(STATUS "Lua checks: ${_mgs_dir} is not Lua 5.1.5; ignored")
    continue()
  endif()
  set(_mgs_complete TRUE)
  foreach(_mgs_file IN LISTS _mgs_lua_sources)
    if(NOT EXISTS "${_mgs_dir}/src/${_mgs_file}")
      set(_mgs_complete FALSE)
    endif()
  endforeach()
  if(_mgs_complete)
    set(MGS_LUA_EFFECTIVE_DIR "${_mgs_dir}")
  else()
    message(STATUS "Lua checks: ${_mgs_dir} is missing Lua 5.1.5 sources; ignored")
  endif()
endforeach()

set(_mgs_registered "")
set(_mgs_skipped "")
if(MGS_LUA_EFFECTIVE_DIR)
  enable_language(C)
  list(TRANSFORM _mgs_lua_sources PREPEND "${MGS_LUA_EFFECTIVE_DIR}/src/" OUTPUT_VARIABLE _mgs_lua_paths)
  add_executable(lua51 ${_mgs_lua_paths})
  target_include_directories(lua51 PRIVATE "${MGS_LUA_EFFECTIVE_DIR}/src")
  target_compile_definitions(lua51 PRIVATE _CRT_SECURE_NO_WARNINGS)
  if(UNIX)
    # lmathlib, lvm and lobject call floor, pow and fmod from libm.
    target_link_libraries(lua51 PRIVATE m)
  endif()
  set_target_properties(lua51 PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/lua-fixture")
  message(STATUS "Lua checks: building lua51 from ${MGS_LUA_EFFECTIVE_DIR}")
else()
  message(STATUS "Lua checks: SKIPPED all Lua 5.1 tests; no Lua 5.1.5 source in: ${_mgs_lua_candidates}")
endif()

# Registers a Lua check script (tests/<name>.lua) with absolute arguments.
function(mgs_add_lua_check name)
  if(TARGET lua51)
    add_test(NAME ${name} COMMAND lua51 "${CMAKE_CURRENT_SOURCE_DIR}/tests/${name}.lua" ${ARGN})
    set(_mgs_registered ${_mgs_registered} ${name} PARENT_SCOPE)
  else()
    set(_mgs_skipped ${_mgs_skipped} ${name} PARENT_SCOPE)
  endif()
endfunction()

set(_mgs_coop "${CMAKE_CURRENT_SOURCE_DIR}/lua/coop")
mgs_add_lua_check(host_admission_lua_check "${_mgs_coop}/host_admission.lua")
mgs_add_lua_check(equipment_allocation_lua_check "${_mgs_coop}/equipment_loading.lua")
mgs_add_lua_check(equipment_stock_preload_lua_check "${_mgs_coop}/equipment_stock_preload.lua")
mgs_add_lua_check(equipment_observation_lua_check "${_mgs_coop}/equipment_observation.lua")
mgs_add_lua_check(session_lifecycle_lua_check "${_mgs_coop}/session_lifecycle.lua")
mgs_add_lua_check(client_join_lua_check "${_mgs_coop}/client_join.lua")
mgs_add_lua_check(session_feedback_lua_check "${_mgs_coop}/session_feedback.lua")
mgs_add_lua_check(damage_parameters_lua_check "${_mgs_coop}/damage_parameters.lua")
mgs_add_lua_check(damage_lifecycle_lua_check "${_mgs_coop}/damage_lifecycle.lua")
mgs_add_lua_check(partner_sync_lua_check "${_mgs_coop}/partner_sync.lua")
mgs_add_lua_check(partner_actions_lua_check "${_mgs_coop}/partner_actions.lua")
mgs_add_lua_check(debug_menu_lua_check "${_mgs_coop}/debug_menu.lua" "${_mgs_coop}/debug_equipment.lua" "${_mgs_coop}/debug_enemy.lua"
  "${_mgs_coop}/debug_player.lua" "${_mgs_coop}/debug_spawn.lua")
# World sync W0-W2 (docs/world-sync-protocol.md): the seven chunks, in splice order.
set(_mgs_world_chunks "${_mgs_coop}/world_sync.lua" "${_mgs_coop}/world_enemy.lua" "${_mgs_coop}/world_outcome.lua"
  "${_mgs_coop}/world_phase.lua" "${_mgs_coop}/world_env.lua" "${_mgs_coop}/world_probe.lua" "${_mgs_coop}/world_goto.lua")
mgs_add_lua_check(world_sync_lua_check ${_mgs_world_chunks})
mgs_add_lua_check(world_downed_lua_check "${_mgs_coop}/world_downed.lua" "${_mgs_coop}/world_friendly.lua" "${_mgs_coop}/world_partner_sleep.lua" "${_mgs_coop}/world_sleep_clock.lua")
mgs_add_lua_check(world_revive_input_lua_check "${_mgs_coop}/world_downed.lua" "${_mgs_coop}/world_revive_input.lua" "${_mgs_coop}/world_sync.lua")
mgs_add_lua_check(world_buddy_lua_check "${_mgs_coop}/world_buddy.lua" "${_mgs_coop}/debug_buddy.lua")
mgs_add_lua_check(world_ride_pose_lua_check "${_mgs_coop}/world_ride_pose.lua")
mgs_add_lua_check(world_horse_cargo_lua_check "${_mgs_coop}/world_horse_cargo.lua")
mgs_add_lua_check(world_buddy_ride_lua_check "${_mgs_coop}/world_buddy_ride.lua")
mgs_add_lua_check(debug_buddy_lua_check "${_mgs_coop}/debug_buddy.lua" "${_mgs_coop}/debug_menu.lua")
mgs_add_lua_check(debug_vehicle_lua_check "${_mgs_coop}/debug_vehicle.lua")
mgs_add_lua_check(world_follow_lua_check "${_mgs_coop}/world_follow.lua" "${_mgs_coop}/world_follow_seed.lua")
mgs_add_lua_check(world_goto_lua_check "${_mgs_coop}/world_goto.lua")
mgs_add_lua_check(world_align_lua_check "${_mgs_coop}/world_align.lua" "${_mgs_coop}/world_enemy.lua"
  "${_mgs_coop}/world_outcome.lua" "${_mgs_coop}/world_phase.lua" "${_mgs_coop}/world_recovery.lua" "${_mgs_coop}/world_probe.lua"
  ${_mgs_buddy_ride_composed} "${_mgs_coop}/world_place.lua" "${_mgs_coop}/world_place_guest.lua")

# --- Extracted game inputs ----------------------------------------------------
# Finds <relative> (or an alternative relative path given after <sha256>)
# under the configured, environment and main-checkout game folders; accepts
# only the exact source the native gate pins.
set(_mgs_game_candidates "${MGS_GAME_LUA_DIR}")
if(DEFINED ENV{MGS_GAME_LUA_DIR} AND NOT "$ENV{MGS_GAME_LUA_DIR}" STREQUAL "")
  list(APPEND _mgs_game_candidates "$ENV{MGS_GAME_LUA_DIR}")
endif()
if(MGS_MAIN_CHECKOUT_FALLBACK)
  list(APPEND _mgs_game_candidates "${MGS_MAIN_CHECKOUT_FALLBACK}/build")
endif()
function(mgs_find_game_input out relative sha256)
  set(found "")
  foreach(path IN ITEMS ${relative} ${ARGN})
    foreach(dir IN LISTS _mgs_game_candidates)
      if(NOT found AND EXISTS "${dir}/${path}")
        file(SHA256 "${dir}/${path}" actual)
        if(actual STREQUAL sha256)
          set(found "${dir}/${path}")
        else()
          message(STATUS "Lua checks: ${dir}/${path} has SHA-256 ${actual}, expected ${sha256}; ignored")
        endif()
      endif()
    endforeach()
  endforeach()
  if(NOT found)
    set(paths ${relative} ${ARGN})
    list(JOIN paths " or " paths)
    message(STATUS "Lua checks: game input ${paths} not found in: ${_mgs_game_candidates}")
  endif()
  set(${out} "${found}" PARENT_SCOPE)
endfunction()
mgs_find_game_input(_mgs_tpp "startup-script-inspection-20260909/package-00.lua"
  ac2ca8bfaf622dcb1b5e11570829b4d1e9ce98b22e4a1624562a59ff2a90572b)
mgs_find_game_input(_mgs_tpp_main "player-allocation-lifecycle-20260909/package-00.lua"
  6542d997c3f29742ff7ea0587f7f307794586a9b7a8ee3114b58e79e7f0b6103)
mgs_find_game_input(_mgs_pack_list "player-load-inspection-20260909/package-00.lua"
  85120c16a6cb27d8a732763b023f45125edd70201bef182695856756ee4cb00e)
mgs_find_game_input(_mgs_damage_tables "damage-parameters-current-20260910/package-00.lua"
  e88ed1e39fbb689bcddb75af115351245571ecf2a778675ab157d6c4023d0fa3)
# TppMission.lua (104,102 bytes, docs/respawn.md); the startup inspection
# extracted the same file as package-02.lua.
mgs_find_game_input(_mgs_tpp_mission "respawn-scripts-20260910/package-00.lua"
  e99e6addd4c5135fb2d77ab58632d5e349a626eb7f10ea38301372d127f7404a
  "startup-script-inspection-20260909/package-02.lua")
mgs_find_game_input(_mgs_world_tpp_player "startup-script-inspection-20260909/package-01.lua"
  7407bd41dd82913edfe9ef7fbb344f443adc7399d42f86c7798ba6b02c94e5b9)
if(_mgs_tpp_mission AND _mgs_tpp_main AND _mgs_world_tpp_player)
  mgs_add_lua_check(world_follow_game_lua_check "${_mgs_coop}/world_follow.lua" "${_mgs_tpp_mission}" "${_mgs_tpp_main}" "${_mgs_coop}/world_follow_seed.lua" "${_mgs_world_tpp_player}")
endif()
mgs_find_game_input(_mgs_debug_clock "debug-api-source-audit-20261006/lib/TppClock.lua"
  12a3f7b1af62af4d74149aa7220af56b13c8502ea6b28d714f1875467794fc32)
mgs_find_game_input(_mgs_debug_weather "debug-api-source-audit-20261006/lib/TppWeather.lua"
  1042bfb11197d4778a741329abd5898c5d67907a6dd40e3c367a8e029530471d)
mgs_find_game_input(_mgs_debug_enemy "debug-api-source-audit-20261006/lib/TppEnemy.lua"
  f6a55d989a910cac907f57abf78d5a26ef2c580b7d84dda68c8c01ddd2c1a844)
mgs_find_game_input(_mgs_debug_quest "debug-api-source-audit-20261006/lib/TppQuest.lua"
  6d3e08b1817b79d1022cc471616a85f9e653fc0286ad54ef5bf5f933e85c9e34)
mgs_find_game_input(_mgs_debug_weapon_sequence "debug-weapon-source-audit-20261006/package-01_fpkd/Assets/tpp/level/mission2/story/s10260/s10260_sequence.lua"
  59c4dd532b25a09b3ba5dc495a729d39aa014a3548b6d0f19c567d82f2813356)
mgs_find_game_input(_mgs_debug_spawn_sequence "debug-spawn-source-audit-20261006/package-02_fpkd/Assets/tpp/level/mission2/story/s10121/s10121_sequence.lua"
  aff5b23d2f4630c90879b455d33374a8d0b7362e4ebffaba060ec22be949bebc)
mgs_find_game_input(_mgs_debug_active_slot "debug-spawn-source-audit-20261006/package-00_fpkd/Assets/tpp/level/mission2/story/s10030/s10030_sequence.lua"
  3616b5437ef537e63f925d90fd84922b982f7e4aebf710e1d25c4da105821776)
mgs_find_game_input(_mgs_debug_tpp_debug "debug-api-source-audit-20261006/lib/TppDebug.lua"
  974cebba26b12e1e504776133a5a8ea31a816da46191e82118769c9ec212d50a)
mgs_find_game_input(_mgs_debug_checkpoint "debug-api-source-audit-20261006/lib/TppCheckPoint.lua"
  55bbca7895395d9413d22a3a3257779f553b57b60a4f41e9a1a472802ad5b36f)
if(_mgs_debug_clock AND _mgs_debug_weather)
  mgs_add_lua_check(world_env_game_lua_check "${_mgs_coop}/world_env.lua" "${_mgs_debug_clock}" "${_mgs_debug_weather}")
endif()
if(_mgs_world_tpp_player AND _mgs_tpp_mission AND _mgs_debug_clock AND _mgs_debug_weather AND
   _mgs_debug_enemy AND _mgs_debug_quest AND _mgs_debug_weapon_sequence AND _mgs_debug_spawn_sequence AND _mgs_debug_active_slot AND _mgs_debug_tpp_debug AND _mgs_debug_checkpoint)
  mgs_add_lua_check(debug_menu_game_lua_check "${_mgs_coop}/debug_menu.lua" "${_mgs_coop}/debug_equipment.lua"
    "${_mgs_world_tpp_player}" "${_mgs_debug_clock}" "${_mgs_debug_weather}" "${_mgs_debug_enemy}"
    "${_mgs_debug_quest}" "${_mgs_debug_weapon_sequence}" "${_mgs_debug_spawn_sequence}" "${_mgs_tpp_mission}" "${_mgs_debug_active_slot}" "${_mgs_coop}/debug_enemy.lua" "${_mgs_debug_tpp_debug}" "${_mgs_debug_checkpoint}"
    "${_mgs_coop}/debug_player.lua" "${_mgs_coop}/debug_spawn.lua")
endif()

if(TARGET mgs_bootstrap_patch)
  add_executable(damage_parameters_patch_check tests/damage_parameters_patch_check.cpp)
  target_link_libraries(damage_parameters_patch_check PRIVATE mgs_bootstrap_patch)
  target_compile_options(damage_parameters_patch_check PRIVATE /W4 /WX)
  add_executable(death_policy_patch_check tests/death_policy_patch_check.cpp)
  target_link_libraries(death_policy_patch_check PRIVATE mgs_bootstrap_patch)
  target_compile_options(death_policy_patch_check PRIVATE /W4 /WX)
endif()

# Each patch check composes the actual extracted source into the build tree
# (a CTest fixture), and the matching Lua check executes that composition.
set(_mgs_out "${CMAKE_CURRENT_BINARY_DIR}/lua-check")
file(MAKE_DIRECTORY "${_mgs_out}")
function(mgs_add_patch_check name input)
  if(TARGET ${name} AND input)
    add_test(NAME ${name} COMMAND ${name} "${input}" ${ARGN})
    set_tests_properties(${name} PROPERTIES FIXTURES_SETUP ${name}_output)
    set(_mgs_registered ${_mgs_registered} ${name} PARENT_SCOPE)
  else()
    set(_mgs_skipped ${_mgs_skipped} ${name} PARENT_SCOPE)
  endif()
endfunction()
# Registers tests/<script>.lua as test <name>; it needs one game input and,
# when <fixture> is not empty, the output of that registered patch check.
function(mgs_add_game_lua_check name script input fixture)
  set(ok FALSE)
  if(TARGET lua51 AND input)
    set(ok TRUE)
    if(fixture)
      if(NOT TEST ${fixture})
        set(ok FALSE)
      endif()
    endif()
  endif()
  if(ok)
    add_test(NAME ${name} COMMAND lua51 "${CMAKE_CURRENT_SOURCE_DIR}/tests/${script}.lua" ${ARGN})
    if(fixture)
      set_tests_properties(${name} PROPERTIES FIXTURES_REQUIRED ${fixture}_output)
    endif()
    set(_mgs_registered ${_mgs_registered} ${name} PARENT_SCOPE)
  else()
    set(_mgs_skipped ${_mgs_skipped} ${name} PARENT_SCOPE)
  endif()
endfunction()

mgs_add_patch_check(bootstrap_patch_check "${_mgs_tpp}" "${_mgs_out}/Tpp-bootstrap.lua" "${_mgs_out}/Tpp-archive.lua")
mgs_add_game_lua_check(bootstrap_lua_check bootstrap_lua_check "${_mgs_tpp}" bootstrap_patch_check
  "${_mgs_tpp}" "${_mgs_out}/Tpp-bootstrap.lua" "${_mgs_out}/Tpp-archive.lua" "${_mgs_coop}/archive_probe.lua")
mgs_add_patch_check(host_trial_patch_check "${_mgs_tpp_main}" "${_mgs_out}/TppMain-host-trial.lua")
mgs_add_game_lua_check(host_trial_lua_check host_trial_lua_check "${_mgs_tpp_main}" host_trial_patch_check
  "${_mgs_tpp_main}" "${_mgs_out}/TppMain-host-trial.lua")
# The offline partner chunk inside that complete composition, run at 30050.
mgs_add_game_lua_check(partner_sync_composed_lua_check partner_sync_lua_check "${_mgs_tpp_main}" host_trial_patch_check
  "${_mgs_coop}/partner_sync.lua" "${_mgs_out}/TppMain-host-trial.lua")
# The world chunks inside that composition: spliced once, in order, inert with a literal CFG.
set(_mgs_buddy_ride_composed)
if(MGS_LOCAL_BUDDY_CAPACITY_TEST)
  list(APPEND _mgs_buddy_ride_composed "${_mgs_coop}/world_buddy_ride.lua")
endif()
mgs_add_game_lua_check(world_sync_composed_lua_check world_sync_lua_check "${_mgs_tpp_main}" host_trial_patch_check
  ${_mgs_world_chunks} "${_mgs_out}/TppMain-host-trial.lua" "${_mgs_coop}/world_follow_seed.lua" "${_mgs_coop}/world_follow.lua" "${_mgs_coop}/world_align.lua" "${_mgs_coop}/world_recovery.lua"
  "${_mgs_coop}/world_downed.lua" "${_mgs_coop}/world_sleep_clock.lua" "${_mgs_coop}/world_partner_sleep.lua" "${_mgs_coop}/world_revive_input.lua" "${_mgs_coop}/world_friendly.lua" "${_mgs_coop}/world_buddy.lua" "${_mgs_coop}/world_ride_pose.lua" "${_mgs_coop}/world_horse_cargo.lua"
  ${_mgs_buddy_ride_composed} "${_mgs_coop}/world_place.lua" "${_mgs_coop}/world_place_guest.lua")
mgs_add_patch_check(mission_trace_patch_check "${_mgs_tpp_main}" "${_mgs_out}/TppMain-trace.lua")
# The documented two-argument form runs the fragment; the optional third
# argument also executes the composed TppMain-trace.lua when it is built.
if(TEST mission_trace_patch_check)
  mgs_add_game_lua_check(mission_trace_lua_check mission_trace_lua_check "${_mgs_tpp_main}" mission_trace_patch_check
    "${_mgs_coop}/mission_trace.lua" "${_mgs_tpp_main}" "${_mgs_out}/TppMain-trace.lua")
else()
  mgs_add_game_lua_check(mission_trace_lua_check mission_trace_lua_check "${_mgs_tpp_main}" ""
    "${_mgs_coop}/mission_trace.lua" "${_mgs_tpp_main}")
endif()
# TppMission.OnPlayerDead death-policy wrapper: fragment semantics need only
# the interpreter; the composition needs the pinned TppMission.lua.
mgs_add_lua_check(death_policy_lua_check "${_mgs_coop}/death_policy.lua")
mgs_add_patch_check(death_policy_patch_check "${_mgs_tpp_mission}" "${_mgs_out}/TppMission-death-policy.lua")
mgs_add_game_lua_check(death_policy_composed_lua_check death_policy_lua_check "${_mgs_tpp_mission}" death_policy_patch_check
  "${_mgs_coop}/death_policy.lua" "${_mgs_tpp_mission}" "${_mgs_out}/TppMission-death-policy.lua")
mgs_add_patch_check(player_package_patch_check "${_mgs_pack_list}" "${_mgs_out}/TppPackList.lua")
mgs_add_game_lua_check(player_package_lua_check player_package_lua_check "${_mgs_pack_list}" player_package_patch_check
  "${_mgs_pack_list}" "${_mgs_out}/TppPackList.lua")
# The same checks plus the real TppMissionList.GetMissionPackagePath (30050
# layouts 1..3, DemoStage, BattleHanger 0..5, 30010 and its Volgin demo, 30020)
# through the composed TppPackList.lua; registered only when that source is found.
mgs_find_game_input(_mgs_mission_list "mission-list-inspection-20260909/package-00.lua"
  3c250e19fc5ba0ccc14b192daef2860555e6cadf0f44039876fc3e8687f60e1d)
if(_mgs_mission_list)
  mgs_add_game_lua_check(player_package_mission_list_lua_check player_package_lua_check "${_mgs_pack_list}" player_package_patch_check
    "${_mgs_pack_list}" "${_mgs_out}/TppPackList.lua" "${_mgs_mission_list}")
else()
  list(APPEND _mgs_skipped player_package_mission_list_lua_check)
endif()
# Optional parity reference: Dynamite c90f252 standalone/DamageParameterTables.lua
# from an upstream checkout (work/upstream-dynamite, gitignored).
set(_mgs_dynamite_candidates "${CMAKE_CURRENT_SOURCE_DIR}/work/upstream-dynamite")
if(MGS_MAIN_CHECKOUT_FALLBACK)
  list(APPEND _mgs_dynamite_candidates "${MGS_MAIN_CHECKOUT_FALLBACK}/work/upstream-dynamite")
endif()
set(_mgs_game_candidates ${_mgs_dynamite_candidates})
mgs_find_game_input(_mgs_dynamite_damage "moddata/standalone/DamageParameterTables.lua"
  8990a541d6c417e0a0738fd19c21b8a95677b789e5b6a9faa6283b35a854f3eb)
mgs_add_patch_check(damage_parameters_patch_check "${_mgs_damage_tables}" "${_mgs_out}/DamageParameterTables.lua")
mgs_add_game_lua_check(damage_parameters_composed_lua_check damage_parameters_lua_check "${_mgs_damage_tables}" damage_parameters_patch_check
  "${_mgs_coop}/damage_parameters.lua" "${_mgs_damage_tables}" "${_mgs_out}/DamageParameterTables.lua" ${_mgs_dynamite_damage})

list(LENGTH _mgs_registered _mgs_count)
message(STATUS "Lua checks: registered ${_mgs_count} Lua/patch tests: ${_mgs_registered}")
if(_mgs_skipped)
  message(STATUS "Lua checks: SKIPPED (missing interpreter or game input): ${_mgs_skipped}")
endif()
