#pragma once
// Supply drops synced (docs/world-sync-protocol.md section 26; lua/coop/world_supply.lua): the portable core of the
// supply-box observer. The game keeps ONE supply box per copy (the TppSupplyCboxSystem core, Z): the observer's two
// observe-only detours (native/supply_cbox.cpp, contract native/supply_cbox_contract.hpp) record each box request
// (0x15C3F00: drop type, equip, parts, aim position, level, flags) and each box reset (0x15C4080) in a small ring that
// Lua reads with MGS_COOP_SUPPLY_V1. Pure: no Win32, no game memory; every time is passed in.
//
// Receipts (Lua -> native, lab key world_supply; OFF unless named, lab and play):
//   return 'MGS_COOP_SUPPLY_V1' -- poll <after>     the box state and the events with seq > after
//        -> return {on=1,inst=1,box=1,st=<state>,fl=<flags>,last=<seq>,lost=<n>,ev={{seq,kind,mirror,drop,equip,parts,
//                   x,y,z,level,wait,flags,aim,caller,state,accepted},...}}  (x y z mm; kind 1 request, 2 reset; mirror 1
//                   while Lua's own mirror request ran; accepted 0 when the box core refused the request) | return {on=0}
//   return 'MGS_COOP_SUPPLY_V1' -- mirror <0|1>     Lua's mirror request begins (1) / ends (0) on this thread
//        -> return {ok=1}
//   return 'MGS_COOP_SUPPLY_V1' -- remove <token>   the lab remove lever: the box core's flags |= 2 (the game's own
//        reset bit; the update resets the box) -> return {ok=1,st=<state>} | return {ok=0,why='<reason>'}
#include <cstddef>
#include <cstdint>

namespace mgscoop::supply_sync {

inline constexpr std::uint8_t kOp = 0x45;   // SUPPLY (world_wire op::supply)
inline constexpr std::uint8_t kNote = 21;   // world note SUPPLY
inline constexpr unsigned kRing = 16;
inline constexpr std::size_t kRequestSize = 0x130;
inline constexpr std::uint8_t kRequest = 1, kReset = 2;
inline constexpr std::int32_t kPosLimitMm = 1000000000;  // the wire's I32 range

struct Event {
    std::uint32_t seq = 0;
    std::uint8_t kind = 0, mirror = 0, level = 0, wait_level = 0, flags = 0, accepted = 1;
    std::uint16_t aim_object = 0xFFFF;
    std::uint32_t drop_type = 0, equip_id = 0, parts_type = 0;
    std::int32_t pos_mm[3]{};
    std::uint32_t caller_rva = 0, state = 0;
    std::uint64_t t_ms = 0;
};

// The 0x130-byte request (Player.RequestToSupplyCbox layout, the contract's lua_request_tail): +0 drop type, +4 equip,
// +8 parts, +0x100 aim position (3 floats, metres), +0x110 aim object, +0x112 wait level, +0x120 level, +0x12d flags. False (out untouched)
// for a short buffer or a position that is not finite or beyond +-1e6 m.
bool DecodeRequest(const unsigned char* request, std::size_t size, Event& out) noexcept;

// The box core around a request (review r1101): the request detour calls the original first. The core's refused branch
// ([Z+0x20]->vt[0]() true: jnz to the epilogue) touches nothing in Z; an accepted request copies itself to the stored
// request (Z+0x4E0) and writes the state (Z+0xF0) and the flags (Z+0x124 |= 2 and 8 or 0x10). Accepted = the state, the
// flags or the stored request changed, or a read failed (unknown: as before). A request that changes nothing (the same
// bytes again before the update ran) is no new box either.
struct CoreView {
    bool ok = false;
    std::uint32_t state = 0, flags = 0;
    unsigned char stored[kRequestSize]{};
};
bool RequestAccepted(const CoreView& before, const CoreView& after) noexcept;

// Events in order; seq 1.. (0 never used: Lua's "nothing seen").
class Ring {
public:
    std::uint32_t Push(const Event& e) noexcept;
    // Events with seq > after, oldest first, at most max; lost = how many of them fell out of the ring.
    std::size_t Since(std::uint32_t after, Event* out, std::size_t max, std::uint32_t& lost) const noexcept;
    std::uint32_t Last() const noexcept { return last_; }
    void Clear() noexcept;
private:
    Event events_[kRing]{};
    std::uint32_t last_ = 0;
    std::size_t count_ = 0;
};

enum class Command : std::uint8_t { none, poll, mirror, remove };
struct Parsed {
    Command cmd = Command::none;
    std::uint32_t arg = 0;
};
// The text after the receipt prefix: "poll <u32>", "mirror 0|1", "remove <u32>" (digits only, no leading zeros, single
// spaces, nothing trailing).
bool ParseCommand(const char* text, std::size_t size, Parsed& out) noexcept;

struct Snapshot {
    bool on = false, installed = false, box_known = false;
    std::uint32_t state = 0, flags = 0;
};
// NUL-terminated poll reply; 0 when it does not fit.
std::size_t FormatPoll(const Snapshot& s, const Event* events, std::size_t count, std::uint32_t last, std::uint32_t lost,
                       char* out, std::size_t capacity) noexcept;
// "supply=<what>; seq=..; kind=..; ..." for the native log (one line, CRLF).
std::size_t FormatLogLine(const Event& e, char* out, std::size_t capacity) noexcept;

} // namespace mgscoop::supply_sync
