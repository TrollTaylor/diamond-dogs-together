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
// Not covered (documented risks): projectile weapons (row type 3, gm+0x210)
// and the unsuppressed shot noise {pos, 0x145, owner} through group+0xA0
// (0x1303D06): ReadCensus reports that interface's vtable read-only so it can
// be pinned.
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

// The instance GameObjectId whose bullets are dropped (type bits must be 0:
// a player instance); Disarm (tests only) forwards every request again.
bool Arm(std::uint16_t owner) noexcept;
void Disarm() noexcept;
bool Armed() noexcept;

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

namespace detail {
// The hook entry (vtable ABI: rcx = iface, rdx = record; returns rax).
using RequestFn = void* (*)(void* iface, const void* record) noexcept;
RequestFn HookEntry() noexcept;
// Test only: forget an install (the test frees its synthetic image) and
// redirect the forwarded call (the synthetic image holds no code).
void ResetForTest() noexcept;
void SetOriginalForTest(RequestFn original) noexcept;
// Pure: whether a request with this owner is dropped for the armed owner
// (0xFFFF = disarmed).
inline bool Drops(std::uint16_t armed_owner, std::uint16_t owner) noexcept {
    return armed_owner != 0xFFFFu && (armed_owner & 0xFE00u) == 0 && owner == armed_owner;
}
}

} // namespace mgscoop::partner_fire_filter
