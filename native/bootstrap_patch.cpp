#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <cstring>
#include <initializer_list>
#include <utility>
#include "mgscoop/bootstrap_patch.hpp"
#include "bootstrap_source.hpp"
#include "archive_bootstrap_source.hpp"
#include "player_package_source.hpp"
#include "host_trial_source.hpp"
#include "equipment_source.hpp"
#include "marker_loading_source.hpp"
#include "host_admission_source.hpp"
#include "client_join_source.hpp"
#include "mission_trace_source.hpp"
#include "damage_source.hpp"
#include "enemy_targeting_source.hpp"
#include "session_lifecycle_source.hpp"
#include "session_feedback_source.hpp"
#include "partner_actions_source.hpp"
#include "partner_sync_source.hpp"
#include "partner_ride_source.hpp"
#include "world_sync_source.hpp"
#include "debug_menu_source.hpp"
#include "debug_equipment_source.hpp"
#include "debug_enemy_source.hpp"
#include "debug_player_source.hpp"
#include "debug_spawn_source.hpp"
#include "debug_buddy_source.hpp"
#include "debug_vehicle_source.hpp"
#include "debug_boot_source.hpp"
#include "respawn_source.hpp"
#include "character_source.hpp"

namespace mgscoop {
namespace {
constexpr char target_name[] = "@/Assets/tpp/script/lib/Tpp.lua";
constexpr std::size_t source_size = 15742;
constexpr char ending[] = "return e";
constexpr unsigned char source_sha256[] = {
    0xac,0x2c,0xa8,0xbf,0xaf,0x62,0x2d,0xcb,0x1b,0x5e,0x11,0x57,0x08,0x29,0xb4,0xd1,
    0xe9,0xce,0x98,0xb2,0x2e,0x4a,0x16,0x24,0x56,0x2a,0x59,0xff,0x2a,0x90,0x57,0x2b};
constexpr std::size_t patch_size = source_size + sizeof(bootstrap_source)-1;
constexpr std::size_t archive_patch_size = patch_size + sizeof(archive_bootstrap_source)-1;
static_assert(archive_patch_size <= script_bridge::max_replacement_bytes);
char patched[2][archive_patch_size]{};
SRWLOCK patch_lock = SRWLOCK_INIT;
bool ready[2]{};

bool SourceMatches(const char* source, std::size_t size, const unsigned char* expected) noexcept {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    unsigned char digest[32]{};
    bool ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0;
    if (ok) ok = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
    if (ok) ok = BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(source)),
                               static_cast<ULONG>(size), 0) >= 0;
    if (ok) ok = BCryptFinishHash(hash, digest, sizeof(digest), 0) >= 0;
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    return ok && std::memcmp(digest, expected, sizeof(digest)) == 0;
}
}

bool SelectBootstrapPatch(const char* source, std::size_t size, const char* name,
                          script_bridge::Replacement& out, bool archive_mounted) noexcept {
    out = {};
    if (!source || !name || size != source_size || std::strcmp(name, target_name) != 0 ||
        std::memcmp(source+size-(sizeof(ending)-1), ending, sizeof(ending)-1) != 0 ||
        !SourceMatches(source, size, source_sha256)) return false;
    AcquireSRWLockExclusive(&patch_lock);
    const unsigned variant = archive_mounted ? 1 : 0;
    auto* destination = patched[variant];
    const auto output_size = archive_mounted ? archive_patch_size : patch_size;
    if (!ready[variant]) {
        const auto prefix_size = source_size-(sizeof(ending)-1);
        std::memcpy(destination, source, prefix_size);
        std::memcpy(destination+prefix_size, bootstrap_source, sizeof(bootstrap_source)-1);
        if (archive_mounted)
            std::memcpy(destination+prefix_size+sizeof(bootstrap_source)-1,
                        archive_bootstrap_source, sizeof(archive_bootstrap_source)-1);
        std::memcpy(destination+output_size-(sizeof(ending)-1), ending, sizeof(ending)-1);
        ready[variant] = true;
    }
    out = {destination, output_size};
    ReleaseSRWLockExclusive(&patch_lock);
    return true;
}

