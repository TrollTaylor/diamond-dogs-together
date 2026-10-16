#pragma once
// Anim round 2, type-12 slot-3 equipment only. The writer runs on the existing
// ObserveEquipmentTick owner; Follow runs in the qualified block-1 drain.
#include <cstddef>
#include <cstdint>
namespace mgscoop::partner_support_equip {
using LogFn=void (*)(const char*) noexcept;
struct Preconditions {
    bool enabled=false,owner=false,gates=false,body_ready=false,safety_ready=false;
    std::uint32_t player_flags=0,local=1,local_id=1,inventory_local=1;
    std::uint16_t body_id=0xFFFF,equip=0;
    std::uint8_t pool=0xFF,type=0,sub=0xFF,request_flags=4,restriction=1;
};
constexpr const char* Refusal(const Preconditions& p) noexcept {
    return !p.enabled ? "off" : !p.owner ? "thread" : !p.gates ? "admission" : !p.body_ready ? "body"
        : !p.safety_ready ? "safety" : !(p.player_flags&1) || (p.player_flags&0x100) ? "offline"
        : p.local==1 ? "p214" : p.local_id==1 ? "p218" : p.inventory_local==1 ? "inventory_local"
        : p.body_id!=1 ? "body_id" : p.pool==0xFF ? "pool" : p.restriction ? "restriction"
        : !p.equip || p.equip>=0x800 ? "equip_range" : p.type!=12 ? "type" : p.sub>=8 ? "sub"
        : !(p.request_flags&4) || (p.request_flags&16) ? "request_flags" : nullptr;
}
struct Context {
    bool enabled=false,owner=false,gates=false,body_ready=false,safety_ready=false;
    // Body 1's gun mode is unread/switching/reloading/throw family
    // (partner_support_policy::BodyBusy): never (re)equip slot 3 then.
    bool body_busy=false;
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
// handle (optional) receives the Match-verified slot-3 handle Y+0x40[1*7+3]
// when ready (else 0): the drain requires rec+0x240 to equal it (WS1 B).
bool Ready(std::uintptr_t base,std::uintptr_t player,std::uintptr_t block,std::uint16_t equip,
           std::uint8_t* decoded_type=nullptr,std::uint32_t* handle=nullptr) noexcept;
// Mission-scoped: a dispatch or follow whose outcome was uncertain (it may have
// touched the viewer's state) keeps the writer off until the mission changes.
bool Uncertain() noexcept;
// Matching live slot-3 handle required. Calls pinned SetCurrentSlot for id 1
// only if not current already; never the old data-only gun follower.
const char* Follow(std::uintptr_t base,std::size_t span,std::uintptr_t player,std::uintptr_t block,
                   std::uint16_t equip,LogFn log) noexcept;
}
