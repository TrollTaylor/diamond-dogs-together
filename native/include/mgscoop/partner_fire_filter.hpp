#pragma once
// Native puppet weapons, fire visuals (docs/native-puppet.md "Weapons"). The
// partner body (player instance 1) fires through the game's own gun system
// when the remote presses FIRE; on the viewer's machine that shot must stay
// visual: fire animation, recoil, muzzle and "Shoot" effects, audio and
// casings, but no projectile, hit-scan, damage or impact (the remote's real
// shot already happened in the remote's world, and world sync carries its
// results).
//
// Mechanism (all addresses RVAs in mgsvtpp.exe 1.0.15.4, contract-verified by
// tools/generate_partner_contract.py --check and the code gate):
// - the model group's shot handler 0x1302F50 spawns every bullet through
//   RequestBullet 0xD32C60(iface, record) (calls 0x13031C8, 0x13031FA,
//   0x130343C) and plays sound and effects independently of it;
// - RequestBullet is reached only through slot 0 of the bullet iface vtable
//   0x2337DE8 (and 0x2337D18 while the system is destroyed); no direct call;
// - record+0x2A is the shooter's GameObjectId (stamped at 0x1041219 as
//   id & 0x1FF; 0x1302FF3 tests its type bits 0xFE00, 0 = player).
// Install swaps both vtable slots (one locked qword exchange each, read-only
// data, protection restored) to Hook, which drops a request whose owner is the
// armed instance id (counted) and forwards everything else to 0xD32C60. A
// dropped request returns 0, the value the original returns when its slot
// allocator yields nothing (0xD32CB4); no caller uses the return value.
// The non-local shoot path (rec+0x3C0 bit 0 clear, instance 1) never reaches
// the local-only side effects (ammo, camera kick, shot origin, shoot count,
// suppressor life): asserted by the contract checker.
//
// Not covered (documented risk): projectile weapons (row type 3, gm+0x210).
//
// Noise (InstallNoise, partner_native_noise=filter): the unsuppressed shot
// noise {pos, u8 0x45, u8 1, u16 owner = record+0x2A} the model group sends
// through group+0xA0 vt+0 (0x1303D06, rax ignored) is AddNoise 0x515270
// (slot 0 of the noise iface vtable 0x21876A8, stored by its ctor 0x51517F,
// dtor 0x515246 and the static initializer 0x2074C5C; no direct caller);
// AddNoise copies the record's +0x12 owner into its ring (0x5152F9). The
// slot is swapped the same way as the bullet slots; a record whose owner is
// the armed id is dropped (the world-sync rule: each PC perceives for its
// own player; the remote's real shot made the real noise on its machine).
//
// Installed once per process, never removed; once armed, the driver never
// disarms it (it re-arms the same id every tick): a shot request of instance 1
// queued while FIRE was allowed can reach RequestBullet after FIRE is masked
// again (mission end, drive change), so dropping instance 1's bullets for the
// process lifetime is the leak-proof choice. Instance 1 fires only under the
// native drive, and the local instance (id 0) is never the armed owner (the
// driver refuses an owner equal to ids[0]). Disarm exists for tests.
// Any thread may run Hook (the group update's thread is not established
// statically): atomics only.
#include <cstddef>
#include <cstdint>
#include "mgscoop/partner_seat_shot.hpp"

