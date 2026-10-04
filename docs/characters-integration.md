# Character selection: integration steps

For the orchestrator. Branch `wp/characters` adds the character feature as
new, self-contained files. This document lists the exact changes to files that
the branch does not own. Apply them **after the lab-observer package merges**.
The full change set is also in [`characters-integration.patch`](characters-integration.patch):

```
git apply --ignore-whitespace docs/characters-integration.patch
```

`--ignore-whitespace` is needed because the working tree uses CRLF
(`core.autocrlf=true`) and the patch uses LF. If lab-observer changed the same
regions, apply the hunks by hand using the textual anchors given in each step.

## 1. What the branch adds (already merged with the branch)

| File | Role |
|---|---|
| `native/player_type_patch.cpp`, `native/include/mgscoop/player_type_patch.hpp` | `mgscoop::player_type::SetActive(base, span, enabled, log)` / `IsActive()`: scoped bypass of the native Ocelot/Quiet -> Snake reset. Linked into `mgs_player_ghost_patch`, which `dinput8.dll` already links. |
| `native/player_type_contract.hpp` | Generated contract: `UpdatePartsStatus` extent `[0x9cd030, 0x9ce924)` (6388 bytes), SHA-256 `eb2c6b98...a8aa`, site `0x9cd370`, `0f 85 9c 07 00 00` -> `48 e9 9c 07 00 00`. |
| `tools/generate_player_type_contract.py`, `docs/evidence/player-type-contract-2026-10-03.json` | Static re-verification and evidence (see section 8). |
| `native/character_receipt.cpp`, `native/include/mgscoop/character_receipt.hpp` | Pure receipt helpers (`Classify`, `FormatQuery`, `NeedsResetBypass`, `FormatLogLine`). Added to `mgs_coop_core`. |
| `lua/coop/character.lua` | Inline chunk spliced into `TppMain.OnAllocate` (step 3). |
| `lua/coop/character_readback.lua` | Appended `TppMain` wrapper: readback poll at mission start and on update (step 3). |
| `native/character_source.hpp.in` | `configure_file` template producing `character_source` and `character_readback_source` (step 2). |
| `tests/player_type_patch_tests.cpp`, `tests/character_receipt_tests.cpp`, `tests/character_lua_check.lua` | Registered in CTest by the branch's own block at the end of `CMakeLists.txt`. |
| `tools/inspect_player_system.py` (extended) | Live read-only readback: per-instance type/parts/camo/hand/face, vars appearance bytes and the branch state. |

Nothing in the branch is reachable from the DLL until steps 2 to 4 are applied.
With steps 2 to 4 applied and no `coop.ini`, behaviour stays as before: the
query returns `''`, the Lua does nothing further, and the native bypass is
never touched.

## 2. CMake: generate the Lua source header

In the `WIN32` block, directly after
`configure_file(native/respawn_source.hpp.in generated/respawn_source.hpp @ONLY)`,
add:

```cmake
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS lua/coop/character.lua lua/coop/character_readback.lua)
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/lua/coop/character.lua" MGS_CHARACTER_SOURCE)
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/lua/coop/character_readback.lua" MGS_CHARACTER_READBACK_SOURCE)
  configure_file(native/character_source.hpp.in generated/character_source.hpp @ONLY)
```

The raw-string delimiters are `MGS_CHARACTER` and `MGS_CHAR_READ` (MSVC allows
at most 16 characters). `character.lua` is 12 KB. `character_lua_check` fails
if either file reaches 16,000 bytes (MSVC C2026 limit 16,380 per literal).

## 3. `native/bootstrap_patch.cpp`: splice into the TppMain host composition

`SelectHostTrialPatch` (exact TppMain.lua, 21,634 bytes, SHA-256 `6542d997...6103`):

