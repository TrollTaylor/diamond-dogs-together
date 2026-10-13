#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "carry_owned_handshake.hpp"
namespace mgscoop::carry_op1_owned {
inline constexpr std::uintptr_t sourceReturn=0x11286c6,walkerReturn=0xbf6839;
inline bool Source(std::uintptr_t image,std::uintptr_t caller,std::uintptr_t service,std::uint32_t actual,const carry_owned_handshake::MessagePrefix& m)noexcept{
 return image&&caller==image+sourceReturn&&service==image+0x2c38008&&!actual&&m.hash==carry_request_payload::kHandshakeHash&&m.carrier==1&&m.operation==1&&!m.carrierRecord;
}
inline bool NativePhase(std::uint8_t current,std::uint8_t next,std::uint16_t action,std::uint8_t prior,std::uint8_t committed,std::uint8_t queued)noexcept{
 if(current!=next||queued!=carry_request_payload::NextSequence(prior))return false;
 if(current==1)return action==0x84&&committed==prior;
 return (current==2||current==3)&&action==0x2a&&committed==queued;
}
inline bool ReplyOnly(const unsigned char* before,const unsigned char* after,std::uintptr_t actualCS,const carry_owned_handshake::MessagePrefix& initial,const carry_owned_handshake::MessagePrefix& final,std::uintptr_t actualRecord,std::int32_t result)noexcept{
 if(!before||!after||!actualCS||!actualRecord||result||initial.hash!=final.hash||initial.carrier!=final.carrier||initial.reserved!=final.reserved||initial.operation!=final.operation||initial.carrierRecord||final.carrierRecord!=actualRecord)return false;
 unsigned char expected[0x6c0]{};std::memcpy(expected,before,sizeof(expected));std::memcpy(expected+0x60,&actualCS,8);expected[0x80]=0;expected[0x85]=1;
 return !std::memcmp(expected,after,sizeof(expected));
}
} // namespace mgscoop::carry_op1_owned
