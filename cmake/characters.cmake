# Character selection (docs/characters.md; the original integration notes: archive/research-2026-10-23:docs/characters-integration.md).
# Included right after enable_testing() in the top-level CMakeLists.txt. The
# target part is deferred (cmake_language(DEFER)) to the end of that file, so
# it runs after every target exists: mgs_coop_core, the WIN32 probe block
# (mgs_player_ghost_patch, mgs_bootstrap_patch) and, when the test-hygiene
# package is merged, its lua51 interpreter. Keeping these lines out of the
# shared target lists avoids merge conflicts with other packages.

set(MGS_MAIN_CHECKOUT_FALLBACK "" CACHE PATH
  "Main checkout whose work/lua-5.1.5 and build/ inputs are used when the configured folders lack them (empty disables)")
# Pinned Lua 5.1.5 source for the fixture interpreter. Languages cannot be
# enabled during deferred execution, so C is enabled here when a source exists.
set(_mgs_character_lua_dir "")
set(_mgs_character_dirs "${CMAKE_CURRENT_SOURCE_DIR}/work/lua-5.1.5")
if(DEFINED ENV{MGS_LUA_SOURCE_DIR} AND NOT "$ENV{MGS_LUA_SOURCE_DIR}" STREQUAL "")
  list(PREPEND _mgs_character_dirs "$ENV{MGS_LUA_SOURCE_DIR}")
endif()
if(MGS_MAIN_CHECKOUT_FALLBACK)
  list(APPEND _mgs_character_dirs "${MGS_MAIN_CHECKOUT_FALLBACK}/work/lua-5.1.5")
endif()
foreach(_mgs_character_dir IN LISTS _mgs_character_dirs)
  if(NOT _mgs_character_lua_dir AND EXISTS "${_mgs_character_dir}/src/lua.c" AND EXISTS "${_mgs_character_dir}/src/lua.h")
    file(STRINGS "${_mgs_character_dir}/src/lua.h" _mgs_character_release REGEX "^#define[ \t]+LUA_RELEASE[ \t]+\"Lua 5\\.1\\.5\"")
    if(_mgs_character_release)
      set(_mgs_character_lua_dir "${_mgs_character_dir}")
    endif()
  endif()
endforeach()
if(_mgs_character_lua_dir)
  enable_language(C)
endif()

