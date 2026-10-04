#pragma once
#include <cstddef>
#include <cstdint>

namespace mgscoop::player_type {
// Scoped bypass of the native "OCELOT/QUIET outside FOB -> SNAKE" reset in
// UpdatePartsStatus: the jne at RVA 0x9cd370 becomes an unconditional jmp to
// the same keep path (two bytes, one aligned interlocked exchange). Enabling
// and disabling both require the complete function body hash with the branch
// in the state this module expects; disabling restores the original bytes.
// Idempotent and thread safe. Refuses and logs one reason on any mismatch,
// including bytes it does not own. No disk writes, detours or callbacks are
// retained; the caller's last-error value is preserved.
bool SetActive(void* base, std::size_t span, bool enabled, void (*log)(const char*) noexcept) noexcept;
// True while this module owns the replacement bytes in the game image.
bool IsActive() noexcept;

namespace detail {
// Test seam: the identical state machine against an explicit contract and
// state object, so the gate can be exercised on synthetic code.
struct Contract {
    std::uintptr_t body_rva;
    std::size_t body_size;
    std::size_t site_offset;
    unsigned char original[6];
    unsigned char replacement[6];
    unsigned char sha256[32];  // body with `original` at site_offset
};
struct State {
    void* owner;  // base whose bytes this state changed at least once
    bool active;  // replacement bytes currently owned and in place
};
inline constexpr std::size_t max_body_size = 8192;
bool SetActiveWith(State& state, const Contract& contract, void* base, std::size_t span, bool enabled,
                   void (*log)(const char*) noexcept) noexcept;
} // namespace detail
} // namespace mgscoop::player_type
