#pragma once
#include <cstddef>
#include <cstdint>
#include "mgscoop/partner_player.hpp"
namespace mgscoop::sleep_info {
struct BodyReply {
 std::uint32_t nodeState=0,nodeKind=0,recordFlags=0,subtype=0,lifeState=0;
 std::uint16_t flags=0,qAction=0;
 std::uint8_t mode=0,requested=0,counter=0,action=0;
 bool nodeActive=false,callbackInitialized=false;
};
struct Reply {
 bool ready=false;
 std::uint32_t generation=0;
 BodyReply bodies[2];
 const char* reason="disabled";
};
// Only after the coordinator's full retail executable/PE/partner gate.
// Exact bounded full-code/constant/slot reads, never a native call.
bool CodeGate(const void* image,std::size_t span,std::uintptr_t& mismatch) noexcept;
void Configure(const partner_player::Addresses& source,bool verified,
 void (*log)(const char*) noexcept=nullptr) noexcept;
// The natural coordinator supplies ownerThread and admission, never Lua.
// All reads need the current fresh downed scope, paired offline realized
// bodies, stable input window and three matching controller observations.
// Mode/action values do not prove a loaded/playing animation or permit sleep,
// recovery, attachment, input unlock, carry or any other game-state change.
bool Observe(std::uint32_t mission,std::uint16_t epoch,bool admitted,
 std::uint32_t naturalOwnerThread,Reply& out) noexcept;
void Shutdown() noexcept;
}
