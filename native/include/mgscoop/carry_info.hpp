#pragma once
#include <cstddef>
#include <cstdint>
#include "mgscoop/partner_player.hpp"
namespace mgscoop::carry_info {
struct BodyReply {
    bool eligible=false,rejected=false,attachmentPresent=false,nativeCarryBit=false,gunTargetsPartner=false;
    float origin[4]{},radius=0,height=0;
    std::uint16_t gunTarget=0xffff;
    std::uint32_t carryFlags=0,gunFlags=0;
    std::uint16_t carryTarget=0xffff,carryPrevious=0xffff,carrier=0xffff,pluginFlags[4]{};
    std::uint8_t carryState[5]{};
    bool carryNodeActive=false;
};
struct Reply {
    bool ready=false;
    std::uint32_t generation=0;
    BodyReply bodies[2];
    bool pluginReady=false,selectorOwnerMatches=false,selectorAccepts=false,playerCarryEnabled=false;
    std::uint16_t selectorTarget=0xffff;
    std::uint32_t cachedKind=0,currentKind=0,cachedFlags=0,currentFlags=0;
    const char* pluginReason="disabled";
    const char* reason="disabled";
};
// Requires the coordinator's exact PE image gate and existing player contract.
// Adds its own complete carry-query code/slot gate. No gameplay writes.
bool CodeGate(const void* base,std::size_t span,std::uintptr_t& mismatch) noexcept;
void Configure(const partner_player::Addresses& addresses,bool verified,
    void (*log)(const char*) noexcept=nullptr) noexcept;
// Only the coordinator's natural script-owner route may supply ownerThread.
// Never expose that value, native pointers, or arbitrary query hashes to Lua.
// Both bodies must be realized offline and both observations coherent; native
// downed scope must be fresh. A successful query NEVER starts/attaches carry.
bool Observe(std::uint32_t mission,std::uint16_t epoch,bool admitted,
    std::uint32_t naturalOwnerThread,Reply& out) noexcept;
void Shutdown() noexcept;
}
