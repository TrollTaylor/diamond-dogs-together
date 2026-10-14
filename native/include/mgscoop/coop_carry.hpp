#pragma once
// Co-op teammate carrying through the game's own player-carries-player code.
//
// The pinned executable (mgsvtpp.exe 1.0.15.4, SHA-256 085c2f82...bb45)
// contains the complete online (FOB) carry path for one player body carrying
// another: the carry candidate producer 0x112C680 scans player bodies only
// when TppGameStatusFlag S_IS_ONLINE (20) is set, and the carried-recipient
// handshake receiver 0x11268B0 creates the recipient's kind-15 "being carried"
// request only in its online branch. Offline, both stop short, so a teammate
// could never be picked up. This adapter answers the S_IS_ONLINE query true for
// exactly the candidate producer's call (return address 0x112C8BA) and, after
// an accepted offline op0 handshake whose target is a co-op player body,
// submits the same kind-15 request the online branch would. Everything after
// that (attachment, carrying locomotion, put-down, cleanup) is stock code.
// See docs/STATUS.md.
#include <cstddef>
#include <cstdint>
#include "mgscoop/camera_mode_setter.hpp"

namespace mgscoop::coop_carry {

using Log = void (*)(const char*) noexcept;

// Modes: 0 off; 1 teammates in the native sleep/stun states (Q action 0x83/0x84);
// 2 also downed teammates in any other state (the stock 0x83/0x84 op0 guard is
// skipped for co-op targets only).
enum : unsigned { kOff = 0, kSleeping = 1, kAnyDowned = 2 };

// Verifies the exact code/vtable bytes and patches the two vtable slots once.
bool Install(const void* image, std::size_t span, Log log) noexcept;
bool Installed() noexcept;
// Pure captured owned-patch overlay, including partial installations.
// Does not invoke any earlier code-view reader, native method or mutation.
bool OverlayOriginal(std::uintptr_t at,void* output,std::size_t size) noexcept;
// Current typed presentation-slot ownership only, no call or substitution.
// Proves this module's actual slot receipt and untouched native setter ABI.
bool ReadPresentationSetter(std::uintptr_t image,std::uintptr_t slot,std::uintptr_t actual,
                            camera_mode_setter::Receipt&) noexcept;
// Co-op link (reliable channel 2): the carrier's machine announces pickup and
// put-down; the other machine mirrors them on its replica of the carrier.
void SetSender(bool (*send)(const std::uint8_t*, std::size_t) noexcept) noexcept;
bool IsCarryMessage(const std::uint8_t* bytes, std::size_t size) noexcept;
bool Receive(const std::uint8_t* bytes, std::size_t size) noexcept;
// One-line mirror/wire state for diagnostics.
bool MirrorLine(char* out, std::size_t capacity) noexcept;
// Per-frame (runtime thread) monitor; logs carry state changes.
void Tick() noexcept;
void SetMode(unsigned mode) noexcept;
unsigned Mode() noexcept;
// Per-body "carryable" grant from the downed/sleep owner (bit i = body id i).
// Mode 2 requires the grant before the stock action guard is skipped.
void SetCarryable(unsigned mask) noexcept;
unsigned Carryable() noexcept;
// Authenticated current scene/generation binds native readiness on BOTH copies.
void SetReadinessFrame(std::uint32_t mission,std::uint16_t epoch,std::uint32_t localGeneration,
 std::uint32_t peerGeneration,bool localReady,bool admitted) noexcept;
bool PeerCarryReady() noexcept;


struct BodyCarry {
    bool read = false;
    std::uint16_t action = 0;      // Q+0x18[id]
    std::uint32_t psFlags0 = 0;    // PS record dword 0 (bit 22: carrying)
    std::uint32_t psFlags1 = 0;    // PS record dword 1 (bit 14: put-down request)
    std::uint8_t requestKind = 0;  // Q+0x1d8 request slot kind byte
    std::uint8_t requestSeq = 0, requestForce = 0;
    std::uint16_t requestCarrier = 0xffff;
};
struct Status {
    bool installed = false;
    unsigned mode = 0, carryable = 0;
    std::uint32_t overrides = 0, op0Calls = 0, op0Ours = 0, op0Accepted = 0, guardSkips = 0, kind15 = 0, kind15Refused = 0;
    std::uint16_t lastCarrier = 0xffff, lastTarget = 0xffff, lastTargetAction = 0;
    std::int32_t lastResult = 0;
    // The last op0 carrier record (fields read now).
    bool carrierRead = false;
    std::uint16_t carrierTarget = 0xffff, carrierTarget2 = 0xffff;
    std::uint8_t progress = 0, ack = 0, owned = 0, variant = 0;
    std::uint16_t carrierFlags = 0;
    BodyCarry body[2]{};
};
Status GetStatus() noexcept;
bool StatusLine(char* out, std::size_t capacity) noexcept;
bool BodyLine(char* out, std::size_t capacity) noexcept;
bool TraceLine(char* out, std::size_t capacity) noexcept;
bool SelectorLine(char* out, std::size_t capacity) noexcept;
// Carrier 0 load-onto-horse/vehicle fields (+0x538 target vehicle, +0x6AC prompt).
bool LoadLine(char* out, std::size_t capacity) noexcept;
bool HorseLoadLine(char* out, std::size_t capacity) noexcept;
bool HorseGateLine(char* out, std::size_t capacity) noexcept;

// Instance 1 (the teammate's body on this machine) is carried by the local
// player or being picked up/put down: its transform belongs to the native
// attachment. Cheap reads; false when unknown or the adapter is off.
bool PartnerCarryHold() noexcept;
// Horse cargo: Lua places instance 1 on the local D-Horse; the partner driver
// stays off it (PartnerCarryHold() is true while set).
void SetCargoHold(bool on) noexcept;
// The local player rides as cargo on the teammate's horse (HUD only).
void SetCargoLocal(bool on) noexcept;
// CarryRole, else 3 (teammate is cargo on the local horse) or 4 (local cargo).
unsigned HudRole() noexcept;
// 0 none, 1 the local player carries the teammate, 2 the local player is carried.
unsigned CarryRole() noexcept;
// The local player is down but awake (wounded): keep the normal view while carried.
void SetLocalAwakeDown(bool awake) noexcept;
// The stock put-down request for the local carrier (RequestCarryOff's write).
bool RequestLocalPutDown() noexcept;
// Current native flags/actions, paired twice; unknown is never detached.
struct AttachmentState { bool ready=false; bool carrying[2]{},carried[2]{}; };
AttachmentState ReadAttachmentState() noexcept;
// Accepted wake first asks the matching native carrier to put down this body.
// The caller waits for actual detachment before releasing sleep/controls.
bool RequestPutDownForBody(unsigned victim) noexcept;
// The stock carry point (0x112E7D0, the position the pickup predicate measures)
// of body id. Test helper; false when unavailable.
bool CarryPoint(unsigned id, float (&out)[3]) noexcept;

// True while the given body id is in the stock carried state (action 0x2A..0x2C
// family) or is a stock carrier (PS bit 22). Cheap reads; false when unknown.
bool BodyCarried(unsigned id) noexcept;
bool BodyCarrying(unsigned id) noexcept;

} // namespace mgscoop::coop_carry