namespace mgscoop::partner_fire_filter {

using LogFn = void (*)(const char* line) noexcept;

// nullptr on success (or already installed), otherwise a static reason (also
// logged): bytes (the RequestBullet prologue differs) | slot (a slot does not
// hold 0xD32C60) | changed | protect | alignment | protect_restore |
// disabled (an earlier install failed). A refusal leaves both slots as they
// were (the first slot is swapped back when the second exchange fails).
const char* Install(const void* base, LogFn log) noexcept;
bool Installed() noexcept;
// True while both slots still hold Hook.
bool PatchLive(const void* base) noexcept;
// The noise slot (0x21876A8 +0, AddNoise 0x515270): nullptr on success, else
// bytes | slot | changed | protect | alignment | protect_restore | disabled.
// Uses the same armed owner as the bullet filter.
const char* InstallNoise(const void* base, LogFn log) noexcept;
bool NoiseInstalled() noexcept;
bool NoisePatchLive(const void* base) noexcept;

// The instance GameObjectId whose bullets are dropped (type bits must be 0:
// a player instance); Disarm (tests only) forwards every request again.
bool Arm(std::uint16_t owner) noexcept;
void Disarm() noexcept;
bool Armed() noexcept;

// Anim round 2: thrown service entry filter (vt+8 at 2429AD0/24299A0).
// Installed independently of the census. The armed owner stays latched across
// drain boundaries and key changes: a queued/unwindowed request must not create
// an object or overwrite the shared arc preview. Only owner 1 can be armed.
const char* InstallThrown(const void* base, LogFn log) noexcept;
bool ThrownPatchLive(const void* base) noexcept;
bool ArmThrown(std::uint16_t owner = 1) noexcept;
bool ThrownArmed(std::uint16_t owner = 1) noexcept;

// Process-lifetime totals (the driver logs deltas).
struct Stats {
    bool installed, live, armed;
    std::uint16_t owner;            // armed owner (0xFFFF when disarmed)
    std::uint32_t suppressed;       // requests dropped (owner == armed owner)
    std::uint32_t passed;           // requests forwarded
    std::uint32_t passed_player;    // ... of them with a player owner (type bits 0), e.g. the local instance 0
    std::uint32_t owner_disarmed;   // requests of the armed owner id forwarded while disarmed
    std::uint32_t last_thread;      // thread of the last request
    std::uint16_t last_owner;
    // The noise filter: installed, its slot still holds the hook, records
    // dropped (owner == armed owner) and forwarded, the last record's owner.
    bool noise_installed, noise_live;
    std::uint32_t noise_dropped, noise_passed;
    std::uint16_t noise_last_owner;
    bool thrown_installed, thrown_live, thrown_armed;
    std::uint16_t thrown_owner, thrown_last_owner;
    std::uint32_t thrown_dropped, thrown_preview_dropped, thrown_passed, thrown_unread;
};
void ReadStats(const void* base, Stats& out) noexcept;

// Read-only census of the services the filter relies on and of the shot
// noise interface it does not cover: svc = *(*(base+0x2C3A8A0)+8),
// bullets = *(*(svc+0x98)+0x218) (expected vtable 0x2337DE8),
// noise = *(*(svc+0xA8)+8) (group+0xA0; its vtable and slot 0 reported as
// RVAs when inside the image, else absolute). No call, no write.
struct Census {
    bool ok;                 // the chain was readable
    bool bullets_ok;         // bullets vtable == base + 0x2337DE8
    std::uintptr_t svc, bullets, bullets_vt, noise, noise_vt, noise_fn0;
    bool noise_vt_in_image, noise_fn0_in_image;
};
void ReadCensus(const void* base, std::size_t image_span, Census& out) noexcept;

// --- Anim round 1: the fire/projectile census (lab key partner_fire_census=1) ---
// Read-only records of every request the hooks see, for every owner (the local
// instance 0 included), before the filter's own verdict (which is unchanged):
// RequestBullet records (owner +0x2A, bullet id +0x2E, kind +0x30, sub +0x31,
// flags +0x32: 0x1302F50 fills them before the call), AddNoise records (owner
// +0x12, kind +0x10, flags +0x11) and, with the projectile census slot, the
// class-3 projectile requests 0x1303450 sends to *(*(svc+0x98)+0x210) vt+0 =
// 0x1337760 (owner +0x32 = the shot record's +0x2A, projectile id +0x2E, +0x34).
// With the census on, Install also swaps four forwarding slots (each in its
// service's live and base iface vtable, read-only data, no direct caller,
// contract-pinned): the projectile request above; the rocket punch (gun mode
// 53: the projectile service vt+0x58 = 0x1337920; +0x2C, +0x3E); thrown objects
// (gun mode 37 and its helper 0x1061D90: gm+0x200 vt+8 = 0x15E0230; the equip
// +0x3C, flags +0x3E); placed objects (gun mode 46: gm+0x208 vt+0x48 =
// 0xFCCD80; the equip +0x30, flags +0x3C). Every builder stamps the owner id
// & 0x1FF at +0x32. Armed body-1 thrown requests are dropped at the entry.
enum class CensusKind : std::uint8_t { bullet, noise, projectile, rocket, thrown, placed };
inline constexpr unsigned kCensusSlots = 4;  // projectile, rocket, thrown, placed (CensusKind - 2)
const char* CensusKindName(CensusKind kind) noexcept;
struct CensusEntry {
    std::uint32_t seq;      // write sequence (1-based); 0: an empty slot
    CensusKind kind;
    bool dropped;           // the owner filter dropped it (bullet/noise/thrown)
    bool fields_ok;         // the record's fields were readable
    std::uint16_t owner;
    std::uint16_t id;       // bullet id, projectile id, or the noise kind
    std::uint8_t b0, b1, b2;  // bullet: kind, sub, flags; noise: flags; projectile: 0
    std::uint16_t aux;      // projectile request +0x34
    std::uint32_t thread;
};
inline constexpr std::size_t kCensusRing = 256;
// Recording on/off (the driver sets it from partner_fire_census every tick).
void SetCensus(bool on) noexcept;
bool CensusOn() noexcept;
// Entries newer than cursor (the last seq consumed; advanced), at most max;
// lost counts entries overwritten before they were drained or torn reads.
std::size_t DrainCensus(std::uint32_t& cursor, CensusEntry* out, std::size_t max, std::uint32_t& lost) noexcept;
struct CensusTotals {
    bool slot_installed[kCensusSlots], slot_live[kCensusSlots];
    const char* slot_refusal[kCensusSlots];   // static, nullptr: installed or not tried
    std::uint32_t slot_requests[kCensusSlots];
    std::uint32_t bullets, noises, recorded;
};
void ReadCensusTotals(CensusTotals& out) noexcept;  // the slot check uses the base Install saw

// --- M7 seated shots, S1 (partner_seat_shot.hpp; docs/native-puppet.md "Seat shot (S1)") ---
// The driver publishes the local seated passenger every tick (a seqlock; PublishSeatShot stamps pub.ms with the
// hook's clock) and withdraws it when the report ends. Hook, for a request whose owner is the published owner (type
// bits 0, never the armed instance) and a publication at most MaxAgeMs(pub.speed) old: copies the record's first kRecordBytes,
// plans (partner_seat_shot::PlanShot) and, with pub.apply and a move, forwards the copy with its start moved; the
// caller's record is never written. Counters and the last plan for the driver's lines.
void PublishSeatShot(const partner_seat_shot::Pub& pub) noexcept;
void WithdrawSeatShot() noexcept;
bool SeatShotPublished() noexcept;
struct SeatShotStats {
    std::uint32_t seen;      // requests of the published owner while published
    std::uint32_t shifted;   // forwarded as a moved copy
    std::uint32_t census;    // planned a move with apply off (forwarded unchanged)
    std::uint32_t refused;   // planned no move (class, miss, clear, near, target, ...; forwarded unchanged)
    std::uint32_t stale;     // the publication was older than MaxAgeMs(its speed) (forwarded unchanged)
    std::uint32_t unread;    // the record was not readable (forwarded unchanged)
    std::uint32_t busy;      // the publication was being written (forwarded unchanged)
    std::uint32_t seq;       // the last plan's number (0: none yet)
    // The last plan (seq): its result word (static), the publication it used and its geometry.
    const char* result;
    bool apply, inside;
    std::uint8_t seat;
    std::uint16_t vehicle;
    float t, exit, dist, o_local[3], d_local[3];
    std::uint32_t age_ms, max_ms;  // the publication's age at the shot and its freshness window (MaxAgeMs)
};
// False while a hook is writing the last plan (the counters are still read).
bool ReadSeatShot(SeatShotStats& out) noexcept;
// The lab lever "seatshot on|off|lab|status" (partner_seat_shot::HandleCommand): this copy's override, -1 none.
int SeatShotOverride() noexcept;
void SetSeatShotOverride(int value) noexcept;

namespace detail {
// Test only: the clock PublishSeatShot stamps and Hook compares (nullptr: QueryPerformanceCounter in ms).
void SetSeatShotClockForTest(std::uint64_t (*clock)() noexcept) noexcept;
// The hook entry (vtable ABI: rcx = iface, rdx = record; returns rax).
using RequestFn = void* (*)(void* iface, const void* record) noexcept;
RequestFn HookEntry() noexcept;
// Test only: forget an install (the test frees its synthetic image) and
// redirect the forwarded call (the synthetic image holds no code).
void ResetForTest() noexcept;
// A census slot's hook entry (vtable ABI: rcx = the service iface, rdx = the
// request): slot 0 projectile, 1 rocket, 2 thrown, 3 placed.
using ProjectileFn = void* (*)(void* iface, const void* request) noexcept;
ProjectileFn SlotHookEntry(unsigned slot) noexcept;
void SetSlotOriginalForTest(unsigned slot, ProjectileFn original) noexcept;
// Records one census entry (the hooks' path; tests call it directly).
void RecordForTest(CensusKind kind, bool dropped, std::uint16_t owner, std::uint16_t id) noexcept;
void SetOriginalForTest(RequestFn original) noexcept;
// The noise hook entry (vtable ABI: rcx = the noise manager, rdx = the record).
using NoiseFn = void (*)(void* self, const void* record) noexcept;
NoiseFn NoiseHookEntry() noexcept;
void SetNoiseOriginalForTest(NoiseFn original) noexcept;
// Pure: whether a request with this owner is dropped for the armed owner
// (0xFFFF = disarmed).
inline bool Drops(std::uint16_t armed_owner, std::uint16_t owner) noexcept {
    return armed_owner != 0xFFFFu && (armed_owner & 0xFE00u) == 0 && owner == armed_owner;
}
}

} // namespace mgscoop::partner_fire_filter
