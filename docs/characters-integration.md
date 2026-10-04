# Character selection: integration steps

For the orchestrator. Branch `wp/characters` adds the character feature as
new, self-contained files. This document lists the exact changes to the three
files the branch does not own. Apply them **after the lab-observer package
merges**. The full change set is in
[`characters-integration.patch`](characters-integration.patch):

```
git apply docs/characters-integration.patch
```

Checked with `git apply --check` (and then applied, built and tested, see
"Verified before handing over") on all of these bases:

- `wp/lab-observer` fa08f9e merged with `wp/characters`
- `wp/lab-observer` 4dffdac merged with `wp/characters`
- `coop-lab` + `wp/characters` alone
- `wp/lab-observer` + `wp/characters` + `wp/test-hygiene`

The game_script_observer.cpp hunks sit at anchors that lab-observer does not
change. For that reason the include hunk now goes after
`mgscoop/marker_loading_patch.hpp`, not after lab-observer's own includes, and
the patch no longer adds a second `mgscoop/lab_config.hpp` include.
`character_receipt.hpp` already includes it.

The patch applies with either line ending. If a later commit on another
branch breaks one file's hunks, apply the other files on their own and do the
failing one by hand from the anchors below:

```
git apply --include=native/bootstrap_patch.cpp docs/characters-integration.patch
git apply --include=native/game_script_observer.cpp docs/characters-integration.patch
git apply --include=tests/host_trial_patch_check.cpp docs/characters-integration.patch
```

No CMake change is needed. The branch's `cmake/characters.cmake` already
generates `generated/character_source.hpp`, adds the sources and registers
the tests (section 2).

## 1. What the branch adds (already merged with the branch)

| File | Role |
|---|---|
| `native/player_type_patch.cpp`, `native/include/mgscoop/player_type_patch.hpp` | `mgscoop::player_type::SetActive(base, span, enabled, log)` / `IsActive()`: scoped bypass of the native Ocelot/Quiet -> Snake reset. `SampleTypes(base, span, sample)`: read-only guard for the lab readback (section 4). Linked into `mgs_player_ghost_patch`, which `dinput8.dll` already links. |
| `native/player_type_contract.hpp` | Generated contract. `UpdatePartsStatus` extent `[0x9cd030, 0x9ce924)` (6388 bytes), SHA-256 `eb2c6b98...a8aa`, site `0x9cd370`, `0f 85 9c 07 00 00` -> `48 e9 9c 07 00 00`. Readback guard: the exact 219-byte `Player.GetPlayerType` body (`0x9d30b0`), root getter `0xaec8d0`, player slot `0x2a82c30`, player type `0x2295490`, offsets `0x208`/`0x60`/`0x40`. |
| `tools/generate_player_type_contract.py`, `docs/evidence/player-type-contract-2026-10-03.json` | Static re-verification and evidence (see section 8). |
| `native/character_receipt.cpp`, `native/include/mgscoop/character_receipt.hpp` | Pure receipt helpers: `Classify`, `FormatQuery`, `NeedsResetBypass`, `FormatLogLine`, `FormatPlayersReply`. |
| `lua/coop/character.lua` | Inline chunk spliced into `TppMain.OnAllocate` (step 3). |
| `lua/coop/character_readback.lua` | Appended `TppMain` wrapper. It starts the lab readback after the original `OnMissionGameStart` returns, polls after `OnUpdate` and stops at `OnTerminate` (step 3). |
| `native/character_source.hpp.in` | `configure_file` template producing `character_source` and `character_readback_source`. |
| `cmake/characters.cmake` + one `include()` line after `enable_testing()` in `CMakeLists.txt` | All CMake wiring (section 2). |
| `tests/player_type_patch_tests.cpp`, `tests/character_receipt_tests.cpp`, `tests/character_lua_check.lua` | Registered in CTest by `cmake/characters.cmake`. |
| `tools/inspect_player_system.py` (extended) | Live read-only readback: per-instance type/parts/camo/hand/face, vars appearance bytes and the branch state. |

