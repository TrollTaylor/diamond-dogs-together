# Diamond Dogs Together launcher, GUI shell (C0, launcher spec 2026-10-23):
# DiamondDogsTogether.exe (Win32 + DX11 + Dear ImGui, WIN32 subsystem) on the
# LauncherModel interface with the mock model, plus its headless tests.
if(WIN32 AND MSVC AND TARGET mgs_imgui)
  set(_ddt_generated "${CMAKE_CURRENT_BINARY_DIR}/generated/launcher")
  configure_file(native/launcher/ddt_launcher_version.h.in "${_ddt_generated}/ddt_launcher_version.h" @ONLY)
  # The pure part (no ImGui, no window): UI logic, join-code check, mock model, self-test.
  # launcher_join_code.cpp lives in ddt_core (cmake/ddt_launcher_core.cmake), which
  # the logic library links: the real model and the --watch / --selftest entries.
  add_library(ddt_launcher_logic STATIC
    native/launcher/launcher_ui_logic.cpp
    native/launcher/launcher_model_mock.cpp
    native/launcher/launcher_entry.cpp)
  target_include_directories(ddt_launcher_logic PUBLIC native/launcher "${_ddt_generated}")
  target_link_libraries(ddt_launcher_logic PUBLIC ddt_core mgs_coop_core)
  target_compile_options(ddt_launcher_logic PRIVATE /W4 /WX /utf-8)

  add_executable(ddt_launcher WIN32
    native/launcher/launcher_main.cpp
    native/launcher/launcher_app.cpp
    native/launcher/launcher_theme.cpp
    native/launcher/resources/ddt_launcher.rc
    native/launcher/resources/ddt_launcher.manifest)
  set_target_properties(ddt_launcher PROPERTIES OUTPUT_NAME DiamondDogsTogether)
  # rc.exe reads the icon and the emblem itself; the dependency scan only sees
  # #include files, so without this a regenerated icon would not rebuild the exe.
  set_source_files_properties(native/launcher/resources/ddt_launcher.rc PROPERTIES OBJECT_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/native/launcher/resources/ddt_launcher.ico;${CMAKE_CURRENT_SOURCE_DIR}/native/launcher/resources/ddt_logo.png")
  target_include_directories(ddt_launcher PRIVATE native/launcher/resources third_party/imgui)
  target_compile_options(ddt_launcher PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W4 /WX /utf-8>)
  target_compile_definitions(ddt_launcher PRIVATE UNICODE _UNICODE)
  target_link_libraries(ddt_launcher PRIVATE ddt_launcher_logic mgs_imgui d3d11 dxgi d3dcompiler dwmapi
                        windowscodecs ole32 shell32 comdlg32 user32 gdi32)

  add_executable(launcher_ui_tests tests/launcher_ui_tests.cpp)
  target_link_libraries(launcher_ui_tests PRIVATE ddt_launcher_logic)
  target_compile_options(launcher_ui_tests PRIVATE /W4 /WX /utf-8)
  add_test(NAME launcher_ui_tests COMMAND launcher_ui_tests)
  # The app icon is the logo at every size, and the built exe embeds exactly that icon.
  add_executable(launcher_icon_tests tests/launcher_icon_tests.cpp)
  target_include_directories(launcher_icon_tests PRIVATE native/launcher/resources)
  target_compile_options(launcher_icon_tests PRIVATE /W4 /WX /utf-8)
  target_compile_definitions(launcher_icon_tests PRIVATE UNICODE _UNICODE)
  target_link_libraries(launcher_icon_tests PRIVATE windowscodecs ole32 user32 gdi32)
  add_dependencies(launcher_icon_tests ddt_launcher)
  add_test(NAME launcher_icon_tests COMMAND launcher_icon_tests
           "${CMAKE_CURRENT_SOURCE_DIR}/native/launcher/resources/ddt_launcher.ico"
           "${CMAKE_CURRENT_SOURCE_DIR}/native/launcher/resources/ddt_logo.png"
           "${CMAKE_CURRENT_SOURCE_DIR}/docs/readme/logo.png" $<TARGET_FILE:ddt_launcher>)
  # The committed icon and emblem are exactly what the generator makes from the logo
  # (pixels, not bytes: PNG bytes can differ between zlib builds). Needs Python 3.8
  # or later (the generator uses itertools.accumulate(initial=)).
  find_package(Python3 3.8 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    add_test(NAME launcher_assets_current
             COMMAND Python3::Interpreter -I "${CMAKE_CURRENT_SOURCE_DIR}/tools/launcher/make_launcher_assets.py" --check)
  endif()
  # The exe itself: the built-in self-test, and every page rendered once
  # (hidden window, WARP fallback without a GPU) into the build folder.
  add_test(NAME ddt_launcher_selftest COMMAND ddt_launcher --selftest)
  add_test(NAME ddt_launcher_screens COMMAND ddt_launcher --screenshot-all "${CMAKE_CURRENT_BINARY_DIR}/launcher-shots" --scale 1)
  # ImGui reports recoverable layout errors on stdout in this mode: any of them fails the test.
  set_tests_properties(ddt_launcher_screens PROPERTIES FAIL_REGULAR_EXPRESSION "imgui-error|FAIL:")
  # The local two-window test's Win32 seams with real processes, all hidden: the exe's own
  # --local-watch helper puts a fixture run's files back; CloseGame ends a waiting helper.
  add_test(NAME ddt_local_watch_process COMMAND ddt_core_tests --local-watch-process $<TARGET_FILE:ddt_launcher>)
  set_tests_properties(ddt_local_watch_process PROPERTIES TIMEOUT 180)
endif()