bool SelectMissionTracePatch(const char* source, std::size_t size, const char* name,
                             script_bridge::Replacement& out, bool enabled) noexcept {
    constexpr std::size_t original_size = 21634;
    constexpr unsigned char digest[] = {
        0x65,0x42,0xd9,0x97,0xc3,0xf2,0x97,0x42,0xff,0x7e,0xa0,0x58,0x7f,0x7f,0x30,0x77,
        0x94,0x58,0x6a,0x9b,0x7a,0x8e,0xe3,0x11,0x4b,0x58,0xe7,0x9e,0x7f,0x0b,0x61,0x03};
    out = {};
    if (!enabled || !source || !name || size != original_size ||
        std::strcmp(name,"@/Assets/tpp/script/lib/TppMain.lua") != 0 ||
        std::memcmp(source+size-8,"return e",8)!=0 || !SourceMatches(source,size,digest)) return false;
    constexpr std::size_t result_size = original_size + sizeof(mission_trace_source)-1;
    static_assert(result_size <= script_bridge::max_replacement_bytes);
    static char result[result_size]{};
    static bool initialized = false;
    AcquireSRWLockExclusive(&patch_lock);
    if (!initialized) {
        std::memcpy(result,source,size-8);
        std::memcpy(result+size-8,mission_trace_source,sizeof(mission_trace_source)-1);
        std::memcpy(result+result_size-8,"return e",8);
        initialized=true;
    }
    out={result,result_size};
    ReleaseSRWLockExclusive(&patch_lock);
    return true;
}

bool SelectDeathPolicyPatch(const char* source,std::size_t size,const char* name,
                           script_bridge::Replacement& out,bool enabled) noexcept {
    constexpr std::size_t original_size=104102;
    constexpr unsigned char digest[]={0xe9,0x9e,0x6a,0xdd,0xd4,0xc5,0x13,0x5f,0xb2,0xd7,0x7a,0xb5,0x86,0x32,0xd5,0xe3,
        0x49,0xa6,0x26,0xeb,0x7f,0x10,0xea,0x38,0x30,0x13,0x72,0xd1,0x27,0xf7,0x40,0x4a};
    constexpr auto result_size=original_size+sizeof(death_policy_source)-1;
    static_assert(result_size<=script_bridge::max_replacement_bytes);
    static char result[result_size]{};
    static bool initialized=false;
    out={};
    if(!enabled || !source || !name || size!=original_size ||
       std::strcmp(name,"@/Assets/tpp/script/lib/TppMission.lua")!=0 ||
       std::memcmp(source+size-8,"return e",8)!=0 || !SourceMatches(source,size,digest)) return false;
    AcquireSRWLockExclusive(&patch_lock);
    if(!initialized) {
        std::memcpy(result,source,size-8);
        std::memcpy(result+size-8,death_policy_source,sizeof(death_policy_source)-1);
        std::memcpy(result+result_size-8,"return e",8);
        initialized=true;
    }
    out={result,result_size};
    ReleaseSRWLockExclusive(&patch_lock);
    return true;
}

