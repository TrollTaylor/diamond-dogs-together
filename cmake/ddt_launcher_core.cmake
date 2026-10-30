# Diamond Dogs Together launcher core (component B, launcher spec 2026-10-23):
# the real LauncherModel (setup, sessions, save handoff, tools, the --watch
# helper, the developer's local two-window test and its --local-watch helper)
# on top of the archive utilities, plus its unit tests.
if(NOT (WIN32 AND MSVC))
  return()
endif()

add_library(ddt_core STATIC
  native/launcher/core/core_env_win32.cpp
  native/launcher/core/core_install.cpp
  native/launcher/core/core_json.cpp
  native/launcher/core/core_localtest.cpp
  native/launcher/core/core_localtest_win32.cpp
  native/launcher/core/core_log.cpp
  native/launcher/core/core_model.cpp
  native/launcher/core/core_paths.cpp
  native/launcher/core/core_saves.cpp
  native/launcher/core/core_selftest.cpp
  native/launcher/core/core_session.cpp
  native/launcher/core/core_text.cpp
  native/launcher/core/core_update.cpp
  native/launcher/core/core_util.cpp
  native/launcher/launcher_join_code.cpp)
target_include_directories(ddt_core PUBLIC native native/launcher native/launcher/core)
target_link_libraries(ddt_core PUBLIC ddt_archive mgs_coop_core shell32 ole32 user32 advapi32 winhttp ws2_32)
# The program version (package.json must name the same X.Y.Z) and the build id.
target_compile_definitions(ddt_core PRIVATE "DDT_CORE_VERSION=\"${PROJECT_VERSION}\"" "DDT_CORE_BUILD_ID=\"${MGS_COOP_BUILD_ID}\"")
target_compile_options(ddt_core PRIVATE /W4 /WX /utf-8)

add_executable(ddt_core_tests tests/ddt_core_tests.cpp)
target_link_libraries(ddt_core_tests PRIVATE ddt_core)
target_compile_definitions(ddt_core_tests PRIVATE "DDT_CORE_VERSION=\"${PROJECT_VERSION}\""
                           "DDT_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"")
target_compile_options(ddt_core_tests PRIVATE /W4 /WX /utf-8)
# Five parallel shards (tests/test_cases.hpp): ddt_core_tests_1 .. _5, about 9 s each instead of one 45 s run.
mgs_add_test_shards(ddt_core_tests 5 TIMEOUT 300 COMMAND ddt_core_tests)