- **Insertion** right after this anchor, which is unique in the pinned source
  (offset 3,749; the insertion point is the following byte):

  ```lua
  if t(n.sequence.MissionPrepare)then
  n.sequence.MissionPrepare()end
  if t(n.sequence.OnEndMissionPrepareSequence)then
  TppSequence.SetOnEndMissionPrepareFunction(n.sequence.OnEndMissionPrepareSequence)end
  end
  ```

  This position comes after `Mission.Start()` (the host-trial anchor, offset
  2,649) and after the mission's `MissionPrepare` (where story missions call
  `RegisterTemporaryPlayerType`). It comes before the `OnLoad` loop, `DeclareSVars`,
  `TppVarInit.InitializeForNewMission` (which registers the Subsistence
  parts), the first `TppPlayer.MissionStartPlayerTypeSetting()` (offset 4,583),
  `TppEquip.StartLoadingToEquipMissionBlock()` (the equipment anchor, 6,764) and
  `TppPlayer.ForceChangePlayerFromOcelot()` (7,236). It is at statement level
  in `OnAllocate`, outside the `if n.sequence` block, so it runs for every
  allocation. That keeps the bypass state current (for example, it is
  switched off in the ACC).
- **Append** `character_readback_source` after `marker_loading_source` and
  before `mission_trace_source`. It stays inside `session_lifecycle`, which
  must remain the outermost wrapper.