bool SelectHostTrialPatch(const char* source, std::size_t size, const char* name,
                          script_bridge::Replacement& out, bool enabled) noexcept {
    constexpr std::size_t original_size=21634;
    constexpr unsigned char digest[] = {
        0x65,0x42,0xd9,0x97,0xc3,0xf2,0x97,0x42,0xff,0x7e,0xa0,0x58,0x7f,0x7f,0x30,0x77,
        0x94,0x58,0x6a,0x9b,0x7a,0x8e,0xe3,0x11,0x4b,0x58,0xe7,0x9e,0x7f,0x0b,0x61,0x03};
    constexpr char anchor[]="Mission.Start()";
    constexpr char equipment_anchor[]="TppEquip.StartLoadingToEquipMissionBlock()";
    constexpr char marker_anchor[]="TppMarker.RestoreMarkerLocator()";
    constexpr char marker_guard[]="\nif not (Tpp and type(Tpp.MgsCoop)==\"table\" and Tpp.MgsCoop.markerLoadingGuardActive==true) then\n";
    constexpr char marker_guard_end[]="\nend\n";
    constexpr auto marker_extra=sizeof(marker_guard)-1+sizeof(marker_guard_end)-1;
    // Character selection runs after the mission's MissionPrepare and before
    // the OnLoad/DeclareSVars/MissionStartPlayerTypeSetting part of OnAllocate.
    constexpr char character_anchor[]="if t(n.sequence.MissionPrepare)then\nn.sequence.MissionPrepare()end\n"
        "if t(n.sequence.OnEndMissionPrepareSequence)then\n"
        "TppSequence.SetOnEndMissionPrepareFunction(n.sequence.OnEndMissionPrepareSequence)end\nend\n";
    constexpr auto character_extra=sizeof(character_source)-1;
    // World sync (lua/coop/world_*.lua): seven chunks, in this order, right after partner sync.
#if defined(MGS_LOCAL_BUDDY_CAPACITY_TEST) && MGS_LOCAL_BUDDY_CAPACITY_TEST
    constexpr std::size_t buddy_ride_size=sizeof(world_buddy_ride_source)-1;
#else
    constexpr std::size_t buddy_ride_size=0;
#endif
    constexpr std::size_t world_size=sizeof(world_sync_source)-1+sizeof(world_enemy_source)-1+sizeof(world_outcome_source)-1+
        sizeof(world_phase_source)-1+sizeof(world_env_source)-1+sizeof(world_probe_source)-1+sizeof(world_goto_source)-1+sizeof(world_follow_seed_source)-1+sizeof(world_follow_source)-1+sizeof(world_handoff_source)-1+sizeof(world_align_source)-1+sizeof(world_recovery_source)-1+sizeof(world_downed_source)-1+sizeof(world_sleep_clock_source)-1+sizeof(world_partner_sleep_source)-1+sizeof(world_friendly_source)-1+sizeof(world_revive_input_source)-1+sizeof(world_buddy_source)-1+sizeof(world_ride_pose_source)-1+sizeof(world_horse_cargo_source)-1+buddy_ride_size+sizeof(world_place_source)-1+sizeof(world_place_guest_source)-1+sizeof(world_vehicle_source)-1+sizeof(world_vehicle_state_source)-1+sizeof(world_companion_source)-1+sizeof(world_companion_dog_source)-1+sizeof(world_companion_guard_source)-1+sizeof(world_relief_source)-1+sizeof(world_relief_sync_source)-1;
    constexpr std::size_t result_size=original_size+sizeof(host_trial_source)-1+sizeof(equipment_loading_source)-1+marker_extra+character_extra+sizeof(host_admission_source)-1+sizeof(client_join_source)-1+sizeof(damage_lifecycle_source)-1+sizeof(enemy_targeting_source)-1+sizeof(respawn_source)-1+sizeof(session_lifecycle_source)-1+sizeof(session_feedback_source)-1+sizeof(partner_actions_source)-1+sizeof(partner_ride_source)-1+sizeof(partner_sync_source)-1+world_size+sizeof(debug_menu_source)-1+sizeof(debug_equipment_source)-1+sizeof(debug_enemy_source)-1+sizeof(debug_player_source)-1+sizeof(debug_spawn_source)-1+sizeof(debug_buddy_source)-1+sizeof(debug_vehicle_source)-1+sizeof(debug_boot_source)-1+sizeof(marker_loading_source)-1+sizeof(character_readback_source)-1+sizeof(mission_trace_source)-1;
    static_assert(result_size<=script_bridge::max_replacement_bytes);
    static char result[result_size]{};
    static bool initialized=false;
    out={};
    if(!enabled || !source || !name || size!=original_size ||
       std::strcmp(name,"@/Assets/tpp/script/lib/TppMain.lua")!=0 ||
       !SourceMatches(source,size,digest)) return false;
    // Insertions are pinned by the complete original source hash. Require
    // unique, ordered anchors; the original calls remain in the output once.
    std::size_t insertion=0,found=0,equipment_insertion=0,equipment_found=0;
    std::size_t marker_insertion=0,marker_found=0,character_insertion=0,character_found=0;
    for(std::size_t i=0;i+sizeof(anchor)-1<=size;++i)
        if(std::memcmp(source+i,anchor,sizeof(anchor)-1)==0) { insertion=i; ++found; }
    for(std::size_t i=0;i+sizeof(character_anchor)-1<=size;++i)
        if(std::memcmp(source+i,character_anchor,sizeof(character_anchor)-1)==0) { character_insertion=i+sizeof(character_anchor)-1; ++character_found; }
    for(std::size_t i=0;i+sizeof(equipment_anchor)-1<=size;++i)
        if(std::memcmp(source+i,equipment_anchor,sizeof(equipment_anchor)-1)==0) { equipment_insertion=i; ++equipment_found; }
    for(std::size_t i=0;i+sizeof(marker_anchor)-1<=size;++i)
        if(std::memcmp(source+i,marker_anchor,sizeof(marker_anchor)-1)==0) { marker_insertion=i; ++marker_found; }
    if(found!=1 || equipment_found!=1 || marker_found!=1 || character_found!=1 || character_insertion<=insertion ||
       equipment_insertion<=character_insertion || marker_insertion<=equipment_insertion ||
       marker_insertion+sizeof(marker_anchor)-1>=size-8) return false;
    AcquireSRWLockExclusive(&patch_lock);
    if(!initialized) {
        std::memcpy(result,source,insertion);
        std::memcpy(result+insertion,host_trial_source,sizeof(host_trial_source)-1);
        std::memcpy(result+insertion+sizeof(host_trial_source)-1,source+insertion,character_insertion-insertion);
        const auto character=character_insertion+sizeof(host_trial_source)-1;
        std::memcpy(result+character,character_source,character_extra);
        std::memcpy(result+character+character_extra,source+character_insertion,equipment_insertion-character_insertion);
        const auto equipment=equipment_insertion+sizeof(host_trial_source)-1+character_extra;
        std::memcpy(result+equipment,equipment_loading_source,sizeof(equipment_loading_source)-1);
        std::memcpy(result+equipment+sizeof(equipment_loading_source)-1,source+equipment_insertion,marker_insertion-equipment_insertion);
        const auto marker=marker_insertion+sizeof(host_trial_source)-1+character_extra+sizeof(equipment_loading_source)-1;
        std::memcpy(result+marker,marker_guard,sizeof(marker_guard)-1);
        std::memcpy(result+marker+sizeof(marker_guard)-1,source+marker_insertion,sizeof(marker_anchor)-1);
        const auto after_marker=marker+sizeof(marker_guard)-1+sizeof(marker_anchor)-1;
        std::memcpy(result+after_marker,marker_guard_end,sizeof(marker_guard_end)-1);
        std::memcpy(result+after_marker+sizeof(marker_guard_end)-1,source+marker_insertion+sizeof(marker_anchor)-1,
            size-marker_insertion-(sizeof(marker_anchor)-1)-8);
        constexpr auto appended=original_size-8+sizeof(host_trial_source)-1+character_extra+sizeof(equipment_loading_source)-1+marker_extra;
        std::memcpy(result+appended,host_admission_source,sizeof(host_admission_source)-1);
        std::memcpy(result+appended+sizeof(host_admission_source)-1,client_join_source,sizeof(client_join_source)-1);
        constexpr auto lifecycle=appended+sizeof(host_admission_source)-1+sizeof(client_join_source)-1;
        std::memcpy(result+lifecycle,damage_lifecycle_source,sizeof(damage_lifecycle_source)-1);
        constexpr auto targeting=lifecycle+sizeof(damage_lifecycle_source)-1;
        std::memcpy(result+targeting,enemy_targeting_source,sizeof(enemy_targeting_source)-1);
        constexpr auto session=targeting+sizeof(enemy_targeting_source)-1;
        std::memcpy(result+session,respawn_source,sizeof(respawn_source)-1);
        constexpr auto feedback=session+sizeof(respawn_source)-1;
        std::memcpy(result+feedback,session_feedback_source,sizeof(session_feedback_source)-1);
        // Offline partner spike (lua/coop/partner_sync.lua): right after the
        // session feedback; inert unless MGS_COOP_PARTNER_CFG_V1 answers true.
        constexpr auto action_events=feedback+sizeof(session_feedback_source)-1;
        std::memcpy(result+action_events,partner_actions_source,sizeof(partner_actions_source)-1);
        constexpr auto ride=action_events+sizeof(partner_actions_source)-1;
        std::memcpy(result+ride,partner_ride_source,sizeof(partner_ride_source)-1);
        constexpr auto partner=ride+sizeof(partner_ride_source)-1;
        std::memcpy(result+partner,partner_sync_source,sizeof(partner_sync_source)-1);
        // World sync (lua/coop/world_*.lua): inert unless MGS_COOP_WORLD_CFG_V1
        // answers level >= 1. Spliced after partner sync, so its wrappers sit
        // outside partner's and inside the session lifecycle's.
        auto world=partner+sizeof(partner_sync_source)-1;
        for(const auto& chunk : {std::pair<const char*,std::size_t>{world_sync_source,sizeof(world_sync_source)-1},
                                 std::pair<const char*,std::size_t>{world_enemy_source,sizeof(world_enemy_source)-1},
                                 std::pair<const char*,std::size_t>{world_outcome_source,sizeof(world_outcome_source)-1},
                                 std::pair<const char*,std::size_t>{world_phase_source,sizeof(world_phase_source)-1},
                                 std::pair<const char*,std::size_t>{world_env_source,sizeof(world_env_source)-1},
                                 std::pair<const char*,std::size_t>{world_probe_source,sizeof(world_probe_source)-1},
                                 std::pair<const char*,std::size_t>{world_goto_source,sizeof(world_goto_source)-1},
                                 std::pair<const char*,std::size_t>{world_follow_seed_source,sizeof(world_follow_seed_source)-1},
                                 std::pair<const char*,std::size_t>{world_follow_source,sizeof(world_follow_source)-1},
                                 std::pair<const char*,std::size_t>{world_handoff_source,sizeof(world_handoff_source)-1},
                                 std::pair<const char*,std::size_t>{world_align_source,sizeof(world_align_source)-1},
                                 std::pair<const char*,std::size_t>{world_recovery_source,sizeof(world_recovery_source)-1},
                                 std::pair<const char*,std::size_t>{world_downed_source,sizeof(world_downed_source)-1},
                                 std::pair<const char*,std::size_t>{world_sleep_clock_source,sizeof(world_sleep_clock_source)-1},
                                 std::pair<const char*,std::size_t>{world_partner_sleep_source,sizeof(world_partner_sleep_source)-1},
                                 std::pair<const char*,std::size_t>{world_revive_input_source,sizeof(world_revive_input_source)-1},
                                 std::pair<const char*,std::size_t>{world_friendly_source,sizeof(world_friendly_source)-1},
                                 std::pair<const char*,std::size_t>{world_buddy_source,sizeof(world_buddy_source)-1},
                                 std::pair<const char*,std::size_t>{world_ride_pose_source,sizeof(world_ride_pose_source)-1},
                                 std::pair<const char*,std::size_t>{world_horse_cargo_source,sizeof(world_horse_cargo_source)-1},
#if defined(MGS_LOCAL_BUDDY_CAPACITY_TEST) && MGS_LOCAL_BUDDY_CAPACITY_TEST
                                 std::pair<const char*,std::size_t>{world_buddy_ride_source,sizeof(world_buddy_ride_source)-1},
#endif
                                 std::pair<const char*,std::size_t>{world_place_source,sizeof(world_place_source)-1},
                                 std::pair<const char*,std::size_t>{world_place_guest_source,sizeof(world_place_guest_source)-1},
                                 std::pair<const char*,std::size_t>{world_vehicle_source,sizeof(world_vehicle_source)-1},
                                 std::pair<const char*,std::size_t>{world_vehicle_state_source,sizeof(world_vehicle_state_source)-1},
                                 std::pair<const char*,std::size_t>{world_companion_source,sizeof(world_companion_source)-1},
                                 std::pair<const char*,std::size_t>{world_companion_dog_source,sizeof(world_companion_dog_source)-1},
                                 std::pair<const char*,std::size_t>{world_companion_guard_source,sizeof(world_companion_guard_source)-1},
                                 std::pair<const char*,std::size_t>{world_relief_source,sizeof(world_relief_source)-1},
                                 std::pair<const char*,std::size_t>{world_relief_sync_source,sizeof(world_relief_sync_source)-1}}) {
            std::memcpy(result+world,chunk.first,chunk.second);
            world+=chunk.second;
        }
        constexpr auto debug=partner+sizeof(partner_sync_source)-1+world_size;
        std::memcpy(result+debug,debug_menu_source,sizeof(debug_menu_source)-1);
        constexpr auto equipment_debug=debug+sizeof(debug_menu_source)-1;
        std::memcpy(result+equipment_debug,debug_equipment_source,sizeof(debug_equipment_source)-1);
        constexpr auto enemy_debug=equipment_debug+sizeof(debug_equipment_source)-1;
        std::memcpy(result+enemy_debug,debug_enemy_source,sizeof(debug_enemy_source)-1);
        constexpr auto player_debug=enemy_debug+sizeof(debug_enemy_source)-1;
        std::memcpy(result+player_debug,debug_player_source,sizeof(debug_player_source)-1);
        constexpr auto spawn_debug=player_debug+sizeof(debug_player_source)-1;
        std::memcpy(result+spawn_debug,debug_spawn_source,sizeof(debug_spawn_source)-1);
        constexpr auto buddy_debug=spawn_debug+sizeof(debug_spawn_source)-1;
        std::memcpy(result+buddy_debug,debug_buddy_source,sizeof(debug_buddy_source)-1);
        constexpr auto vehicle_debug=buddy_debug+sizeof(debug_buddy_source)-1;
        std::memcpy(result+vehicle_debug,debug_vehicle_source,sizeof(debug_vehicle_source)-1);
        // The developer test boot (lua/coop/debug_boot.lua, docs/test-boot.md): inert unless
        // MGS_COOP_BOOT_CFG_V1 answers (the owned private worker of a worker build, lab mode).
        constexpr auto boot_debug=vehicle_debug+sizeof(debug_vehicle_source)-1;
        std::memcpy(result+boot_debug,debug_boot_source,sizeof(debug_boot_source)-1);
        constexpr auto marker_lifecycle=boot_debug+sizeof(debug_boot_source)-1;
        std::memcpy(result+marker_lifecycle,marker_loading_source,sizeof(marker_loading_source)-1);
        constexpr auto readback=marker_lifecycle+sizeof(marker_loading_source)-1;
        std::memcpy(result+readback,character_readback_source,sizeof(character_readback_source)-1);
        std::memcpy(result+readback+sizeof(character_readback_source)-1,mission_trace_source,sizeof(mission_trace_source)-1);
        // The operational termination signal must run before every other
        // wrapper, independently of diagnostic limits or tracing errors.
        constexpr auto session_lifecycle=readback+sizeof(character_readback_source)-1+sizeof(mission_trace_source)-1;
        std::memcpy(result+session_lifecycle,session_lifecycle_source,sizeof(session_lifecycle_source)-1);
        static_assert(session_lifecycle+sizeof(session_lifecycle_source)-1==result_size-8);
        std::memcpy(result+result_size-8,"return e",8);
        initialized=true;
    }
    out={result,result_size};
    ReleaseSRWLockExclusive(&patch_lock);
    return true;
}

