#pragma once
// D7 (docs/vehicles-companions-design.md 6.12, 8.9; user decision 2026-10-29): the guest's copy of the host's
// D-Dog cannot be hurt on the guest's PC and follows the host dog's life state. Original code.
// Static evidence (capstone, exe 085c2f82...bb45): the dog's damage handler (factory 0xC89110, secondary vtable
// 0x231AD30) adds every hit to the dog's life record in ProcessDamages (slot 0x231AD38 -> 0xC89510), which the
// generic DamageControllerImpl update (0xDA55F0) calls only for an instance with hits; the dog's life controller
// (factory 0xCA4900, secondary vtable 0x231FD60) applies a record's pending damage every frame (slot 0x231FD60 ->
// 0xCA4B90 -> 0xCA4C40) and posts "Dead" for 0x2600|index when the record's state becomes 2. The dog has no
// invincibility command (dispatchers 0xC7D220, 0xC56770, 0xCB9CC0) and the damage parameters are shared by
// every target, so the guard is a filter on those two slots, armed only on the guest for instance 0.
#include <cstddef>
#include <cstdint>
namespace mgscoop::dog_guard {
struct Pin {std::uintptr_t rva; std::size_t bytes; const char* sha256;};
inline constexpr Pin kPins[]={
 {0xc89510,1707,"36044db9a88f0e5b98e98be051a124e57ba97cdf05b44740e6218c5f40c31c73"}, // ProcessDamages incl. cold chunks (pending life +4, stun +0xA)
 {0xc89110,92,"17cfab7267965dc136ff993c0ea93ee59b25339d1162e643c7722900381fdc31"}, // damage handler factory binding vtables 0x231AD00/0x231AD30
 {0xca4900,164,"9e8d00ab1c65741b9019d11c8f5c84b4ebbed7b820d6e93d9a455b32ef1da463"}, // life controller factory binding vtables 0x231FD30/0x231FD60
 {0xca49b0,37,"71f2474b5e4e4e5ea3638605b6170c7728cdde39ca7bf8850959220e41b4c8c3"}, // life records interface query 0xC95E9AF7A1DD
 {0xca49e0,409,"45430ef1996656421fb2a73c233663d789e02b18398b2be6406e92d0f418eea8"}, // records allocation and initialisation (0x20 stride, life=max=10000)
 {0xca4b90,176,"23b4c5c619b17e10358f4950d3762a55f11246c41e3bec3208f9c1edd2c4232f"}, // per-frame life update over the records
 {0xca4c40,1300,"25d4f43a86feeed065176e51d5fab8cf3d109a66013462967536e992ae3e87b5"}, // record update incl. kill flag, Dead post 0x2600|index, jump tables
 {0xda55f0,3082,"28450c10b3eaefcfaaba325073a0d5602f37f0d394dd5b140bbc3a0d834fb914"}, // DamageControllerImpl update: ProcessDamages only when count != 0
};
inline constexpr std::uintptr_t kProcessSlot=0x231ad38, kProcess=0xc89510, kProcessVtable=0x231ad30;
inline constexpr std::uintptr_t kLifeSlot=0x231fd60, kLifeUpdate=0xca4b90, kLifeVtable=0x231fd60;
// The life record (0x20 bytes per dog instance; the copy holds one dog, instance 0).
inline constexpr std::size_t kRecordSize=0x20,kLife=0,kMax=2,kPending=4,kStun=6,kStunMax=8,kPendingStun=0xa,kState=0x1b,kFlags=0x1d;
inline constexpr std::uint8_t kFlagKill=4,kFlagRefill=8; // 4: life 0 at the next update (a forced death); 8: life = max
inline constexpr std::uint8_t kStateDown=2,kStateStunned=1;
struct Record {std::uint16_t life{},max{},pending{},stun{},stunMax{},pendingStun{};std::uint8_t state{},flags{};};
constexpr bool Same(const Record& a,const Record& b) noexcept {
 return a.life==b.life&&a.max==b.max&&a.pending==b.pending&&a.stun==b.stun&&a.stunMax==b.stunMax&&a.pendingStun==b.pendingStun&&
  a.state==b.state&&a.flags==b.flags;}
// The dog-state byte the host sends in COMPANION_POSE / COMPANION `state` (no wire change: both fields were sent 0):
// bit 7 valid (0: an older host, unreadable or no dog), bits 5-6 state (0 normal, 1 stunned or asleep, 2 down),
// bits 0-4 life in 31ths (0 only when down).
inline constexpr std::uint8_t kValid=0x80;
struct Follow {bool valid{};std::uint8_t state{},life31{};};
std::uint8_t Encode(const Record& host) noexcept;
Follow Decode(std::uint8_t byte) noexcept;
// Guest, armed (lease): record 0 before the stock update. Pending life and stun damage are dropped; a forced
// death the module did not issue is undone while the host's dog lives; with a valid host byte the copy takes the
// host's life (31ths), stun (asleep: 0; awake again: its maximum) and, once, the kill flag when the host's is down.
// A copy already down is left alone (the restore path, Lua). `next` is what the hook writes.
struct Plan {bool write{},kill{},clearedKill{},clearedPending{},life{},stun{};Record next{};};
Plan Guard(const Record& local,std::uint8_t follow,bool killIssued) noexcept;
// Lab lever (lab mode only): op 1 sets life to pct of the maximum (0: the kill flag), op 2 stun to pct.
Plan Lab(const Record& local,std::uint8_t op,std::uint8_t pct) noexcept;
// The arm lease counts the dog's own life updates, so a pause (no updates) never expires it while a stopped
// Lua tick does within kLeaseFrames updates.
inline constexpr std::uint64_t kLeaseFrames=180;
constexpr bool Armed(std::uint64_t frame,std::uint64_t until) noexcept {return until!=0&&frame<=until;}

#ifdef _WIN32
bool CodeGate(const void* image,std::size_t span) noexcept;
bool Install(void* image,std::size_t span,void (*log)(const char*) noexcept) noexcept;
bool Installed() noexcept;
void Shutdown() noexcept;
// The receipt owner (game_script_observer.cpp) passes the native role; only a guest may arm.
struct Request {bool guest{},arm{};std::uint8_t follow{},labOp{},labPct{};};
struct Status {bool installed{},armed{},snapshot{};Record rec{};std::uint32_t ageMs{},drops{},hits{},applied{},kills{},cleared{};
 std::uint16_t lastAttacker{0xffff},lastAttack{0xffff};std::uint64_t frames{};};
Status Control(const Request&) noexcept;
Status Read() noexcept;
// Mission end, unpair, loading: the guard and any queued lab op are dropped (the receipt re-arms).
void Disarm(const char* why) noexcept;
#endif
} // namespace mgscoop::dog_guard