```diff
--- a/native/bootstrap_patch.cpp
+++ b/native/bootstrap_patch.cpp
@@ -18,6 +18,7 @@
 #include "session_lifecycle_source.hpp"
 #include "session_feedback_source.hpp"
 #include "respawn_source.hpp"
+#include "character_source.hpp"
 
 namespace mgscoop {
 namespace {
@@ -137,7 +138,13 @@
     constexpr char marker_guard[]="\nif not (Tpp and type(Tpp.MgsCoop)==\"table\" and Tpp.MgsCoop.markerLoadingGuardActive==true) then\n";
     constexpr char marker_guard_end[]="\nend\n";
     constexpr auto marker_extra=sizeof(marker_guard)-1+sizeof(marker_guard_end)-1;
-    constexpr std::size_t result_size=original_size+sizeof(host_trial_source)-1+sizeof(equipment_loading_source)-1+marker_extra+sizeof(host_admission_source)-1+sizeof(client_join_source)-1+sizeof(damage_lifecycle_source)-1+sizeof(enemy_targeting_source)-1+sizeof(respawn_source)-1+sizeof(session_lifecycle_source)-1+sizeof(session_feedback_source)-1+sizeof(marker_loading_source)-1+sizeof(mission_trace_source)-1;
+    // Character selection runs after the mission's MissionPrepare and before
+    // the OnLoad/DeclareSVars/MissionStartPlayerTypeSetting part of OnAllocate.
+    constexpr char character_anchor[]="if t(n.sequence.MissionPrepare)then\nn.sequence.MissionPrepare()end\n"
+        "if t(n.sequence.OnEndMissionPrepareSequence)then\n"
+        "TppSequence.SetOnEndMissionPrepareFunction(n.sequence.OnEndMissionPrepareSequence)end\nend\n";
+    constexpr auto character_extra=sizeof(character_source)-1;
+    constexpr std::size_t result_size=original_size+sizeof(host_trial_source)-1+sizeof(equipment_loading_source)-1+marker_extra+character_extra+sizeof(host_admission_source)-1+sizeof(client_join_source)-1+sizeof(damage_lifecycle_source)-1+sizeof(enemy_targeting_source)-1+sizeof(respawn_source)-1+sizeof(session_lifecycle_source)-1+sizeof(session_feedback_source)-1+sizeof(marker_loading_source)-1+sizeof(character_readback_source)-1+sizeof(mission_trace_source)-1;
     static_assert(result_size<=script_bridge::max_replacement_bytes);
     static char result[result_size]{};
     static bool initialized=false;
@@ -148,31 +155,37 @@
     // Insertions are pinned by the complete original source hash. Require
     // unique, ordered anchors; the original calls remain in the output once.
     std::size_t insertion=0,found=0,equipment_insertion=0,equipment_found=0;
-    std::size_t marker_insertion=0,marker_found=0;
+    std::size_t marker_insertion=0,marker_found=0,character_insertion=0,character_found=0;
     for(std::size_t i=0;i+sizeof(anchor)-1<=size;++i)
         if(std::memcmp(source+i,anchor,sizeof(anchor)-1)==0) { insertion=i; ++found; }
+    for(std::size_t i=0;i+sizeof(character_anchor)-1<=size;++i)
+        if(std::memcmp(source+i,character_anchor,sizeof(character_anchor)-1)==0) { character_insertion=i+sizeof(character_anchor)-1; ++character_found; }
     for(std::size_t i=0;i+sizeof(equipment_anchor)-1<=size;++i)
         if(std::memcmp(source+i,equipment_anchor,sizeof(equipment_anchor)-1)==0) { equipment_insertion=i; ++equipment_found; }
     for(std::size_t i=0;i+sizeof(marker_anchor)-1<=size;++i)
         if(std::memcmp(source+i,marker_anchor,sizeof(marker_anchor)-1)==0) { marker_insertion=i; ++marker_found; }
-    if(found!=1 || equipment_found!=1 || marker_found!=1 || equipment_insertion<=insertion ||
-       marker_insertion<=equipment_insertion || marker_insertion+sizeof(marker_anchor)-1>=size-8) return false;
+    if(found!=1 || equipment_found!=1 || marker_found!=1 || character_found!=1 || character_insertion<=insertion ||
+       equipment_insertion<=character_insertion || marker_insertion<=equipment_insertion ||
+       marker_insertion+sizeof(marker_anchor)-1>=size-8) return false;
     AcquireSRWLockExclusive(&patch_lock);
     if(!initialized) {
         std::memcpy(result,source,insertion);
         std::memcpy(result+insertion,host_trial_source,sizeof(host_trial_source)-1);
-        std::memcpy(result+insertion+sizeof(host_trial_source)-1,source+insertion,equipment_insertion-insertion);
-        const auto equipment=equipment_insertion+sizeof(host_trial_source)-1;
+        std::memcpy(result+insertion+sizeof(host_trial_source)-1,source+insertion,character_insertion-insertion);
+        const auto character=character_insertion+sizeof(host_trial_source)-1;
+        std::memcpy(result+character,character_source,character_extra);
+        std::memcpy(result+character+character_extra,source+character_insertion,equipment_insertion-character_insertion);
+        const auto equipment=equipment_insertion+sizeof(host_trial_source)-1+character_extra;
         std::memcpy(result+equipment,equipment_loading_source,sizeof(equipment_loading_source)-1);
         std::memcpy(result+equipment+sizeof(equipment_loading_source)-1,source+equipment_insertion,marker_insertion-equipment_insertion);
-        const auto marker=marker_insertion+sizeof(host_trial_source)-1+sizeof(equipment_loading_source)-1;
+        const auto marker=marker_insertion+sizeof(host_trial_source)-1+character_extra+sizeof(equipment_loading_source)-1;
         std::memcpy(result+marker,marker_guard,sizeof(marker_guard)-1);
         std::memcpy(result+marker+sizeof(marker_guard)-1,source+marker_insertion,sizeof(marker_anchor)-1);
         const auto after_marker=marker+sizeof(marker_guard)-1+sizeof(marker_anchor)-1;
         std::memcpy(result+after_marker,marker_guard_end,sizeof(marker_guard_end)-1);
         std::memcpy(result+after_marker+sizeof(marker_guard_end)-1,source+marker_insertion+sizeof(marker_anchor)-1,
             size-marker_insertion-(sizeof(marker_anchor)-1)-8);
-        constexpr auto appended=original_size-8+sizeof(host_trial_source)-1+sizeof(equipment_loading_source)-1+marker_extra;
+        constexpr auto appended=original_size-8+sizeof(host_trial_source)-1+character_extra+sizeof(equipment_loading_source)-1+marker_extra;
         std::memcpy(result+appended,host_admission_source,sizeof(host_admission_source)-1);
         std::memcpy(result+appended+sizeof(host_admission_source)-1,client_join_source,sizeof(client_join_source)-1);
         constexpr auto lifecycle=appended+sizeof(host_admission_source)-1+sizeof(client_join_source)-1;
@@ -185,10 +198,12 @@
         std::memcpy(result+feedback,session_feedback_source,sizeof(session_feedback_source)-1);
         constexpr auto marker_lifecycle=feedback+sizeof(session_feedback_source)-1;
         std::memcpy(result+marker_lifecycle,marker_loading_source,sizeof(marker_loading_source)-1);
-        std::memcpy(result+marker_lifecycle+sizeof(marker_loading_source)-1,mission_trace_source,sizeof(mission_trace_source)-1);
+        constexpr auto readback=marker_lifecycle+sizeof(marker_loading_source)-1;
+        std::memcpy(result+readback,character_readback_source,sizeof(character_readback_source)-1);
+        std::memcpy(result+readback+sizeof(character_readback_source)-1,mission_trace_source,sizeof(mission_trace_source)-1);
         // The operational termination signal must run before every other
         // wrapper, independently of diagnostic limits or tracing errors.
-        constexpr auto session_lifecycle=marker_lifecycle+sizeof(marker_loading_source)-1+sizeof(mission_trace_source)-1;
+        constexpr auto session_lifecycle=readback+sizeof(character_readback_source)-1+sizeof(mission_trace_source)-1;
         std::memcpy(result+session_lifecycle,session_lifecycle_source,sizeof(session_lifecycle_source)-1);
         static_assert(session_lifecycle+sizeof(session_lifecycle_source)-1==result_size-8);
         std::memcpy(result+result_size-8,"return e",8);
```