bool SelectDamageParameterPatch(const char* source,std::size_t size,const char* name,
                                script_bridge::Replacement& out,bool enabled) noexcept {
    constexpr std::size_t original_size=82834;
    constexpr unsigned char digest[]={0xe8,0x8e,0xd1,0xe3,0x9f,0xbb,0x68,0x9b,0xcd,0xdb,0x75,0xaf,0x11,0x53,0x51,0x24,
        0x55,0x71,0xec,0xf2,0xa7,0x78,0x67,0x5a,0xb1,0x57,0xd6,0xc4,0x02,0x3d,0x0f,0xa3};
    constexpr char prefix[]="this={}TppDamage.ReloadDamageParameter";
    constexpr char replaced_prefix[]="this={} local mgsDamage=";
    constexpr char tail[]="return this";
    constexpr std::size_t result_size=original_size-(sizeof(prefix)-1)+(sizeof(replaced_prefix)-1)+sizeof(damage_parameters_source)-1;
    static_assert(result_size<=script_bridge::max_replacement_bytes);
    static char result[result_size]{};
    static bool initialized=false;
    out={};
    if(!enabled || !source || !name || size!=original_size ||
       std::strcmp(name,"@/Assets/tpp/level_asset/damage/ParameterTables/DamageParameterTables.lua") ||
       std::memcmp(source,prefix,sizeof(prefix)-1) || std::memcmp(source+size-(sizeof(tail)-1),tail,sizeof(tail)-1) ||
       !SourceMatches(source,size,digest)) return false;
    AcquireSRWLockExclusive(&patch_lock);
    if(!initialized) {
        constexpr auto table_size=original_size-(sizeof(prefix)-1)-(sizeof(tail)-1);
        std::memcpy(result,replaced_prefix,sizeof(replaced_prefix)-1);
        std::memcpy(result+sizeof(replaced_prefix)-1,source+sizeof(prefix)-1,table_size);
        constexpr auto suffix=sizeof(replaced_prefix)-1+table_size;
        std::memcpy(result+suffix,damage_parameters_source,sizeof(damage_parameters_source)-1);
        std::memcpy(result+result_size-(sizeof(tail)-1),tail,sizeof(tail)-1);
        initialized=true;
    }
    out={result,result_size};
    ReleaseSRWLockExclusive(&patch_lock);
    return true;
}

