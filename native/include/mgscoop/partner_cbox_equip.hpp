#pragma once
// F2: the remote's cardboard box on body 1 (instance 1), native (docs/native-puppet.md
// "Cardboard box"). Modelled on partner_support_equip: it runs on the natural
// mission owner (ObserveEquipmentTick, 10 Hz, right after the support lane),
// re-reads every native precondition, and makes at most one of two direct calls
// per tick, each SEH-wrapped and bracketed by viewer / body-1 digests:
//   on:  ChangeEquip 0x11AC2E0 (ctl, &result, R8D = 1, {equip = B, flags = 4})
//   off: SetCurrentItemIndex 0x11AF9A0 (ctl, EDX = 1, R8D = 0, R9B = 0), only
//        for a box this module put on.
// B is a box the VIEWER owns with stock (partner_cbox_policy::MapViewerBox): the
// engine's box wake checks the viewer's item list, not body 1's. Lab key
// partner_native_cbox (default 0; play forces it off).
#include <cstddef>
#include <cstdint>
#include "mgscoop/partner_cbox_policy.hpp"
namespace mgscoop::partner_cbox_equip {
using LogFn = void (*)(const char*) noexcept;
struct Context {
    bool enabled = false;       // partner_native_cbox in lab mode
    bool owner = false;         // the natural mission owner thread
    bool gates = false;         // the support lane's admission gates (copied)
    const char* gate_reason = nullptr;  // the first failing gate (static; nullptr when gates)
    bool body_ready = false;    // the two-instance body lease (pp::ReadState + ResolveDm)
    const char* busy_reason = nullptr;  // partner_cbox_policy::BusyReason (gun / support writers)
    // The driver's edge (NoteCbox): the remote is boxed with box id want_id; seq
    // counts every logged change (Mission::cbox_seq).
    bool want = false;
    std::uint16_t want_id = 0;
    std::uint32_t seq = 0;
    std::uintptr_t base = 0, player = 0, remote_block = 0;
    std::size_t image_span = 0;
    std::uint32_t mission = 0, realize_gen = 0;
    // The driver's mission start (Mission::start_ms, set at CFG ready): with the
    // mission code it scopes the latches and the edge state (cbox_seq restarts
    // with every mission, also one with the same code).
    std::uint64_t epoch = 0;
    std::uint64_t now_ms = 0;
    // equipment_stock_writer::PinsIntact (every SHA-pinned body, including
    // ChangeEquip, SetCurrentItemIndex and its callees).
    bool (*pins_intact)(std::uintptr_t, std::size_t, std::uintptr_t&) noexcept = nullptr;
    // partner_player::EquipType (the packed descriptor's type; a box must be 17).
    bool (*equip_type)(std::uint16_t, std::uint8_t&) noexcept = nullptr;
};
void Observe(const Context& context, LogFn log) noexcept;
// Mission-scoped: a call whose bracket showed a surprise keeps the box off
// (no further call, not even an off) until the mission changes.
bool Uncertain() noexcept;
// Read-only status for telemetry and tests.
struct Status {
    std::uint32_t mission = 0, dispatches_on = 0, dispatches_off = 0;
    std::uint16_t applied = 0;
    bool entered = false, uncertain = false, watch_latched = false;
};
Status GetStatus() noexcept;
}  // namespace mgscoop::partner_cbox_equip
