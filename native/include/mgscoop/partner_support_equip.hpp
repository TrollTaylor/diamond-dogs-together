#pragma once
// Anim round 2 support equipment for body 1: slot 3 type 12 (thrown) and, WS4
// v1, slot 3 type 13 C4 391..395 (lab key partner_native_placed) and slot 6
// type 14 arms 515..521 (lab key partner_native_arm; ChangeEquip 0x11AC2E0 ->
// ctl vt+0x1F0 0x11B1B60, the same qualified dispatch). The writer runs on the
// existing ObserveEquipmentTick owner; Follow runs in the qualified block-1 drain.
#include <cstddef>
#include <cstdint>
#include "mgscoop/partner_support_policy.hpp"
namespace mgscoop::partner_support_equip {
using LogFn=void (*)(const char*) noexcept;
struct Preconditions {
    bool enabled=false,owner=false,gates=false,body_ready=false,safety_ready=false;
    std::uint32_t player_flags=0,local=1,local_id=1,inventory_local=1;
    std::uint16_t body_id=0xFFFF,equip=0;
    std::uint8_t pool=0xFF,type=0,sub=0xFF,request_flags=4,restriction=1;
    // WS4 v1: the lab keys and the lane the remote holds (its live slot, 3 or 6).
    bool placed=false,arm=false;
    std::uint8_t slot=3;
};
constexpr const char* Refusal(const Preconditions& p) noexcept {
    namespace sp=partner_support_policy;
    const sp::Kind kind=sp::KindOf(p.type,p.equip,p.placed,p.arm);
    return !p.enabled ? "off" : !p.owner ? "thread" : !p.gates ? "admission" : !p.body_ready ? "body"
        : !p.safety_ready ? "safety" : !(p.player_flags&1) || (p.player_flags&0x100) ? "offline"
        : p.local==1 ? "p214" : p.local_id==1 ? "p218" : p.inventory_local==1 ? "inventory_local"
        : p.body_id!=1 ? "body_id" : p.pool==0xFF ? "pool" : p.restriction ? "restriction"
        : !p.equip || p.equip>=0x800 ? "equip_range" : kind==sp::Kind::none ? "type" : p.slot!=sp::KindSlot(kind) ? "slot_type"
        : p.sub>=8 ? "sub" : !(p.request_flags&4) || (p.request_flags&16) ? "request_flags" : nullptr;
}
// WS4 v1: the lane a frame's live slot names (3 always; 6 only with the arm key).
constexpr bool LaneSlot(std::uint8_t slot,bool arm) noexcept { return slot==3 || (slot==6 && arm); }
// The Match of one lane, pure: body 1's handle Y+0x40[1*7+slot] (kind = the
// equip's type, equip bits, index below the manager's capacity), its equipment
// row (slot 3: the sub's support entry and the support list row+0xC..; slot 6:
// the arm word row+8, ctl vt+0x98 case 6) and, active, the current slot and
// row+0 (SetCurrentSlot case 3 / case 6), then the handle's manager row (equip,
// owner 1, byte 4 & 0x7F == 0).
struct MatchIn {
    std::uint8_t slot=3,type=0;
    std::uint16_t equip=0;
    std::uint32_t handle=0,pool_capacity=0;
    std::uint8_t row_slot=0xFF,sub=0xFF;
    std::uint16_t current=0,arm=0;
    std::uint16_t supports[8]{};
    bool meta_ok=false;
    std::uint16_t meta_equip=0,meta_owner=0;
    std::uint8_t meta_flags=0;
};
constexpr bool EmptyHandle(std::uint32_t h) noexcept {return !h || (h>>16)==0xFFFFu || ((h>>5)&0x7FFu)==0;}
constexpr const char* MatchRefusal(const MatchIn& m,bool active) noexcept {
    if(EmptyHandle(m.handle)) return "handle_empty";
    if((m.handle&0x1F)!=m.type || ((m.handle>>5)&0x7FF)!=m.equip || (m.handle>>16)>=m.pool_capacity) return "handle_equip";
    if(m.slot==6) {
        if(m.arm!=m.equip || (active && (m.row_slot!=6 || m.current!=m.equip))) return "row_equip";
    } else {
        if(m.slot!=3 || m.sub>=8 || (active && (m.row_slot!=3 || m.current!=m.equip || m.supports[m.sub]!=m.equip))) return "row_equip";
        bool found=false;for(const auto e:m.supports) if(e==m.equip) found=true;
        if(!found) return "support_list";
    }
    if(!m.meta_ok || m.meta_equip!=m.equip || m.meta_owner!=1 || (m.meta_flags&0x7F)) return "handle_owner";
    return nullptr;
}
// Already current for the drain's follower: the lane's slot is active with this equip.
constexpr bool AlreadyCurrent(const MatchIn& m) noexcept {
    return m.slot==6 ? m.row_slot==6 && m.current==m.equip && m.arm==m.equip
                     : m.row_slot==3 && m.sub<8 && m.current==m.equip && m.supports[m.sub]==m.equip;
}
struct Context {
    bool enabled=false,owner=false,gates=false,body_ready=false,safety_ready=false;
    // Body 1's gun mode is unread/switching/reloading/throw/place/arm-use family
    // (partner_support_policy::BodyBusy): never (re)equip slot 3/6 then.
    bool body_busy=false;
    // WS4 v1 lab keys (already false while their mission latches are set).
    bool placed=false,arm=false;
    std::uintptr_t base=0,player=0,remote_block=0;
    std::size_t image_span=0;
    std::uint32_t mission=0,realize_gen=0;
    std::uint64_t now_ms=0;
    std::uint16_t equip=0;
    std::uint8_t slot=0xFF;
    bool (*pins_intact)(std::uintptr_t,std::size_t,std::uintptr_t&) noexcept=nullptr;
};
void Observe(const Context& context,LogFn log) noexcept;
// Fresh, read-only current slot/row/handle match, and the last owner admission
// still names this exact body. No native call and no expensive hash per frame.
// decoded_type (optional) receives the packed-descriptor type of equip;
// handle (optional) receives the Match-verified lane handle Y+0x40[1*7+slot]
// when ready (else 0): the drain requires rec+0x240 to equal it (WS1 B).
// slot: the lane (3, or 6 for an arm).
bool Ready(std::uintptr_t base,std::uintptr_t player,std::uintptr_t block,std::uint16_t equip,
           std::uint8_t* decoded_type=nullptr,std::uint32_t* handle=nullptr,std::uint8_t slot=3) noexcept;
// Mission-scoped: a dispatch or follow whose outcome was uncertain (it may have
// touched the viewer's state) keeps the writer off until the mission changes.
bool Uncertain() noexcept;
// Matching live lane handle required. Calls pinned SetCurrentSlot for id 1
// only if not current already; never the old data-only gun follower.
const char* Follow(std::uintptr_t base,std::size_t span,std::uintptr_t player,std::uintptr_t block,
                   std::uint16_t equip,LogFn log,std::uint8_t slot=3) noexcept;
}
