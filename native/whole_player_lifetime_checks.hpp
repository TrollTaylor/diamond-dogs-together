#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include "whole_player_lifetime_contract.hpp"
#include "native_pose_birth.hpp"
namespace mgscoop::whole_player_lifetime_checks {
// Called only with the actual birth publication lock held. Every lifecycle
// entry invalidates in-flight constructors, including an empty receipt.
inline void InvalidateWhole(native_pose_birth::Receipt& receipt,std::uint64_t& epoch,std::uintptr_t go)noexcept{
 ++epoch;if(!epoch)++epoch;
 if(receipt.ready&&receipt.identity.player>=0x20&&receipt.identity.player-0x20==go)receipt={};
}
template<class R>bool CodeGate(R& r,std::uintptr_t image,std::size_t span,std::uintptr_t& mismatch)noexcept{
 mismatch=0;if(!image||!span||image>std::numeric_limits<std::uintptr_t>::max()-span)return false;unsigned char bytes[8192]{};
 for(const auto& s:whole_player_lifetime_contract::sites){if(s.rva>span||s.size>span-s.rva||s.size>sizeof(bytes)||!r(image+s.rva,bytes,s.size)||std::memcmp(bytes,s.bytes,s.size)){mismatch=s.rva;return false;}}
 for(const auto& s:whole_player_lifetime_contract::slots){std::uintptr_t v=0;if(s.rva>span||sizeof(v)>span-s.rva||s.target>=span||!r(image+s.rva,&v,sizeof(v))||v!=image+s.target){mismatch=s.rva;return false;}}return true;
}
}