## 4. `native/game_script_observer.cpp`: receipts and the native bypass

Add three includes, one helper after `OnMissionThread()`, one dispatch in
`ObserveEntry` (just before `constexpr char receipt[] = "return 'MGS_COOP_BOOTSTRAP_EXECUTED_V1'";`),
and one startup log line in `InstallGameScriptObserver` (after the archive-mode
`Log` calls, before the hook install).

```diff
--- a/native/game_script_observer.cpp
+++ b/native/game_script_observer.cpp
@@ -22,6 +22,9 @@
 #include "mgscoop/soldier_patch.hpp"
 #include "mgscoop/mission_termination.hpp"
 #include "mgscoop/mission_initialization.hpp"
+#include "mgscoop/character_receipt.hpp"
+#include "mgscoop/lab_config.hpp"
+#include "mgscoop/player_type_patch.hpp"
 
 namespace {
 HANDLE script_log = INVALID_HANDLE_VALUE;
@@ -94,6 +97,43 @@
 }
 bool OnTerminationThread() noexcept { return termination_scope.OnThread(GetCurrentThreadId()); }
 bool OnMissionThread() noexcept { return OnAllocationThread() || OnRuntimeThread() || OnTerminationThread(); }
+
+// Character selection receipts (lua/coop/character.lua, docs/characters.md).
+// The query is read-only and formatted once from coop.ini (constant for the
+// process). The Ocelot/Quiet reset bypass is enabled only for a configured
+// Ocelot/Quiet during an accepted allocation (an already active bypass is
+// confirmed, e.g. for a checkpoint reload); disabling is always allowed.
+// Report/readback receipts are log-only and are never replaced.
+char character_query[mgscoop::character::max_line]{};
+std::size_t character_query_size = 0;
+bool SelectCharacterReceipt(mgscoop::character::Receipt kind, const char* buffer, std::size_t size,
+                            mgscoop::script_bridge::Replacement& replacement) noexcept {
+    using mgscoop::character::Receipt;
+    static constexpr char yes[]="return true", no[]="return false";
+    const auto& coop=mgscoop::GetCoopConfig();
+    if(kind==Receipt::query) {
+        if(!character_query_size)
+            character_query_size=mgscoop::character::FormatQuery(coop,mgscoop::GetLabConfig().present,
+                                                                  character_query,sizeof(character_query));
+        if(!character_query_size) return false;
+        replacement={character_query,character_query_size};
+        return true;
+    }
+    if(kind==Receipt::bypass_on || kind==Receipt::bypass_off) {
+        bool accepted=false;
+        if(kind==Receipt::bypass_off) accepted=mgscoop::player_type::SetActive(game_base,game_span,false,Log);
+        else if(!mgscoop::character::NeedsResetBypass(coop) ||
+                (!OnAllocationThread() && !mgscoop::player_type::IsActive()))
+            Log("player_type_patch=refused; reason=not_configured_or_not_allocating\r\n");
+        else accepted=mgscoop::player_type::SetActive(game_base,game_span,true,Log);
+        replacement=accepted ? mgscoop::script_bridge::Replacement{yes,sizeof(yes)-1}
+                             : mgscoop::script_bridge::Replacement{no,sizeof(no)-1};
+        return true;
+    }
+    char line[mgscoop::character::max_line]{};
+    if(mgscoop::character::FormatLogLine(kind,buffer,size,line,sizeof(line))) Log(line);
+    return false;
+}
 bool TransferRuleThread(DWORD previous_thread) noexcept {
     // Both directions are needed: allocation -> runtime for gameplay/cleanup,
     // then runtime -> a later allocation worker for retained cleanup. These
@@ -456,6 +496,11 @@
             }
         }
     }
+    if(!selected && (host_create_enabled || client_enabled)) {
+        const auto character_kind=mgscoop::character::Classify(buffer,size,name);
+        if(character_kind!=mgscoop::character::Receipt::none)
+            selected=SelectCharacterReceipt(character_kind,buffer,size,replacement);
+    }
     constexpr char receipt[] = "return 'MGS_COOP_BOOTSTRAP_EXECUTED_V1'";
     constexpr char admission[]="return 'MGS_COOP_HOST_ADMISSION_V1'";
     constexpr char admission_observed[]="return 'MGS_COOP_HOST_ADMISSION_OBSERVED_V1'";
@@ -581,6 +626,16 @@
                 if (host_create_enabled) Log("mode=host_v2; dynamite_host_creation=enabled max_players=2 peer_acceptance_requested=0\r\n");
                 if (host_admission_enabled) Log("mode=host_v3; peer_acceptance=after_mission_start; max_players=2\r\n");
             }
+            if (host_create_enabled || client_enabled) {
+                // coop.ini is read once here (kernel32 file I/O), outside the Lua compile path.
+                const auto& coop = mgscoop::GetCoopConfig();
+                char character_line[192]{};
+                if (SUCCEEDED(StringCchPrintfA(character_line, ARRAYSIZE(character_line),
+                    "character_config=%s; fallback=%s; coop_ini=%u; reset_bypass_needed=%u; applied_in=TppMain.OnAllocate\r\n",
+                    coop.character == mgscoop::PlayerCharacter::game_default ? "default" : mgscoop::PlayerCharacterName(coop.character),
+                    mgscoop::PlayerCharacterName(coop.character_fallback), unsigned(coop.present),
+                    unsigned(mgscoop::character::NeedsResetBypass(coop))))) Log(character_line);
+            }
             const auto target = reinterpret_cast<mgscoop::script_bridge::LoadBuffer>(static_cast<unsigned char*>(base)+load_rva);
             const auto status = mgscoop::script_bridge::Install(target, expected_body, sizeof(expected_body),
                                                                {nullptr, ObserveEntry, ObserveReturn});
```

