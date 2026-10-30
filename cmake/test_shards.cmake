# Sharded tests (docs/STATUS.md "Developer test loop"): one long fixture registered as N CTest entries that run in
# parallel. mgs_add_test_shards(<name> <count> [TIMEOUT <s>] [FLAG <flag>] COMMAND <command...>) registers
# <name>_1 .. <name>_<count>, each running <command...> <flag> K/<count> (flag: --shard, or -Shard for a script):
#   a C++ fixture with a tests/test_cases.hpp case table deals its cases to the shards by their cost;
#   a PowerShell suite runs the parts its own table gives shard K (tests/TestShards.psm1).
# Every case runs in exactly one shard because all K = 1..count are registered here. The names are kept in the global
# property MGS_TEST_SHARDS_<name> (cmake/test_labels.cmake labels the entries of a slow suite slow).
# tools/worker/Invoke-WorkerChecks.ps1 (Get-WorkerShardedTests) names the same counts for the audited ones, and its
# inventory audit accepts exactly this command form.
function(mgs_add_test_shards name count)
  cmake_parse_arguments(PARSE_ARGV 2 _mgs_shard "" "TIMEOUT;FLAG" "COMMAND")
  if(NOT _mgs_shard_COMMAND)
    message(FATAL_ERROR "mgs_add_test_shards(${name}): COMMAND is required")
  endif()
  if(NOT _mgs_shard_FLAG)
    set(_mgs_shard_FLAG --shard)
  endif()
  set(_mgs_names)
  foreach(_mgs_k RANGE 1 ${count})
    add_test(NAME ${name}_${_mgs_k} COMMAND ${_mgs_shard_COMMAND} ${_mgs_shard_FLAG} ${_mgs_k}/${count})
    if(_mgs_shard_TIMEOUT)
      set_tests_properties(${name}_${_mgs_k} PROPERTIES TIMEOUT ${_mgs_shard_TIMEOUT})
    endif()
    list(APPEND _mgs_names ${name}_${_mgs_k})
  endforeach()
  set_property(GLOBAL PROPERTY MGS_TEST_SHARDS_${name} "${_mgs_names}")
endfunction()