function(mgs_characters_deferred)
  target_sources(mgs_coop_core PRIVATE native/character_receipt.cpp)
  add_executable(character_receipt_tests tests/character_receipt_tests.cpp)
  target_link_libraries(character_receipt_tests PRIVATE mgs_coop_core)
  if(MSVC)
    target_compile_options(character_receipt_tests PRIVATE /W4 /WX)
  endif()
  add_test(NAME character_receipt_tests COMMAND character_receipt_tests)

  if(TARGET mgs_player_ghost_patch)
    # Ocelot/Quiet reset bypass and the lab readback guard (linked by dinput8).
    target_sources(mgs_player_ghost_patch PRIVATE native/player_type_patch.cpp)
    add_executable(player_type_patch_tests tests/player_type_patch_tests.cpp)
    target_include_directories(player_type_patch_tests PRIVATE native)
    target_link_libraries(player_type_patch_tests PRIVATE mgs_player_ghost_patch bcrypt)
    target_compile_options(player_type_patch_tests PRIVATE /W4 /WX)
    add_test(NAME player_type_patch_tests COMMAND player_type_patch_tests)
  endif()

  if(TARGET mgs_bootstrap_patch)
    # generated/character_source.hpp for the TppMain splice. Nothing includes it
    # until the bootstrap_patch.cpp integration (archive/research-2026-10-23:docs/characters-integration.md).
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS lua/coop/character.lua lua/coop/character_readback.lua)
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/lua/coop/character.lua" MGS_CHARACTER_SOURCE)
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/lua/coop/character_readback.lua" MGS_CHARACTER_READBACK_SOURCE)
    configure_file(native/character_source.hpp.in generated/character_source.hpp @ONLY)
  endif()

  # Lua 5.1.5 fixture checks: reuse test-hygiene's lua51 when present, otherwise
  # build an interpreter from the pinned source found above.
  set(_mgs_character_lua "")
  if(TARGET lua51)
    set(_mgs_character_lua lua51)
  elseif(_mgs_character_lua_dir)
    file(GLOB _mgs_character_sources "${_mgs_character_lua_dir}/src/*.c")
    list(FILTER _mgs_character_sources EXCLUDE REGEX "/(luac|print)\\.c$")
    add_executable(mgs_character_lua51 ${_mgs_character_sources})
    target_compile_definitions(mgs_character_lua51 PRIVATE _CRT_SECURE_NO_WARNINGS)
    set(_mgs_character_lua mgs_character_lua51)
  endif()
  if(_mgs_character_lua)
    set(_mgs_character_args "${CMAKE_CURRENT_SOURCE_DIR}/tests/character_lua_check.lua"
      "${CMAKE_CURRENT_SOURCE_DIR}/lua/coop/character.lua" "${CMAKE_CURRENT_SOURCE_DIR}/lua/coop/character_readback.lua")
    add_test(NAME character_lua_check COMMAND ${_mgs_character_lua} ${_mgs_character_args})
    # Optional real-script pass: extracted TppPlayer.lua, TppMain.lua and
    # TppVarInit.lua, accepted only at the SHA-256 recorded by their read-only
    # extraction inventories (docs/characters.md, "Re-extracting the game scripts").
    set(_mgs_character_player "")
    set(_mgs_character_main "")
    set(_mgs_character_varinit "")
    set(_mgs_character_story "")
    set(_mgs_character_mission "")
    set(_mgs_character_roots "${CMAKE_CURRENT_SOURCE_DIR}/build")
    if(MGS_GAME_LUA_DIR)
      list(PREPEND _mgs_character_roots "${MGS_GAME_LUA_DIR}")
    endif()
    if(MGS_MAIN_CHECKOUT_FALLBACK)
      list(APPEND _mgs_character_roots "${MGS_MAIN_CHECKOUT_FALLBACK}/build")
    endif()
    set(_mgs_character_player_hash "7407bd41dd82913edfe9ef7fbb344f443adc7399d42f86c7798ba6b02c94e5b9")
    set(_mgs_character_main_hash "6542d997c3f29742ff7ea0587f7f307794586a9b7a8ee3114b58e79e7f0b6103")
    set(_mgs_character_varinit_hash "cebb1fc45e992e479dcbf1c6b7f0fe391142fa07a4a696bd429ecab92dd9eecf")
    # character=story: TppStory.lua (Quiet's buddy functions) and TppMission.lua (SetSortieBuddy).
    set(_mgs_character_story_hash "ac86be1ad896474229c9f7efcf4ffc6b597b760a503fc1882a6fc56b5e8b5728")
    set(_mgs_character_mission_hash "e99e6addd4c5135fb2d77ab58632d5e349a626eb7f10ea38301372d127f7404a")
    foreach(_mgs_character_dir IN LISTS _mgs_character_roots)
      foreach(_mgs_character_pick IN ITEMS
          "player|startup-script-inspection-20260909/package-01.lua" "player|character-game-lua-20261003/package-01.lua"
          "main|player-allocation-lifecycle-20260909/package-00.lua" "main|character-game-lua-20261003/package-02.lua"
          "varinit|character-game-lua-20261003/package-00.lua"
          "story|character-story-lua-20261023/package-00.lua"
          "mission|startup-script-inspection-20260909/package-02.lua" "mission|respawn-scripts-20260910/package-00.lua"
          "mission|character-game-lua-20261003/package-03.lua")
        string(REPLACE "|" ";" _mgs_character_pair "${_mgs_character_pick}")
        list(GET _mgs_character_pair 0 _mgs_character_kind)
        list(GET _mgs_character_pair 1 _mgs_character_rel)
        set(_mgs_character_file "${_mgs_character_dir}/${_mgs_character_rel}")
        if(NOT _mgs_character_${_mgs_character_kind} AND EXISTS "${_mgs_character_file}")
          file(SHA256 "${_mgs_character_file}" _mgs_character_hash)
          if(_mgs_character_hash STREQUAL "${_mgs_character_${_mgs_character_kind}_hash}")
            set(_mgs_character_${_mgs_character_kind} "${_mgs_character_file}")
          endif()
        endif()
      endforeach()
    endforeach()
    if(_mgs_character_story AND _mgs_character_mission)
      # The game's own Quiet functions drive the story resolution (docs/characters.md).
      add_test(NAME character_story_lua_check COMMAND ${_mgs_character_lua} ${_mgs_character_args}
        "--story=${_mgs_character_story}" "--mission=${_mgs_character_mission}")
      set_tests_properties(character_story_lua_check PROPERTIES PASS_REGULAR_EXPRESSION "real TppStory/TppMission Quiet life cycle")
    else()
      message(STATUS "Character checks: extracted TppStory.lua/TppMission.lua not found; character_story_lua_check skipped")
    endif()
    if(_mgs_character_player AND _mgs_character_main)
      set(_mgs_character_game_args "${_mgs_character_player}" "${_mgs_character_main}")
      if(_mgs_character_varinit)
        list(APPEND _mgs_character_game_args "--varinit=${_mgs_character_varinit}")
      else()
        message(STATUS "Character checks: extracted TppVarInit.lua not found; the continue path uses the mirror")
      endif()
      add_test(NAME character_game_lua_check COMMAND ${_mgs_character_lua} ${_mgs_character_args} ${_mgs_character_game_args})
      # The complete DLL TppMain composition (host trial, equipment, marker
      # guard, readback, session lifecycle and this chunk) that
      # host_trial_patch_check writes: proves `n` and the end-of-OnAllocate
      # hook where the shipped chunk actually runs (cmake/lua_checks.cmake).
      if(TEST host_trial_patch_check)
        set(_mgs_character_composed "${CMAKE_CURRENT_BINARY_DIR}/lua-check/TppMain-host-trial.lua")
        if(DEFINED _mgs_out)
          set(_mgs_character_composed "${_mgs_out}/TppMain-host-trial.lua")
        endif()
        add_test(NAME character_composed_lua_check COMMAND ${_mgs_character_lua} ${_mgs_character_args}
          ${_mgs_character_game_args} "${_mgs_character_composed}")
        set_tests_properties(character_composed_lua_check PROPERTIES FIXTURES_REQUIRED host_trial_patch_check_output)
      else()
        message(STATUS "Character checks: host_trial_patch_check not registered; character_composed_lua_check skipped")
      endif()
    else()
      message(STATUS "Character checks: extracted TppPlayer.lua/TppMain.lua not found; character_game_lua_check skipped")
    endif()
  else()
    message(STATUS "Character checks: no Lua 5.1.5 source; character_lua_check skipped")
  endif()
endfunction()
cmake_language(DEFER CALL mgs_characters_deferred)