### Receipt contract

| Lua compiles (chunk name == source) | Native replaces with | Gate |
|---|---|---|
| `return 'MGS_COOP_CHARACTER_V1'` | `return '<character>','<fallback>',<readback>` built once from `GetCoopConfig()`; `<character>` is `''` for default; `<readback>` is `GetLabConfig().present` | `host_create_enabled \|\| client_enabled` |
| `return 'MGS_COOP_CHARACTER_BYPASS_ON_V1'` | `return true` only if `NeedsResetBypass(coop)` (configured `ocelot`/`quiet`) **and** (`OnAllocationThread()` or the bypass is already active) **and** `player_type::SetActive(base, span, true)` succeeded; otherwise `return false` (Lua then uses the fallback) | same |
| `return 'MGS_COOP_CHARACTER_BYPASS_OFF_V1'` | `return true` if `player_type::SetActive(..., false)` (no-op when not owned) | same |
| `return 'MGS_COOP_CHARACTER_REPORT_V1' -- <payload>` | not replaced; logs `character=<payload with "; ">` | same; payload `[A-Za-z0-9_=:,.;-]{1,192}` |
| `return 'MGS_COOP_CHARACTER_READBACK_V1' -- <payload>` | not replaced; logs `character_readback=...` | same |

When the DLL does not handle the query (older build or another mode), the Lua
receives the literal `'MGS_COOP_CHARACTER_V1'` and stops (`native_unavailable`).

### Where `player_type::SetActive` is called, and why that scope

- **On:** only through `MGS_COOP_CHARACTER_BYPASS_ON_V1`, which the Lua sends
  only when this mission's decision is Ocelot or Quiet. The native side
  re-checks that `coop.ini` asked for Ocelot/Quiet and that the receipt comes
  from the accepted allocation thread.
