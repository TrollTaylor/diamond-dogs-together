#pragma once
// MGS_COOP_HORSE_DRIVE_V1 (horse plan M1, plan B; lab key world_horse_share; docs/mounts.md "Shared D-Horse").
// The other copy places its D-Horse (6656, record 0) every frame from the rider's RIDE_FAST stream. SetCallHorse
// cannot do it (probe P1: rear-ups, a lost call during state 1, p95 0.6-1.7 m), so the receipt performs only the
// pose part of the D-Horse call-enter (0x140E6AA90, state 2 enter with rec+0x2E3 pending, 0x140E6AB6B..0x140E6AF7D):
//   1. census pose[idx] = x, y, z, yaw (receiver+0x18 kind, +0x20 positions, idx * 16), and
//   2. group = receiver+0x110[slot / receiver+0xF0] (receiver vt+0x78, 0x140E4C540), CS = group+0x10 (vtable
//      0x1422E6070), CS vt+0xA8 (0x140AF35F0 -> 0x140B6ECF0)(CS, slot, &matrix): the engine's own write at 0x140E6AF3F.
// It never sets rec+0x2E3 / +0x2E4 (no call run, no route, no AI state change), refuses while this copy's player
// rides the horse (rec+0x2DD: the rider beats the owner) or a call is pending, and checks every site and slot of
// native/horse_drive_contract.hpp (tools/generate_horse_drive_contract.py) before the one engine call.
// Verbs (Lua -> native, after "return 'MGS_COOP_HORSE_DRIVE_V1' -- "):
//   cfg                                         -> {on=0|1}  (the lab key; both PCs must run it)
//   read <mission> <epoch>                      -> {ready=true,x,y,z,yaw,occupied,rider,gen} | {ready=false,reason}
//   place <mission> <epoch> <x> <y> <z> <yaw> <anchor 0|1>
//                                               -> {ready=true,bx,by,bz,byaw,x,y,z,anchored,gen} | refusal
// mm / mrad. anchor 1: when the partner driver shows body 1 as a horse rider (partner_mount_policy) and its body
// sample is fresh, the horse goes under that body (HorseUnderRider), the yaw from the stream: the rider sits in the
// saddle on this screen whatever the two streams' delays are.
#include <cstddef>
#include <cstdint>

namespace mgscoop::horse_drive {

inline constexpr std::uint16_t kHorseId = 6656;
inline constexpr std::uint32_t kFreshMs = 500;   // the partner driver shows the rider this long after a place
inline constexpr std::uint32_t kAnchorMs = 200;  // a rider body sample anchors a place this long
// Probe P2 (run fa31b4f7): the rider root sits 0.10 m forward and 0.49 m up of the census root.
inline constexpr float kSeatForward = 0.10f, kSeatUp = 0.49f;

// The call-enter's matrix (rows: x axis, y axis, z axis, translation; row-major 4x4 as at 0x140E6AF3F):
// (cos,0,-sin,0) (0,1,0,0) (sin,0,cos,0) (x,y,z,1). Forward is (sin yaw, 0, cos yaw), as atan2(dx, dz).
void Matrix(float x, float y, float z, float yaw, float (&m)[16]) noexcept;
// The census root under a rider body at `body` facing `yaw`: body minus the seat offset turned by yaw.
void HorseUnderRider(const float (&body)[3], float yaw, float (&out)[3]) noexcept;

enum class Verb : std::uint8_t { none, cfg, read, place };
struct Request {
    Verb verb{};
    std::uint32_t mission{}, epoch{};
    std::int32_t x{}, y{}, z{}, yaw{};
    std::uint8_t anchor{};
};
// The text after the prefix. Missions 30010 / 30020 only (free roam; Mother Base has no horse), epoch 1..65535,
// |x|,|y|,|z| <= 1e9 mm, |yaw| <= 31416 mrad, anchor 0 or 1.
bool Parse(const char* text, std::size_t size, Request& out) noexcept;

// A place's refusal in order (nullptr: place): off, pins, scope, census, unrealized, ridden, pending, cs.
struct PlaceScope {
    bool key{}, pins{}, field{}, census{}, realized{}, ridden{}, pending{}, cs{};
};
const char* PlaceRefusal(const PlaceScope& s) noexcept;
// The census identity (records, receiver, realized mask): a change ends every seat (Lua's horse generation).
std::uint32_t Generation(std::uintptr_t records, std::uintptr_t receiver, std::uint32_t mask) noexcept;

// Runtime-thread state shared by the receipt and partner_driver.
void NotePlaced(std::uint64_t now_ms) noexcept;
bool PlacedFresh(std::uint64_t now_ms) noexcept;
void NoteRiderBody(const float (&p)[3], std::uint64_t now_ms) noexcept;
bool RiderBody(std::uint64_t now_ms, float (&p)[3]) noexcept;
void Reset() noexcept;

// Live (mgs_partner_player): the contract's sites and slots in this image, then the write. receiver / kind come
// from a buddy_capacity_census::ReadPlayPose of this tick. Returns nullptr on success, else a static reason
// (pins, receiver, kind, unrealized, cs, slot, call). Runtime thread only.
const char* PinsReason(std::uintptr_t image, std::size_t span) noexcept;
const char* Write(std::uintptr_t image, std::size_t span, std::uintptr_t receiver, std::uintptr_t kind, std::uint16_t idx,
                  float x, float y, float z, float yaw) noexcept;

} // namespace mgscoop::horse_drive
