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
//   cfg                                         -> {on=0|1,cargo=0|1,pillion=0|1,shot=0|1}  (world_horse_share, M2's
//                                                  world_horse_cargo, M3's world_horse_pillion, M4's world_horse_shot)
//   read <mission> <epoch>                      -> {ready=true,x,y,z,yaw,occupied,rider,gen,life,maxlife} | {ready=false,reason}
//     (M3c: life / maxlife of record 0 from the receiver's life table, -1 when unreadable; SetMaxLife's loop, a pinned site)
//   place <mission> <epoch> <x> <y> <z> <yaw> <anchor 0|1>
//                                               -> {ready=true,bx,by,bz,byaw,x,y,z,anchored,gen} | refusal
//   hold0 <mission> <epoch> <x> <y> <z> <yaw> <reset 0..3> -> {ready=true,held=true} | refusal
//     (M2, world_horse_cargo: this copy's own downed body 0 lying on the horse, moved every frame as the partner
//     driver's xform mover moves body 1: T.vt+0x70 then T.vt+0x68 on body 0's transform, no C0 / A0 reset; the
//     partner contract's T vtable slots. A per-tick Lua Warp of body 0, which adds both resets, left it on the ground
//     under the moving horse for a second or more at a time: runs a49c4d76 / 20aef564 / d2a5cafa. With no reset body 0
//     stayed at its target but played the airborne pose (a242d867): reset bit 0 adds A0.vt+0x80(id), bit 1 C0.vt+0x90(id)
//     after the move, C0 first as Warp, each only when body 0's A0 / C0 class and slot target equal body 1's.)
//     BODY0 round: reset bits 2 / 4 and 3 / 8 add horse_body_watch's write watch and pin on body 0's transform (run
//     cfd2b604: the C0 reset keeps the lying pose but a later write of the frame dropped the body under the horse).
//   free0 <mission> <epoch>                     -> {ready=true,freed=true}  (the watch and pin end before a lay-down Warp)
//   hold1 <mission> <epoch> <x> <y> <z> <yaw> <reset 0..15> -> as hold0 for body 1 (world_horse_cargo, the carrier's copy:
//     the downed partner on its horse; also while this copy's player rides; run cfd2b604 drew body 1 low behind the rider)
//   pad <mission> <epoch>                       -> {ready=true,e=0|1,masked=0|1,held,bm,mag} | refusal  (M3, world_horse_pillion: the
//     local E before the pad masks: (src+0x48 ?: PP+0x460) & ACTION without & ~PP+0x28. The ride hold's ACTION mask
//     hides E from the downed poll (probe P5), so the rear-seat claim beside a partner-ridden horse reads it here.
//     M3c: held = the getter's word with the mask, bm = PP+0x28 (button mask), mag = PP+0x10 (move stick) * 1000.)
//   seat1 <mission> <epoch> <back_mm> <up_mm> <pose 0|1> -> {ready=true,seat=1} | refusal  (M3: this copy's player rides the
//     shared horse with the partner behind; leases kSeatMs: the partner driver draws body 1 at RearSeat of this copy's
//     own body 0 every tick, the DM overlay seating it, instead of the partner's stream.)
//   pbox <mission> <epoch> <x> <y> <z> <yaw>    -> {ready=true,box=true} | refusal  (M4, world_horse_shot: this copy's player
//     sits behind the partner; the placed census root and yaw of this copy's horse, leased kBoxMs to the seated-shot hook
//     (partner_seat_shot Class::horse): the player's own bullets start past the horse and the driver in front of them.)
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
// An anchored target is used only within kAnchorMaxM of the stream's (a stale or unrealized body never drags the horse).
inline constexpr float kAnchorMaxM = 1.5f;
bool AnchorUsable(const float (&stream)[3], const float (&anchored)[3]) noexcept;

enum class Verb : std::uint8_t { none, cfg, read, place, hold0, pad, seat1, free0, hold1, pbox };
struct Request {
    Verb verb{};
    std::uint32_t mission{}, epoch{};
    std::int32_t x{}, y{}, z{}, yaw{};
    std::uint8_t anchor{};
    std::uint8_t reset{};  // hold0: bit 0 A0.vt+0x80, bit 1 C0.vt+0x90 after the move (Warp's resets, C0 first);
                           // bit 2 the write watch, bit 3 the pin (native/include/mgscoop/horse_body_watch.hpp)
    std::int32_t back{}, up{};  // seat1: the rear seat behind (0..1500 mm) and over (-500..1500 mm) the driver's root
    std::uint8_t seat_pose{};   // seat1: 1 the DM overlay seats body 1 (stateRideOnIdle), 0 not (a lab test)
};
// The text after the prefix. Missions 30010 / 30020 only (free roam; Mother Base has no horse), epoch 1..65535,
// |x|,|y|,|z| <= 1e9 mm, |yaw| <= 31416 mrad, anchor 0 or 1.
bool Parse(const char* text, std::size_t size, Request& out) noexcept;

// A place's refusal in order (nullptr: place): off, pins, scope, census, unrealized, ridden, pending, cs.
struct PlaceScope {
    bool key{}, pins{}, field{}, census{}, realized{}, ridden{}, pending{}, cs{};
};
const char* PlaceRefusal(const PlaceScope& s) noexcept;
// A hold0's refusal in order (nullptr: hold): off (world_horse_share and world_horse_cargo), pins, scope, body (body 0's
// census block and transform chain), mounted (this copy's player rides).
struct HoldScope {
    bool share{}, cargo{}, pins{}, field{}, body{}, mounted{};
    bool pillion{};  // M3: world_horse_pillion admits hold0 too (the passenger's body 0 at the rear seat)
};
const char* HoldRefusal(const HoldScope& s) noexcept;