- **Off:** in every other allocation decision (other character, fallback,
  story-forced, ACC/FOB/MGO), through `MGS_COOP_CHARACTER_BYPASS_OFF_V1`.
- **Not tied to session ownership, and deliberately not part of
  `RestoreGameplayRules`.** The player type is chosen in `OnAllocate`, before
  any session exists: the host creates its session inside `OnAllocate`, and
  the guest joins after mission start. The reset check runs on every
  `UpdatePartsStatus` for the local instance. A session-scoped bypass would
  turn Quiet back into Snake before the partner ever joins, and again after
  each disconnect. The bypass only affects a local player whose type is
  already 5 or 6. Outside FOB, only this override produces that type, because
  the vanilla Lua revert (`ForceChangePlayerFromOcelot`) still runs whenever
  the override is not active. FOB missions are excluded anyway, and bit 18 in
  FOB takes the same path the jump forces.
- **Not "always on when character=quiet".** Missions that fall back (story
  Quiet, buddy Quiet, story-forced Snake) get the original bytes back. A
  checkpoint reload that re-allocates while the bypass is active is
  confirmed, not refused (see the `IsActive()` clause).
- The patch is memory only. Nothing is restored at process exit, because the
  game image is discarded with the process.

## 5. Tests to update

- `tests/host_trial_patch_check.cpp` (diff below): the interval between
  `Mission.Start()` and the equipment anchor now contains the character
  chunk. The check verifies both original intervals around it, the chunk
  header at the exact insertion point, and the appended readback.
- `tests/host_trial_lua_check.lua` needs no change. It runs the composed
  TppMain up to `Mission.Start()`, which comes before the character chunk.
- If the test-hygiene package (`cmake/lua_checks.cmake`) is merged:
  `include(cmake/lua_checks.cmake)` should come **before** the branch's
  "Character selection" block at the end of `CMakeLists.txt`. That block then
  reuses the `lua51` target instead of building `mgs_character_lua51`. Its
  `MGS_MAIN_CHECKOUT_FALLBACK` cache variable has the same name and default as
  test-hygiene's. Optionally move the two `add_test` lines into
  `lua_checks.cmake`:

  ```cmake
  mgs_add_lua_check(character_lua_check "${_mgs_coop}/character.lua" "${_mgs_coop}/character_readback.lua")
  mgs_find_game_input(_mgs_tpp_player "startup-script-inspection-20260909/package-01.lua"
    7407bd41dd82913edfe9ef7fbb344f443adc7399d42f86c7798ba6b02c94e5b9)
  if(TARGET lua51 AND _mgs_tpp_player AND _mgs_tpp_main)
    add_test(NAME character_game_lua_check COMMAND lua51 "${CMAKE_CURRENT_SOURCE_DIR}/tests/character_lua_check.lua"
      "${_mgs_coop}/character.lua" "${_mgs_coop}/character_readback.lua" "${_mgs_tpp_player}" "${_mgs_tpp_main}")
  endif()
  ```

