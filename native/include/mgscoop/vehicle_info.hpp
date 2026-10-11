#pragma once
#include <cstddef>
#include <cstdint>
#include "mgscoop/partner_player.hpp"
namespace mgscoop::vehicle_info {
struct BodyReply {
 std::uint16_t actualId=0xffff,vehicle=0xffff,candidate=0xffff;
 std::uint8_t phase=0,nextPhase=0,packedEntryOrSeat=0,entryOrSeat=0;
 bool nodeActive=false;
};
struct Reply {
 bool ready=false;
 std::uint32_t generation=0;
 BodyReply bodies[2];
 const char* reason="disabled"; // module-owned constant; no observed pointer
};
// Coordinator first verifies the complete exact retail executable/PE and
// partner contracts, then this complete bounded code/slot gate. Pure reads.
bool CodeGate(const void* image,std::size_t span,std::uintptr_t& mismatch) noexcept;
void Configure(const partner_player::Addresses& source,bool verified,
 void (*log)(const char*) noexcept=nullptr) noexcept;
// Natural coordinator supplies thread/admission/scene; no Lua identity or
// address is accepted. Three controller snapshots, fresh owner cycle, typed
// realized paired offline bodies and an unchanged even input window are needed.
// Fields are raw ordinary-road-vehicle controller observations only. The low
// record57 nibble represents an entry side in phase1 (0/1), and is0 in settled
// driver phase3. No field proves seat occupancy, body attachment, a valid
// camera/input route or replicated passengers, and none permits any write/call.
bool Observe(std::uint32_t mission,std::uint16_t epoch,bool admitted,
 std::uint32_t naturalOwnerThread,Reply& out) noexcept;
void Shutdown() noexcept;
}