// Horse plan M3 (lab key world_horse_pillion): ride behind your partner. The prologue's rear rider sits about 0.4-0.5 m
// behind the front rider at the same height (vehicles-companions-design 3.3); probe P2 put a guard 0.44 m behind the
// census root. Lua's seat1 sends the offsets each tick; kRearBackMm / kRearUpMm are its defaults.
inline constexpr std::int32_t kRearBackMm = 450, kRearUpMm = 0;
inline constexpr std::uint32_t kSeatMs = 300;  // a seat1 lease
// A seat1's refusal in order (nullptr: lease): off (world_horse_pillion), pins, scope, not_riding (this copy's player is
// not the shared horse's rider).
struct SeatScope {
    bool pillion{}, pins{}, field{}, riding{};
};
const char* SeatRefusal(const SeatScope& s) noexcept;
// The E (ACTION, 0x20) the PlayerPad's held getter (0x1205F00) would return without its button mask: the device word when
// it is non-zero, else the keyboard word.
bool RawAction(std::uint32_t src_held, std::uint32_t kb_held) noexcept;
// The rear seat: back metres behind the rider root along yaw's forward (sin, cos) and up metres over it.
void RearSeat(const float (&rider)[3], float yaw, float back, float up, float (&out)[3]) noexcept;
void NoteSeat(std::int32_t back_mm, std::int32_t up_mm, bool pose, std::uint64_t now_ms) noexcept;
// The live lease (kSeatMs after the last seat1), its offsets in metres and whether the overlay seats body 1.
bool SeatLease(std::uint64_t now_ms, float& back, float& up, bool& pose) noexcept;
// M4 (run d935beae): a capture stalled the driver's game past kSeatMs and the lease lapsed for one tick, which ended the
// overlay seat. The partner driver keeps a lease that was live on its previous tick kSeatGraceTicks more ticks with its
// last offsets (in: this tick's SeatLease and its values; out: the values to use); a lease gone longer ends the seat.
inline constexpr unsigned kSeatGraceTicks = 2;
// The review of M4: no play run passed the driver's get-off with the hold in, so the grace is part of world_horse_shot
// (off unless named): grace false (the defaults) ends the seat on the first lapsed tick, as in 0.15.2.
struct SeatHold {
    bool grace{};  // world_horse_shot named on this copy (set by the partner driver each tick)
    bool live{};
    unsigned miss{};
    float back{}, up{};
    bool pose{true};
};
bool HoldSeat(SeatHold& h, bool lease, float& back, float& up, bool& pose) noexcept;
// Horse plan M4 (lab key world_horse_shot): the passenger's horse box for the seated-shot hook (pbox, mm / mrad), live
// kBoxMs after the last pbox on the partner driver's clock; the partner driver publishes it as partner_seat_shot's horse
// class while no vehicle passenger seat is live.
inline constexpr std::uint32_t kBoxMs = 300;
void NoteShotBox(std::int32_t x_mm, std::int32_t y_mm, std::int32_t z_mm, std::int32_t yaw_mrad, std::uint64_t now_ms) noexcept;
bool ShotBox(std::uint64_t now_ms, float (&pos)[3], float& yaw) noexcept;
// hold0's two engine calls: the T vtable and its +0x70 / +0x68 targets, pinned in native/partner_player_contract.hpp
// (vtable_slots {0x24c8ef0,0x70,0x1ad4450} / {0x24c8ef0,0x68,0x1ad56d0}, code_sites 0x1ad4450 / 0x1ad56d0: the partner
// code gate checks them at start); HoldBody refuses any other vtable or target.
inline constexpr std::uintptr_t kBodyTVtableRva = 0x24c8ef0, kBodySetRotRva = 0x1ad4450, kBodySetPoseRva = 0x1ad56d0;
// M3c: body 1's readback on the driver's copy against the rear seat it was given (partner_pillion=stats, 1 s): the worst
// xz distance and the height gap with the larger magnitude (body minus seat, m); non-finite readbacks are skipped.
struct SeatErr {
    float xz_max{}, dy_max{};
    unsigned n{};
};
void NoteSeatErr(SeatErr& e, const float (&seat)[3], const float (&body)[3]) noexcept;
// M3c: the D-Horse receiver's life table (the Lua dispatcher 0x140E4D250's SetMaxLife loop at 0x140E4D340, pinned in
// native/horse_drive_contract.hpp): *(receiver+0x30) -> +8 = entries of 0x28 bytes per record index, +0 life, +2 max life.
inline constexpr std::uintptr_t kLifeSiteRva = 0xe4d340;
constexpr std::uintptr_t LifeEntry(std::uintptr_t base, unsigned idx) noexcept { return base + std::uintptr_t(idx) * 0x28u; }
// Live: life / max of record idx (false when unreadable). Pins first (PinsReason).
bool ReadLife(std::uintptr_t receiver, unsigned idx, std::uint16_t& life, std::uint16_t& max) noexcept;
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
// hold0 (live): t = body 0's transform view (partner_player ResolveCensus block 0: t, t_vt, t70, t68), then
// T.vt+0x70(t, quat) and T.vt+0x68(t, pos, quat) as the partner driver's xform mover does for body 1 with reset none.
// Returns nullptr, or body (vtable / targets not the pinned ones) or call (an exception inside the engine call).
// c0 / c090 and a0 / a080: the reset objects and their slot targets (0: no such reset).
const char* HoldBody(std::uintptr_t image, std::uintptr_t t, std::uintptr_t t_vt, std::uintptr_t set_rot, std::uintptr_t set_pose,
                     float x, float y, float z, float yaw, std::uintptr_t c0, std::uintptr_t c090, std::uintptr_t a0,
                     std::uintptr_t a080, std::uint32_t id) noexcept;

} // namespace mgscoop::horse_drive