```diff
--- a/tests/host_trial_patch_check.cpp
+++ b/tests/host_trial_patch_check.cpp
@@ -34,9 +34,21 @@
         // insertion sites. Every interval of original code must survive intact.
         const auto equipment=source.find("TppEquip.StartLoadingToEquipMissionBlock()");
         const auto marker=source.find("TppMarker.RestoreMarkerLocator()");
-        check(resumed!=std::string::npos && equipment>insertion && marker>equipment);
-        check(output.substr(resumed,equipment-insertion)==source.substr(insertion,equipment-insertion));
-        const auto equipment_out=output.find(source.substr(equipment,marker-equipment),resumed);
+        // Character selection (lua/coop/character.lua) follows MissionPrepare.
+        const std::string character_anchor="if t(n.sequence.MissionPrepare)then\nn.sequence.MissionPrepare()end\n"
+            "if t(n.sequence.OnEndMissionPrepareSequence)then\n"
+            "TppSequence.SetOnEndMissionPrepareFunction(n.sequence.OnEndMissionPrepareSequence)end\nend\n";
+        const auto character_at=source.find(character_anchor);
+        check(character_at!=std::string::npos && source.find(character_anchor,character_at+1)==std::string::npos);
+        const auto character=character_at+character_anchor.size();
+        check(resumed!=std::string::npos && character>insertion && equipment>character && marker>equipment);
+        check(output.substr(resumed,character-insertion)==source.substr(insertion,character-insertion));
+        const auto character_out=resumed+(character-insertion);
+        const std::string character_header="\n-- MGS co-op character selection (original).";
+        check(output.compare(character_out,character_header.size(),character_header)==0);
+        const auto character_resumed=output.find(source.substr(character,equipment-character),character_out);
+        check(character_resumed!=std::string::npos);
+        const auto equipment_out=output.find(source.substr(equipment,marker-equipment),character_resumed);
         check(equipment_out!=std::string::npos);
         const std::string marker_call="TppMarker.RestoreMarkerLocator()";
         const auto marker_out=output.find(marker_call,equipment_out);
@@ -44,6 +56,7 @@
         const auto tail=marker+marker_call.size();
         check(output.substr(marker_out+marker_call.size()+5,source.size()-tail-8)==source.substr(tail,source.size()-tail-8));
         check(output.find("local receipts = 0",resumed)!=std::string::npos);
+        check(output.find("\n-- MGS co-op character readback (original).",equipment_out)!=std::string::npos);
         check(output.substr(output.size()-8) == "return e");
         check(!mgscoop::SelectHostTrialPatch(changed.data(), changed.size(), name, patch, true));
         check(!patch.bytes && !patch.size);
```

### Verified before handing over

The patch was applied to a scratch copy of the branch (minhook linked, not
modified) and checked:

- `tools/build.ps1 -Configuration Release`: 26/26 CTest suites pass.
- `host_trial_patch_check <extracted TppMain.lua> out.lua`: 24 exact-source checks pass.
- `lua51 tests/host_trial_lua_check.lua <TppMain.lua> out.lua`: 77 checks pass.
- The composed `out.lua` (the real DLL composition, every module) was run
  through `OnAllocate` with the real extracted `TppPlayer.lua` and permissive
  stubs, with `character=quiet`. Receipt order: `ALLOCATION_ENTER`,
  `MissionPrepare`, `CHARACTER_V1`, `BYPASS_ON_V1`, report, then
  `ocelot_reset_skipped` from the real end-of-allocation call.
  `vars.playerType` ends at 6 with parts 27 and camo 116.

## 6. Requests outside this branch

- **Player 2 default.** The lab scripts (`tools/lab/*`) and
  `tools/package-configure-role.ps1` should write
  `<guest exeDir>\mgs-coop\coop.ini` with `character=quiet` and
  `character_fallback=dd_male` when it does not exist, and leave the host's
  absent (game default). The parser already accepts this
  (`tests/lab_config_tests.cpp`).
- **Lab protocol.** Add to `docs/lab-protocol.md` "script-observer.log lines":
  `character_config=...`, `character=<skip|keep|apply|ocelot_reset_skipped|face|error>; ...`,
  `character_readback=<tag>; local=<n>; vars=<type>; i0=<n>; i1=<n>; quiet_ocelot_read_as=0`,
  `player_type_patch=<active|inactive|refused|failed>; ...`.
  Values contain no spaces; `; ` separates fields.

## 7. Live verification plan (single PC, host + guest)

1. Host `coop.ini`: none (or `character=snake`). Guest `coop.ini`:
   `character=quiet`. Both use `lab.ini` (enables readback).
2. Start a story mission that is not story-forced (for example 10036) on both
   instances. Expected on each side: `character_config=...`, then one
   `character=apply; ...` line. On the guest, `player_type_patch=active; branch_rva=0x9cd370 ...`
   and `character=ocelot_reset_skipped; ...`. The guest plays as Quiet and
   stays Quiet for 2+ minutes (the reset would otherwise fire within a frame
   of the parts state settling).
3. `python tools/inspect_player_system.py <pid> --output x.json` on each side:
   `player_type_branch` is `bypassed` on the guest and `original` on the host.
   `vars_appearance_raw.type_fb` is 6 on the guest. `appearance_raw` lists the
   local instance with type 6 / parts 27 / camo 116.
