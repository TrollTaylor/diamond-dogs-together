# Character selection (docs/characters.md, docs/characters-integration.md).
# Included right after enable_testing() in the top-level CMakeLists.txt. The
# target part is deferred (cmake_language(DEFER)) to the end of that file, so
# it runs after every target exists: mgs_coop_core, the WIN32 probe block
# (mgs_player_ghost_patch, mgs_bootstrap_patch) and, when the test-hygiene
# package is merged, its lua51 interpreter. Keeping these lines out of the
# shared target lists avoids merge conflicts with other packages.

set(MGS_MAIN_CHECKOUT_FALLBACK "C:/Users/taylo/OneDrive/Documents/ChatGPT/mgs-coop" CACHE PATH
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
    # until the bootstrap_patch.cpp integration (docs/characters-integration.md).
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
    # Optional real-script pass: extracted TppPlayer.lua and TppMain.lua, accepted
    # only at the SHA-256 recorded by their read-only extraction inventories.
    set(_mgs_character_player "")
    set(_mgs_character_main "")
    set(_mgs_character_roots "${CMAKE_CURRENT_SOURCE_DIR}/build")
    if(MGS_GAME_LUA_DIR)
      list(PREPEND _mgs_character_roots "${MGS_GAME_LUA_DIR}")
    endif()
    if(MGS_MAIN_CHECKOUT_FALLBACK)
      list(APPEND _mgs_character_roots "${MGS_MAIN_CHECKOUT_FALLBACK}/build")
    endif()
    foreach(_mgs_character_dir IN LISTS _mgs_character_roots)
      set(_mgs_character_file "${_mgs_character_dir}/startup-script-inspection-20260909/package-01.lua")
      if(NOT _mgs_character_player AND EXISTS "${_mgs_character_file}")
        file(SHA256 "${_mgs_character_file}" _mgs_character_hash)
        if(_mgs_character_hash STREQUAL "7407bd41dd82913edfe9ef7fbb344f443adc7399d42f86c7798ba6b02c94e5b9")
          set(_mgs_character_player "${_mgs_character_file}")
        endif()
      endif()
      set(_mgs_character_file "${_mgs_character_dir}/player-allocation-lifecycle-20260909/package-00.lua")
      if(NOT _mgs_character_main AND EXISTS "${_mgs_character_file}")
        file(SHA256 "${_mgs_character_file}" _mgs_character_hash)
        if(_mgs_character_hash STREQUAL "6542d997c3f29742ff7ea0587f7f307794586a9b7a8ee3114b58e79e7f0b6103")
          set(_mgs_character_main "${_mgs_character_file}")
        endif()
      endif()
    endforeach()
    if(_mgs_character_player AND _mgs_character_main)
      add_test(NAME character_game_lua_check COMMAND ${_mgs_character_lua} ${_mgs_character_args}
        "${_mgs_character_player}" "${_mgs_character_main}")
    else()
      message(STATUS "Character checks: extracted TppPlayer.lua/TppMain.lua not found; character_game_lua_check skipped")
    endif()
  else()
    message(STATUS "Character checks: no Lua 5.1.5 source; character_lua_check skipped")
  endif()
endfunction()
cmake_language(DEFER CALL mgs_characters_deferred)
