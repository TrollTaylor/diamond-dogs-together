#pragma once
#include <cstddef>
#include <cstdint>

// Debug "Spawn enemy here" (docs/debug-menu.md): sends the engine's own
// set-transform message 0x287158b07327 to one soldier GameObjectId through the
// GameObject manager's SendCommand, as the player carry code does at
// 0x1055b66. Contract: native/soldier_warp_contract.hpp
// (tools/generate_soldier_warp_contract.py). Default off (lab.ini
// debug_soldier_warp=1); the caller owns the thread, mission, role and
// command gates and calls this only on the Lua owner thread.
namespace mgscoop::soldier_warp {
inline constexpr float kMaxRangeM = 20.0f;  // target to local player (default Request::max_range)
// Upper bound of Request::max_range: a guest spawn-sync correction (world_place
// kind 3) may move a guard up to 60 m from the guest's view of the host.
inline constexpr float kMaxRangeLimitM = 60.0f;

// Exact bytes of every contract site and data qword in the running image.
// nullptr when all match, else a reason; `mismatch` gets the first RVA.
const char* Verify(std::uintptr_t base, std::size_t span, std::uintptr_t& mismatch) noexcept;

struct Request {
    std::uint32_t go_id{};
    float pos[3]{};     // ground position, metres (the handler adds the body offset T+0x160 to y)
    float yaw{};        // radians; quaternion {0, sin(yaw/2), 0, cos(yaw/2)}
    float player[3]{};  // reference position, metres (host: the local player; guest: its view of the host)
    float max_range{kMaxRangeM};  // metres, (0, kMaxRangeLimitM]; anything else refuses `input`
};

// WARP_V1 receipt text from lua/coop/debug_spawn.lua:
// "<command id> <GameObjectId> <x_mm> <y_mm> <z_mm> <yaw_mrad>", single spaces, decimal
// integers only. Fills `command` and request.go_id/pos/yaw (not player); false on any
// other text or out-of-range field (id 1..2^32-1, GameObjectId <= 0xffff, |mm| <= 1e8,
// |yaw| <= 8000 mrad).
bool ParseReceipt(const char* text, std::size_t size, std::uint32_t& command, Request& request) noexcept;

// Read-only census of the dispatch chain (RVAs are 0 when outside the image).
struct Chain {
    std::uintptr_t type_object{}, type_vt{}, count_fn{}, handle_object{}, handle_vt{}, hop{}, leaf{}, leaf_vt{};
    unsigned char hop_bytes[24]{};
    bool hop_read{}, pinned{};              // pinned: the hop provably reaches 0x1360f90 on `leaf`
    bool leaf_state{};                      // the three fields below were read (pinned only)
    std::uint32_t count{}, realized{}, stance{};
};

struct Result {
    // nullptr: the message was sent and the engine gave one of its two set-transform
    // answers: out=0 handled=1 (moved) or out=-1 handled=0 (not realized / no handle).
    // Refusals before sending: contract type input range manager registry chain
    // leaf (a recognized forwarder hop that does not reach the verified handler)
    // unrealized (r=-1) index stance. After sending: fault (SEH) and anomaly (any
    // other out/handled pair, so the unpinned dispatch did not behave as the handler).
    const char* refusal{};
    std::int32_t out{-2};
    std::uint8_t handled{};
    bool sent{};
    Chain chain{};
};

// Caller has already verified the contract (Verify) for this image.
Result Warp(std::uintptr_t base, std::size_t span, const Request& request) noexcept;

// Per-command bound, owned by the single caller thread. One "spawn_enemy here"
// command (or one guest spawn-sync grant, id 0x80000000|epoch<<16|seq) moves one
// guard: a fixed GameObjectId, at most kMaxAttempts calls (the menu retries 151
// times at most) and none after the engine handled it.
// A fault or anomaly latches the feature off for the rest of the process.
inline constexpr unsigned kMaxAttempts = 160;
struct Gate {
    std::uint32_t command{}, go_id{};
    unsigned attempts{};
    bool landed{};
    const char* latched{};
    // nullptr admits one Warp call; else latched, target, landed or budget.
    const char* Admit(std::uint32_t command_id, std::uint32_t go) noexcept;
    // The latch reason when this result newly latches the feature off, else nullptr.
    const char* Record(const Result& result) noexcept;
};

// One "soldier_warp_census; ..." line ("\r\n" terminated); 0 on failure.
std::size_t FormatCensus(std::uintptr_t base, std::size_t span, std::uint32_t go_id, const Result& result, char* out,
                         std::size_t capacity) noexcept;
} // namespace mgscoop::soldier_warp