4. After the guest joins: `character_readback=change; ...` lines. Pass if the
   remote slot shows the partner's type. Quiet/Ocelot read as 0 through
   `GetPlayerType`, so use `appearance_raw` for them, and check visually that
   the partner renders as Quiet. Fail or unknown means the native synchronizer
   does not replicate outside FOB. Then the fallback is an appearance packet
   applied with `Player.SetPartsInfoAtInstanceIndex` (0x9DF530, no bounds
   check; clamp the index below P+0x208).
5. Return to the ACC: guest log shows `character=skip; ...; reason=mission_class`
   and `player_type_patch=inactive; ...`. The ACC shows the guest's own character.
6. Start 10050 as the guest: `applied=dd_male; reason=quiet_conflict` and no
   `player_type_patch=active`.

## 8. Static evidence summary (1.0.15.4, SHA-256 085c2f82...bb45)

- `UpdatePartsStatus` (Dynamite name `Player2GameObjectImpl::UpdatePartsStatus`):
  one primary and five chained unwind ranges covering `[0x9cd030, 0x9ce924)`.
  1,475 instructions decode with no gaps up to `0x9ce8e4`, followed by two
  switch tables. Two direct callers (`0x9c2a25`, `0x9c2d37`) in `0x9c1f00`.
  No base relocations.
- Reset path, for the local instance outside parts-load states 1/2: it reads
  `vars.playerType` (`[vars+0xfb]`). If the type is 5 or 6, and neither
  `enclosing+0x224` bit 18 (set at `0x9beb3a` from the FOB mission flag
  getter `0xa1f970`) nor `enclosing+0x284` bit 2 is set, it writes
  `vars.playerType = 0` and calls player vtable `+0x2a8`. Keep path
  `0x9cdb12`: OCELOT -> parts 0x1a / camo 0x73, QUIET -> parts 0x1b / camo 0x74.
- Patch: `jne 0x9cdb12` (`0f 85 9c 07 00 00`, the only occurrence in `.text`)
  becomes `jmp 0x9cdb12` (`48 e9 ...`; capstone decodes one 6-byte jmp). The
  site is word aligned and inside one cache line, so the change is one
  `_InterlockedCompareExchange16`. It maps from Dynamite c90f252 `0x1409cc6c0`
  on 1.0.15.3 (same rel32).
- Enums from the Player binding registration: PlayerType SNAKE 0 ... OCELOT 5,
  QUIET 6 (7 entries); PlayerPartsType OCELOT 26, QUIET 27; PlayerCamoType
  OCELOT 115, QUIET 116; PlayerHandType NORMAL 1.
- `Player.GetPlayerType{instanceIndex}` (`0x9d30b0`): reads byte
  `*(*(P+0x60)+0x40)[idx]` and returns 0..4. Values 5, 6 and any index
  `>= P+0x208` return 0.
- Game Lua: the vanilla TppMain/TppPlayer/TppMission sources extracted from
  this install (`build/*-20260909/package-0x.lua`, hashes in their
  inventories) define `RegisterTemporaryPlayerType{playerType, partsType,
  camoType, handEquip, faceEquipId}` (no faceId or staff fields), the
  save/apply/restore cycle, `ForceChangePlayerFromOcelot` and the OnAllocate
  order. The mission sequence scripts behind the story-forced list (s10010
  for 10010/10280, s10030, s10240, s10151 `Seq_Game_LoadSnakeForDD`) and
  TppVarInit come from third-party extracted copies (Infinite Heaven
  `fpkd-combined-lua`, mgsv-deminified-lua, Dynamite moddata). They were not
  extracted from this install.

## 9. Not verified (needs a live game)

- That the bypass keeps Quiet/Ocelot through the whole mission, and that the
  `enclosing+0x284` bit-2 guard has no other effect (it becomes unreachable
  while the jump is active; its producer is unmapped).
- That `vars.buddyType` at allocation reflects the deployed buddy.
- Remote appearance replication: synchronizer `0x12a5810` / `0x12cc790`,
  possibly FOB-gated.
- Quiet/Ocelot gameplay in story missions: abilities, sound, cutscenes, menus.
- That a DD face set after `MissionStartPlayerTypeSetting` is picked up when
  the player is realized.
- Interaction with mid-session checkpoint reloads.
