# Diamond Dogs Together launcher: archive utilities (CRC-32, inflate, zip,
# QAR, FPK/FPKD, FOX2, MD5/SHA-256) and the runtime .dat builder.
# See docs/runtime-archive-builder.md.
if(NOT WIN32)
  return()
endif()

set(_ddt_generated "${CMAKE_CURRENT_BINARY_DIR}/generated/ddt")
file(MAKE_DIRECTORY "${_ddt_generated}")
# name | C++ symbol | source file | CRLF->LF
set(_ddt_embedded
  "four_player_controller|kFourPlayerControllerFpkd|native/launcher/assets/mgs_coop_four_player_controller.fpkd.bin|0"
  "scarf_controller|kScarfControllerFpkd|native/launcher/assets/mgs_coop_two_player_scarf_controller.fpkd.bin|0"
  "archive_probe|kArchiveProbeLua|lua/coop/archive_probe.lua|1")
set(_ddt_asset_headers)
foreach(_ddt_item IN LISTS _ddt_embedded)
  string(REPLACE "|" ";" _ddt_fields "${_ddt_item}")
  list(GET _ddt_fields 0 _ddt_name)
  list(GET _ddt_fields 1 _ddt_symbol)
  list(GET _ddt_fields 2 _ddt_input)
  list(GET _ddt_fields 3 _ddt_lf)
  set(_ddt_header "${_ddt_generated}/ddt_asset_${_ddt_name}.hpp")
  add_custom_command(
    OUTPUT "${_ddt_header}"
    COMMAND "${CMAKE_COMMAND}" "-DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/${_ddt_input}" "-DOUTPUT=${_ddt_header}"
            "-DSYMBOL=${_ddt_symbol}" "-DTEXT_LF=${_ddt_lf}" -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ddt_embed.cmake"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${_ddt_input}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ddt_embed.cmake"
    COMMENT "Embedding ${_ddt_input}"
    VERBATIM)
  list(APPEND _ddt_asset_headers "${_ddt_header}")
endforeach()

add_library(ddt_archive STATIC
  native/launcher/crc32.cpp
  native/launcher/file_io.cpp
  native/launcher/fox2.cpp
  native/launcher/fpk.cpp
  native/launcher/hash.cpp
  native/launcher/inflate.cpp
  native/launcher/qar.cpp
  native/launcher/runtime_dat.cpp
  native/launcher/zip.cpp
  ${_ddt_asset_headers})
target_include_directories(ddt_archive PUBLIC native PRIVATE "${_ddt_generated}")
target_link_libraries(ddt_archive PUBLIC bcrypt)
if(MSVC)
  target_compile_options(ddt_archive PRIVATE /W4 /WX)
endif()

# Developer tool: ddt_datbuild build <gameDir> <out.dat> (and list/extract/verify).
add_executable(ddt_datbuild native/launcher/ddt_datbuild.cpp)
target_link_libraries(ddt_datbuild PRIVATE ddt_archive)
if(MSVC)
  target_compile_options(ddt_datbuild PRIVATE /W4 /WX)
endif()

add_executable(ddt_archive_tests tests/ddt_archive_tests.cpp)
target_link_libraries(ddt_archive_tests PRIVATE ddt_archive)
if(MSVC)
  target_compile_options(ddt_archive_tests PRIVATE /W4 /WX)
endif()
add_test(NAME ddt_archive_tests COMMAND ddt_archive_tests)

# Builds the runtime .dat from a real game install into a temp folder and checks
# the pinned SHA-256. MGS_GAME_DIR overrides the folder; SKIPPED (exit 77) when absent.
set(DDT_TEST_GAME_DIR "D:/Games/steamapps/common/MGS_TPP" CACHE PATH "Game folder for the runtime .dat integration test")
add_executable(ddt_runtime_dat_tests tests/ddt_runtime_dat_tests.cpp)
target_link_libraries(ddt_runtime_dat_tests PRIVATE ddt_archive)
if(MSVC)
  target_compile_options(ddt_runtime_dat_tests PRIVATE /W4 /WX)
endif()
add_test(NAME ddt_runtime_dat_integration COMMAND ddt_runtime_dat_tests "${DDT_TEST_GAME_DIR}")
set_tests_properties(ddt_runtime_dat_integration PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600)