Nothing in the branch is reachable from the DLL until steps 3 and 4 are
applied. With them applied and no `coop.ini`, behaviour stays as before: the
query returns `''`, and the Lua does nothing else. That holds even with
`lab.ini`, because the readback is enabled only for an apply/keep decision. The
native bypass is never touched in that case.

## 2. CMake (done by the branch; nothing to apply)

`CMakeLists.txt` gets exactly one line, after `enable_testing()`:

```cmake
include(cmake/characters.cmake) # character selection; its target part is deferred to the end of this file
```

`cmake/characters.cmake` adds `native/character_receipt.cpp` to `mgs_coop_core`
and `native/player_type_patch.cpp` to `mgs_player_ghost_patch` with
`target_sources`. It registers `character_receipt_tests`,
`player_type_patch_tests`, `character_lua_check` and `character_game_lua_check`,
and runs `configure_file(native/character_source.hpp.in generated/character_source.hpp @ONLY)`
when `mgs_bootstrap_patch` exists. All of this happens in a
`cmake_language(DEFER)` call at the end of the top-level file, so every target
exists by then, including test-hygiene's `lua51` when that package is merged
(it is reused). The module enables C at include time only when it finds a
Lua 5.1.5 source, because languages cannot be enabled during deferred
execution. With this layout the branch merges with lab-observer, test-hygiene,
lab-tools and steam-shim without a CMake conflict (checked pairwise with
`git merge-tree`). Earlier the shared `mgs_coop_core` line conflicted with
lab-observer.

The raw-string delimiters are `MGS_CHARACTER` and `MGS_CHAR_READ` (MSVC allows
at most 16 characters). `character.lua` is about 15 KB. `character_lua_check`
fails if either file reaches 16,000 bytes (the MSVC C2026 limit is 16,380 per
literal).

## 3. `native/bootstrap_patch.cpp`: splice into the TppMain host composition

`SelectHostTrialPatch` (exact TppMain.lua, 21,634 bytes, SHA-256 `6542d997...6103`):