bool SelectPlayerPackagePatch(const char* source, std::size_t size, const char* name,
                              script_bridge::Replacement& out, bool enabled) noexcept {
    constexpr std::size_t original_size = 4500;
    constexpr unsigned char digest[] = {
        0x85,0x12,0x0c,0x16,0xa6,0xcb,0x27,0xd8,0xa7,0x32,0x76,0x3b,0x02,0x3f,0x45,0x12,
        0x5e,0xdd,0x70,0x20,0x1b,0xef,0x18,0x26,0x95,0x85,0x67,0x56,0xee,0x4c,0xb0,0x0e};
    constexpr std::size_t result_size = original_size + sizeof(player_package_source)-1;
    static_assert(result_size <= script_bridge::max_replacement_bytes);
    static char result[result_size]{};
    static bool initialized = false;
    out = {};
    if (!enabled || !source || !name || size != original_size ||
        std::strcmp(name, "@/Assets/tpp/script/lib/TppPackList.lua") != 0 ||
        std::memcmp(source+size-8, "return e", 8) != 0 ||
        !SourceMatches(source, size, digest)) return false;
    AcquireSRWLockExclusive(&patch_lock);
    if (!initialized) {
        std::memcpy(result, source, original_size-8);
        std::memcpy(result+original_size-8, player_package_source, sizeof(player_package_source)-1);
        std::memcpy(result+result_size-8, "return e", 8);
        initialized = true;
    }
    out = {result, result_size};
    ReleaseSRWLockExclusive(&patch_lock);
    return true;
}
}
