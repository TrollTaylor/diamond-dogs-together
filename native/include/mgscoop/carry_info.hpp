#pragma once
#include <cstddef>
#include <cstdint>
#include "mgscoop/partner_player.hpp"
namespace mgscoop::carry_info {
struct BodyReply {
    bool eligible=false,rejected=false,attachmentPresent=false,nativeCarryBit=false,targetsPartner=false;
    float origin[4]{},radius=0,height=0;
    std::uint16_t carryTarget=0xffff;
    std::uint32_t carryFlags=0,gunFlags=0;
};
struct Reply {
    bool ready=false;
    std::uint32_t generation=0;
    BodyReply bodies[2];
    const char* reason="disabled";
};
// Requires the coordinator's exact PE image gate and existing player contract.
// Adds its own complete carry-query code/slot gate. No gameplay writes.
bool CodeGate(const void* base,std::size_t span,std::uintptr_t& mismatch) noexcept;
void Configure(const partner_player::Addresses& addresses,bool verified) noexcept;
// Only the coordinator's natural script-owner route may supply ownerThread.
// Never expose that value, native pointers, or arbitrary query hashes to Lua.
// Both bodies must be realized offline and both observations coherent; native
// downed scope must be fresh. A successful query NEVER starts/attaches carry.
bool Observe(std::uint32_t mission,std::uint16_t epoch,bool admitted,
    std::uint32_t naturalOwnerThread,Reply& out) noexcept;
void Shutdown() noexcept;
}
