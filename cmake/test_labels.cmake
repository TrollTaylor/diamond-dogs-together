# CTest labels and parallel safety (docs/STATUS.md "Developer test loop").
#
# Every test carries exactly one label, fast or slow:
#   slow  over ~10 s, a PowerShell suite, the runtime .dat integration test
#         (reads the game folder) or the launcher screenshot test (DX11 window)
#   fast  everything else (the native and Lua fixtures)
#   ctest -L fast -j N    the quick loop (tools/build.ps1 -Fast)
#   ctest -j N            everything (tools/build.ps1)
# -j is safe: tests that share something carry a RESOURCE_LOCK below. A fixture
# that binds fixed loopback UDP ports also holds a machine-wide mutex
# (tests/test_port_lock.hpp), because suites of different checkouts may run
# at the same time and RESOURCE_LOCK only orders the tests of one ctest.
#
# Deferred: it runs after every add_test, including the deferred character
# tests (cmake/characters.cmake queued its call first).

set(MGS_SLOW_TESTS
  # PowerShell suites
  local_coop_windows_tests test_shards_tests
  build_profile_tests worker_loading_tests probe_install_recovery mgo_unique_rest_session_tests
  mgo_standing_cycle_session_tests mgo_standing_frame_policy_tests worker_coverage_tests
  # over ~10 s (real sockets with timers; the launcher core end to end; real helper processes)
  ddt_local_watch_process
  # the game folder and the GPU
  ddt_runtime_dat_integration ddt_launcher_screens)

# Sharded suites (cmake/test_shards.cmake) whose entries <name>_1 .. <name>_N are all slow.
set(MGS_SLOW_SHARDED_TESTS ddt_core_tests link_udp_tests worker_session_tests local_coop_session_tests lab_scripts_tests friend_package_tests)

# name -> tests that share it
set(MGS_TEST_RESOURCE_LOCKS
  # Each writes clone-fixture.bin into the build folder (its working directory).
  "clone_fixture_bin:standing_selection_off_tests,standing_selection_on_tests"
  # Each creates a DX11 device and a hidden window.
  "dx11_window:ddt_launcher_screens,debug_overlay_tests,worker_input_fixture_tests")

function(mgs_label_tests)
  get_property(_mgs_tests DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" PROPERTY TESTS)
  set(_mgs_slow_names ${MGS_SLOW_TESTS})
  foreach(_mgs_base IN LISTS MGS_SLOW_SHARDED_TESTS)
    get_property(_mgs_shards GLOBAL PROPERTY MGS_TEST_SHARDS_${_mgs_base})
    if(NOT _mgs_shards AND WIN32 AND MSVC)
      message(FATAL_ERROR "MGS_SLOW_SHARDED_TESTS names ${_mgs_base}, which mgs_add_test_shards did not register")
    endif()
    list(APPEND _mgs_slow_names ${_mgs_shards})
  endforeach()
  # Every registered test name, one per line: worker_coverage_tests checks that the Release receipt
  # (tools/worker/Invoke-WorkerChecks.ps1 Assert-WorkerCoverage) audits or excludes each one (M7 review: a new
  # test missing from Get-WorkerTests failed only at tools/package.ps1 -Friend).
  string(JOIN "\n" _mgs_names ${_mgs_tests})
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/ctest-test-names.txt" "${_mgs_names}\n")
  set(_mgs_fast 0)
  set(_mgs_slow 0)
  foreach(_mgs_test IN LISTS _mgs_tests)
    if(_mgs_test IN_LIST _mgs_slow_names)
      set_property(TEST ${_mgs_test} APPEND PROPERTY LABELS slow)
      math(EXPR _mgs_slow "${_mgs_slow} + 1")
    else()
      set_property(TEST ${_mgs_test} APPEND PROPERTY LABELS fast)
      math(EXPR _mgs_fast "${_mgs_fast} + 1")
    endif()
  endforeach()
  foreach(_mgs_entry IN LISTS MGS_TEST_RESOURCE_LOCKS)
    string(REPLACE ":" ";" _mgs_pair "${_mgs_entry}")
    list(GET _mgs_pair 0 _mgs_resource)
    list(GET _mgs_pair 1 _mgs_members)
    string(REPLACE "," ";" _mgs_members "${_mgs_members}")
    foreach(_mgs_test IN LISTS _mgs_members)
      if(_mgs_test IN_LIST _mgs_tests)
        set_property(TEST ${_mgs_test} APPEND PROPERTY RESOURCE_LOCK ${_mgs_resource})
      endif()
    endforeach()
  endforeach()
  message(STATUS "CTest labels: ${_mgs_fast} fast, ${_mgs_slow} slow")
endfunction()
cmake_language(DEFER CALL mgs_label_tests)
