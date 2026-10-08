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
inline constexpr float kMaxRangeM = 20.0f;  // target to local player

// Exact bytes of every contract site and data qword in the running image.
// nullptr when all match, else a reason; `mismatch` gets the first RVA.
const char* Verify(std::uintptr_t base, std::size_t span, std::uintptr_t& mismatch) noexcept;

struct Request {
    std::uint32_t go_id{};
    float pos[3]{};     // ground position, metres (the handler adds the body offset T+0x160 to y)
    float yaw{};        // radians; quaternion {0, sin(yaw/2), 0, cos(yaw/2)}
    float player[3]{};  // local player position, metres
};

// Read-only census of the dispatch chain (RVAs are 0 when outside the image).
struct Chain {
    std::uintptr_t type_object{}, type_vt{}, count_fn{}, handle_object{}, handle_vt{}, hop{}, leaf{}, leaf_vt{};
    unsigned char hop_bytes[24]{};
    bool hop_read{}, pinned{};              // pinned: the hop provably reaches 0x1360f90 on `leaf`
    bool leaf_state{};                      // the three fields below were read (pinned only)
    std::uint32_t count{}, realized{}, stance{};
};

struct Result {
    // nullptr: the message was sent and `out`/`handled` are the engine's answer.
    // Refusals: contract type input range manager registry chain unrealized(r=-1, nothing sent)
    // index stance fault.
    const char* refusal{};
    std::int32_t out{-2};
    std::uint8_t handled{};
    Chain chain{};
};

// Caller has already verified the contract (Verify) for this image.
Result Warp(std::uintptr_t base, std::size_t span, const Request& request) noexcept;

// One "soldier_warp_census; ..." line ("\r\n" terminated); 0 on failure.
std::size_t FormatCensus(std::uintptr_t base, std::size_t span, std::uint32_t go_id, const Result& result, char* out,
                         std::size_t capacity) noexcept;
} // namespace mgscoop::soldier_warp