- **Insert** right after this anchor. It is unique in the pinned source, at
  offset 3,749, and the insertion point is the following byte:

  ```lua
  if t(n.sequence.MissionPrepare)then
  n.sequence.MissionPrepare()end
  if t(n.sequence.OnEndMissionPrepareSequence)then
  TppSequence.SetOnEndMissionPrepareFunction(n.sequence.OnEndMissionPrepareSequence)end
  end
  ```

  This position is after `Mission.Start()` (the host-trial anchor, offset
  2,649) and after the mission's `MissionPrepare`, where story missions call
  `RegisterTemporaryPlayerType`. It is before the `OnLoad` loop, `DeclareSVars`,
  `TppVarInit.InitializeForNewMission` (which registers the Subsistence parts),
  the first `TppPlayer.MissionStartPlayerTypeSetting()` (offset 4,583),
  `TppEquip.StartLoadingToEquipMissionBlock()` (the equipment anchor, 6,764)
  and `TppPlayer.ForceChangePlayerFromOcelot()` (7,236). It sits at statement
  level in `OnAllocate`, outside the `if n.sequence` block, so it runs on every
  allocation. That keeps the bypass state current; for example, the bypass is
  switched off in the ACC.
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
@@ -137,7 +138,13 @@ bool SelectHostTrialPatch(const char* source, std::size_t size, const char* name
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
@@ -148,31 +155,37 @@ bool SelectHostTrialPatch(const char* source, std::size_t size, const char* name
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
@@ -185,10 +198,12 @@ bool SelectHostTrialPatch(const char* source, std::size_t size, const char* name
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

## 4. `native/game_script_observer.cpp`: receipts, the native bypass and the readback guard

The patch adds:

- Two includes, after `#include "mgscoop/marker_loading_patch.hpp"`.
- One helper, `SelectCharacterReceipt`, after `OnMissionThread()`.
- One dispatch in `ObserveEntry`, right after
  `constexpr char released[]="return 'MGS_COOP_HOST_RELEASE_V1'";`.
- One startup log line in `InstallGameScriptObserver`, after the archive-mode
  `Log` block and before the hook install.

```diff
--- a/native/game_script_observer.cpp
+++ b/native/game_script_observer.cpp
@@ -19,6 +19,8 @@
 #include "mgscoop/result_patch.hpp"
 #include "mgscoop/full_close_patch.hpp"
 #include "mgscoop/marker_loading_patch.hpp"
+#include "mgscoop/character_receipt.hpp"
+#include "mgscoop/player_type_patch.hpp"
 #include "mgscoop/soldier_patch.hpp"
 #include "mgscoop/mission_termination.hpp"
 #include "mgscoop/mission_initialization.hpp"
@@ -232,6 +234,55 @@ bool OnRuntimeThread() noexcept {
 }
 bool OnTerminationThread() noexcept { return termination_scope.OnThread(GetCurrentThreadId()); }
 bool OnMissionThread() noexcept { return OnAllocationThread() || OnRuntimeThread() || OnTerminationThread(); }
+
+// Character selection receipts (lua/coop/character.lua, docs/characters.md).
+// The query is read-only and formatted once from coop.ini (constant for the
+// process). The Ocelot/Quiet reset bypass is enabled only for a configured
+// Ocelot/Quiet during an accepted allocation (an already active bypass is
+// confirmed, e.g. for a checkpoint reload); disabling is always allowed.
+// The players guard (lab only) samples the player object read-only so the
+// readback calls Player.GetPlayerType, which has no null check, only while
+// that object exists. Report/readback receipts are log-only, never replaced.
+char character_query[mgscoop::character::max_line]{};
+std::size_t character_query_size = 0;
+thread_local char character_players[64]{};
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
+    if(kind==Receipt::players) {
+        mgscoop::player_type::TypeSample sample{"lab_disabled",0,0,{0,0}};
+        const bool ready=mgscoop::GetLabConfig().present && mgscoop::player_type::SampleTypes(game_base,game_span,sample);
+        const auto length=mgscoop::character::FormatPlayersReply(ready,sample.stage,sample.limit,sample.type,sample.count,
+                                                                  character_players,sizeof(character_players));
+        if(!length) return false;
+        replacement={character_players,length};
+        return true;
+    }
+    char line[mgscoop::character::max_line]{};
+    if(mgscoop::character::FormatLogLine(kind,buffer,size,line,sizeof(line))) Log(line);
+    return false;
+}
 bool TransferRuleThread(DWORD previous_thread) noexcept {
     // Both directions are needed: allocation -> runtime for gameplay/cleanup,
     // then runtime -> a later allocation worker for retained cleanup. These
@@ -714,6 +765,11 @@ bool ObserveEntry(void*, const char* buffer, std::size_t size, const char* name,
     constexpr char admission[]="return 'MGS_COOP_HOST_ADMISSION_V1'";
     constexpr char admission_observed[]="return 'MGS_COOP_HOST_ADMISSION_OBSERVED_V1'";
     constexpr char released[]="return 'MGS_COOP_HOST_RELEASE_V1'";
+    if(!selected && (host_create_enabled || client_enabled)) {
+        const auto character_kind=mgscoop::character::Classify(buffer,size,name);
+        if(character_kind!=mgscoop::character::Receipt::none)
+            selected=SelectCharacterReceipt(character_kind,buffer,size,replacement);
+    }
     if(host_admission_enabled && (OnRuntimeThread() || OnTerminationThread()) && buffer && name && size==sizeof(released)-1 &&
        std::memcmp(buffer,released,size)==0 && std::strcmp(name,released)==0)
         ForgetDynamiteHostAdmission();
@@ -850,6 +906,16 @@ void InstallGameScriptObserver(void* base, std::size_t span, const wchar_t* dire
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
| `return 'MGS_COOP_CHARACTER_V1'` | `return '<character>','<fallback>',<readback>`, built once from `GetCoopConfig()`. `<character>` is `''` for default. `<readback>` is `GetLabConfig().present`. | `host_create_enabled \|\| client_enabled` |
| `return 'MGS_COOP_CHARACTER_BYPASS_ON_V1'` | `return true` only if all of these hold: `NeedsResetBypass(coop)` (configured `ocelot`/`quiet`), `OnAllocationThread()` or the bypass is already active, and `player_type::SetActive(base, span, true)` succeeded. Otherwise `return false`, and the Lua uses the fallback. | same |
| `return 'MGS_COOP_CHARACTER_BYPASS_OFF_V1'` | `return true` if `player_type::SetActive(..., false)` succeeded (a no-op when the bytes are not owned) | same |
| `return 'MGS_COOP_CHARACTER_PLAYERS_V1'` | `return true,<limit>,<raw0>[,<raw1>]` when `GetLabConfig().present` and `player_type::SampleTypes` reports `ready`. Otherwise `return false,'<stage>'` (`lab_disabled`, `code_gate`, `span`, `player_absent`, `player_type`, `player_limit`, `parts_info_absent`, `type_array_absent`, `*_read`, `type_array_unreadable`). Built by `FormatPlayersReply` into a `thread_local` buffer. | same |
| `return 'MGS_COOP_CHARACTER_REPORT_V1' -- <payload>` | not replaced; logs `character=<payload with "; ">` | same; payload `[A-Za-z0-9_=:,.;-]{1,192}` |
| `return 'MGS_COOP_CHARACTER_READBACK_V1' -- <payload>` | not replaced; logs `character_readback=...` | same |

When the DLL does not handle the query (an older build or another mode), the
Lua receives the literal `'MGS_COOP_CHARACTER_V1'` and stops
(`native_unavailable`). When it does not handle the players guard, the Lua
receives the literal and never calls `Player.GetPlayerType`. The readback then
logs `players=native_unavailable`.

### Why the readback needs the guard

`Player.GetPlayerType` (`0x9d30b0`) loads `P = *(base+0x2a82c30)`, compares
the index with `[P+0x208]`, then reads `Q = [P+0x60]` and `[Q+0x40][idx]`.
None of these loads is checked for null. Vanilla Lua never calls this
function. The repo's live evidence shows `P` is null before `Mission.Start()`
and at `OnInitialize` entry (docs/host-readiness-timing.md), and that it was
read as 0 after returning to the title. `TppMain.OnUpdate` already ticks while
a mission loads, before `OnMissionGameStart`. A fault inside the binding is not
a Lua error, so `pcall` cannot contain it.

The readback is therefore limited in three ways:

1. **Lua lifetime.** Readback is enabled only for an apply/keep decision with
   `lab.ini` present, and never in title mode. It starts only after the
   original `OnMissionGameStart` returns, which the live preflight observed
   with the player present. It stops at `OnTerminate` entry and is reset by
   every allocation.
2. **Native sample.** Right before calling the binding, on the same thread,
   the Lua asks for `MGS_COOP_CHARACTER_PLAYERS_V1`. `SampleTypes` first
   compares the loaded binding and root getter byte for byte with the
   contract. It then reads the same pointer chain through `ReadProcessMemory`,
   so it never dereferences anything directly. It requires the player type
   `base+0x2295490`, a limit of 1..4 and non-null `Q` and array pointers.
3. **Index bound.** The Lua calls `Player.GetPlayerType` only for indices
   below `min(limit, 2)` of a `ready` sample. Without a ready sample it logs
   `na`.

What remains is a teardown on another thread between the sample and the call
a few Lua instructions later. That window is not excluded statically. See
section 9.

### Where `player_type::SetActive` is called, and why that scope

- **On:** only through `MGS_COOP_CHARACTER_BYPASS_ON_V1`. The Lua sends it
  only when this mission's decision is Ocelot or Quiet. The native side checks
  again that `coop.ini` asked for Ocelot/Quiet and that the receipt comes from
  the accepted allocation thread.
- **Off:** in every other allocation decision, through
  `MGS_COOP_CHARACTER_BYPASS_OFF_V1`. That covers another character, the
  fallback, story-forced Snake, story-sequence Snake, title mode, and
  ACC/FOB/MGO.
- **Not tied to session ownership, and deliberately not part of
  `RestoreGameplayRules`.** The player type is chosen in `OnAllocate`, before
  any session exists: the host creates its session inside `OnAllocate`, and
  the guest joins after mission start. The reset check runs on every
  `UpdatePartsStatus` for the local instance. A session-scoped bypass would
  turn Quiet back into Snake before the partner ever joins, and again after
  each disconnect. The bypass only affects a local player whose type is
  already 5 or 6. Outside FOB, only this override produces that type, because
  the vanilla Lua revert (`ForceChangePlayerFromOcelot`) still runs whenever
  the override is not active. FOB missions are excluded anyway, and in FOB
  bit 18 sends the code down the same path the jump forces.
- **Not "always on when character=quiet".** Missions that fall back (story
  Quiet, buddy Quiet, story-forced Snake) get the original bytes back. A
  checkpoint reload that re-allocates while the bypass is active is confirmed,
  not refused (see the `IsActive()` clause).
- The patch is memory only. Nothing is restored at process exit, because the
  game image is discarded with the process.

## 5. Tests to update

- `tests/host_trial_patch_check.cpp` (diff below). The interval between
  `Mission.Start()` and the equipment anchor now contains the character
  chunk. The check verifies both original intervals around it, the chunk
  header at the exact insertion point, and the appended readback.
- `tests/host_trial_lua_check.lua` needs no change. It runs the composed
  TppMain up to `Mission.Start()`, which comes before the character chunk.
- Optional end-to-end check of the complete DLL composition: give
  `character_lua_check.lua` the output of `host_trial_patch_check` as a 5th
  argument. It runs that composition through `OnAllocate`, about 300 loading
  `OnUpdate` ticks, `OnMissionGameStart`, play, `OnTerminate` and teardown,
  using the real `TppPlayer.lua` and a simulated `GetPlayerType` that faults
  while the player is absent:

  ```
  host_trial_patch_check <TppMain.lua> composed.lua
  mgs_character_lua51 tests/character_lua_check.lua lua/coop/character.lua lua/coop/character_readback.lua <TppPlayer.lua> <TppMain.lua> composed.lua
  ```
- test-hygiene (`cmake/lua_checks.cmake`) needs no ordering change any more.
  `cmake/characters.cmake` runs deferred and reuses `lua51` automatically. It
  also honours `MGS_LUA_SOURCE_DIR` and `MGS_GAME_LUA_DIR`.

```diff
--- a/tests/host_trial_patch_check.cpp
+++ b/tests/host_trial_patch_check.cpp
@@ -34,9 +34,21 @@ int main(int argc, char** argv) {
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
@@ -44,6 +56,7 @@ int main(int argc, char** argv) {
         const auto tail=marker+marker_call.size();
         check(output.substr(marker_out+marker_call.size()+5,source.size()-tail-8)==source.substr(tail,source.size()-tail-8));
         check(output.find("local receipts = 0",resumed)!=std::string::npos);
+        check(output.find("\n-- MGS co-op character readback (original).",equipment_out)!=std::string::npos);
         check(output.substr(output.size()-8) == "return e");
         check(!mgscoop::SelectHostTrialPatch(changed.data(), changed.size(), name, patch, true));
         check(!patch.bytes && !patch.size);
```

### Verified before handing over

All of this ran in scratch clones; the branch's worktree and the installed
game were not touched. Bases: `wp/lab-observer` fa08f9e (also 4dffdac) merged
with `wp/characters`, then this patch applied. minhook was linked, not
modified.

- `tools/build.ps1 -Configuration Release`: 29/29 CTest suites pass on
  fa08f9e, and 28/28 on 4dffdac. The difference is lab-observer's own suites.
- `host_trial_patch_check <extracted TppMain.lua> composed.lua`: 24
  exact-source checks pass.
- `lua51 tests/host_trial_lua_check.lua <TppMain.lua> composed.lua`: 77 checks
  pass.
- `character_lua_check.lua ... composed.lua`: 325 checks pass. These include
  the complete DLL composition with `character=quiet` and with the default
  character. Quiet is applied; no `GetPlayerType` call happens while the
  player is absent (loading or teardown); there are exactly 6 calls while
  started; the default character never sends the players guard.

## 6. Requests outside this branch

- **Player 2 default.** The lab scripts (`tools/lab/*`) and
  `tools/package-configure-role.ps1` should write
  `<guest exeDir>\mgs-coop\coop.ini` with `character=quiet` and
  `character_fallback=dd_male` when the file does not exist, and leave the
  host's absent (game default). The parser already accepts this
  (`tests/lab_config_tests.cpp`).
- **Lab protocol.** Add these lines to `docs/lab-protocol.md` under
  "script-observer.log lines":
  - `character_config=<name|default>; fallback=<name>; coop_ini=<0|1>; reset_bypass_needed=<0|1>; applied_in=TppMain.OnAllocate`
  - `character=<skip|keep|apply|ocelot_reset_skipped|face|error>; mission=...; requested=...; applied=...; type=...; reason=...; bypass=<0|1>; real=...`
  - `character_readback=<start|change|manual>; local=<n>; vars=<type>; players=<ready|stopped|native_unavailable|stage>; limit=<n|na>; i0=<n|na|err>; i1=<n|na|err>; raw0=<n|na>; raw1=<n|na>; quiet_ocelot_read_as=0`
  - `player_type_patch=<active|inactive|refused|failed>; ...`

  Values contain no spaces; `; ` separates fields. New `reason=` values are
  `story_sequence_snake` and `title_mode`.

## 7. Live verification plan (single PC, host + guest)

1. Host `coop.ini`: none (or `character=snake`). Guest `coop.ini`:
   `character=quiet`. Both use `lab.ini`, which enables the readback.
2. Start a story mission that is not story-forced (for example 10036) on both
   instances. Expected on each side: `character_config=...`, then one
   `character=apply; ...` line. On the guest, also
   `player_type_patch=active; branch_rva=0x9cd370 ...` and
   `character=ocelot_reset_skipped; ...`. The guest plays as Quiet and stays
   Quiet for 2+ minutes; without the bypass, the reset would fire within a
   frame of the parts state settling.
3. No crash while the missions load, and no `character_readback=` line before
   mission start. The first line is `character_readback=start; ...; players=ready; limit=2 ...`.
   If it reads `players=player_absent` at start, the player object appears
   later than `OnMissionGameStart`. That is safe (nothing is called) but worth
   recording.
4. `python tools/inspect_player_system.py <pid> --output x.json` on each side:
   `player_type_branch` is `bypassed` on the guest and `original` on the host.
   On the guest, `vars_appearance_raw.type_fb` is 6, and `appearance_raw`
   lists the local instance with type 6 / parts 27 / camo 116.
5. After the guest joins, `character_readback=change; ...` lines appear. Pass
   if the remote slot's `raw` value shows the partner's type. Quiet and Ocelot
   read as 0 through `GetPlayerType` (`i0`/`i1`), so use `raw0`/`raw1`, and
   check visually that the partner renders as Quiet. A fail, or an unknown
   result, means the native synchronizer does not replicate outside FOB. The
   fallback then is an appearance packet applied with
   `Player.SetPartsInfoAtInstanceIndex` (0x9DF530). That function has no bounds
   check, so clamp the index below P+0x208.
6. Return to the ACC. The guest log shows `character=skip; ...; reason=mission_class`
   and `player_type_patch=inactive; ...`, and no readback lines during the
   transition. The ACC shows the guest's own character.
7. Start 10050 as the guest: `applied=dd_male; reason=quiet_conflict` and no
   `player_type_patch=active`. On a save right after the prologue, start
   10020: `character=keep; ...; reason=story_sequence_snake`.

## 8. Static evidence summary (1.0.15.4, SHA-256 085c2f82...bb45)

- `UpdatePartsStatus` (Dynamite name `Player2GameObjectImpl::UpdatePartsStatus`)
  has one primary and five chained unwind ranges covering
  `[0x9cd030, 0x9ce924)`. 1,475 instructions decode with no gaps up to
  `0x9ce8e4`, followed by two switch tables. It has two direct callers
  (`0x9c2a25`, `0x9c2d37`), both in `0x9c1f00`. The exe has no base
  relocations.
- Reset path, for the local instance outside parts-load states 1/2: the code
  reads `vars.playerType` (`[vars+0xfb]`). If the type is 5 or 6, and neither
  `enclosing+0x224` bit 18 nor `enclosing+0x284` bit 2 is set, it writes
  `vars.playerType = 0` and calls player vtable `+0x2a8`. Bit 18 is set at
  `0x9beb3a` from the FOB mission flag getter `0xa1f970`. Keep path
  `0x9cdb12`: OCELOT -> parts 0x1a / camo 0x73, QUIET -> parts 0x1b / camo 0x74.
- Patch: `jne 0x9cdb12` (`0f 85 9c 07 00 00`, the only occurrence in `.text`)
  becomes `jmp 0x9cdb12` (`48 e9 ...`; capstone decodes it as one 6-byte jmp).
  The site is word aligned and inside one cache line, so the change is one
  `_InterlockedCompareExchange16`. It maps from Dynamite c90f252 `0x1409cc6c0`
  on 1.0.15.3 (same rel32).
- Enums from the Player binding registration: PlayerType SNAKE 0 ... OCELOT 5,
  QUIET 6 (7 entries); PlayerPartsType OCELOT 26, QUIET 27; PlayerCamoType
  OCELOT 115, QUIET 116; PlayerHandType NORMAL 1.
- `Player.GetPlayerType{instanceIndex}` (`0x9d30b0`, 219 bytes, pinned in the
  contract) calls getter `0xaec8d0`, which is `lea rax,[0x2a82c28]; ret`. It
  loads `r14 = [rax+8]` and runs `cmp esi,[r14+0x208]`; if the index is not
  below that, it returns 0. It then loads `[r14+0x60]`, then `[rax+0x40]`, and
  reads the byte `[rax+idx]`, mapping 1..4 through and everything else to 0.
  There is no null test on `r14` or on the loaded pointers. The player slot
  lies in a writable data section, and the player vtable `0x2295490` lies in a
  read-only one, with slots 2/11 = `0x9c39f0`/`0x9c3bb0`. These are the same
  slots `inspect_player_system.py` and `host_session_trial.cpp`
  `PlayerReadiness` check live.
- Game Lua. The vanilla TppMain, TppPlayer and TppMission sources come from
  this install (`build/*-20260909/package-0x.lua`, hashes in their
  inventories). They define:
  - `RegisterTemporaryPlayerType{playerType, partsType, camoType, handEquip, faceEquipId}`, which has no faceId or staff fields
  - the save/apply/restore cycle
  - `MissionStartPlayerTypeSetting`, which forces SNAKE/NORMAL_SCARF/TIGERSTRIPE while `TppStory.GetCurrentStorySequence() == CLEARD_ESCAPE_THE_HOSPITAL`; a temporary type applied afterwards overrides that
  - `ForceChangePlayerFromOcelot`
  - the OnAllocate order, including `MissionStartPlayerTypeSetting` in title mode

  The mission sequence scripts behind the story-forced list (s10010 for
  10010/10280, s10030, s10240, s10151 `Seq_Game_LoadSnakeForDD`) and
  TppVarInit come from third-party extracted copies (Infinite Heaven
  `fpkd-combined-lua`, mgsv-deminified-lua, Dynamite moddata). They were not
  extracted from this install.

## 9. Not verified (needs a live game)

- That the bypass keeps Quiet/Ocelot through the whole mission, and that the
  `enclosing+0x284` bit-2 guard has no other effect. That guard becomes
  unreachable while the jump is active, and its producer is unmapped.
- That `vars.buddyType` at allocation reflects the deployed buddy.
- Remote appearance replication through synchronizer `0x12a5810` /
  `0x12cc790`, which may be FOB-gated.
- Quiet/Ocelot gameplay in story missions: abilities, sound, cutscenes, menus.
- That a DD face set after `MissionStartPlayerTypeSetting` is picked up when
  the player is realized.
- Interaction with mid-session checkpoint reloads.
- The readback guard's live behaviour. Open questions:
  - whether the player object exists at the end of `OnMissionGameStart` in
    every mode (it was observed at entry in 30050)
  - whether `OnTerminate` runs before the player object is destroyed on
    every exit path
  - whether another thread can free it in the few Lua instructions between
    the native sample and the `Player.GetPlayerType` call

  Without `lab.ini` or a non-default `character=`, the binding is never
  called.
