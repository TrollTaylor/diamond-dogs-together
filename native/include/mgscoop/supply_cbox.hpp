#pragma once
// Supply drops synced (r1101, docs/world-sync-protocol.md section 25): the supply-box observer's Win32 layer
// (native/supply_cbox.cpp). With lab.ini world_supply=1 only, after every site of native/supply_cbox_contract.hpp
// matches the running image: two observe-only MinHook detours (the box request core 0x15C3F00 and the box reset
// 0x15C4080; each records an event and calls the original with its own arguments), a read of the box core's state and
// flags, and the lab remove lever (flags |= 2: the game's own reset bit, the next update resets the box). The pure parts
// are mgscoop/supply_sync.hpp.
#include <cstddef>
#include <cstdint>

namespace mgscoop::supply_cbox {

// Idempotent; false (and one log line) when a site or slot does not match or a hook cannot be created.
bool Install(void* base, std::size_t span, void (*log)(const char*) noexcept) noexcept;
bool Installed() noexcept;
// The key (receipts answer {on=0} while off).
void SetEnabled(bool on) noexcept;
bool Enabled() noexcept;
// MGS_COOP_SUPPLY_V1: the text after the prefix (supply_sync::ParseCommand); the reply ("return {...}") into out.
// Returns its length, or 0 for a command that does not parse.
std::size_t HandleReceipt(const char* text, std::size_t size, std::uint64_t now_ms, char* out, std::size_t capacity) noexcept;

} // namespace mgscoop::supply_cbox
