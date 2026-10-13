#pragma once
#include "carry_owned_handshake.hpp"
#include <cstring>
namespace mgscoop::carry_op2_shared {
inline constexpr std::uintptr_t sourceReturn=0x113c10e;
inline bool Source(std::uintptr_t image,std::uintptr_t caller,std::uintptr_t service,std::uint32_t actual,const carry_owned_handshake::MessagePrefix& p,std::uintptr_t actualRecord)noexcept{
 return image&&actualRecord&&caller==image+sourceReturn&&service==image+0x2c38008&&actual==1&&p.hash==carry_request_payload::kHandshakeHash&&p.carrier==0&&p.operation==2&&p.carrierRecord==actualRecord;
}
inline bool NoMetadata(const carry_owned_handshake::MessagePrefix& a,const carry_owned_handshake::MessagePrefix& b,const unsigned char* recordBefore,const unsigned char* recordAfter,std::uintptr_t returned,std::uintptr_t result,std::int32_t nativeResult)noexcept{
 return result&&returned==result&&nativeResult<0&&recordBefore&&recordAfter&&!std::memcmp(&a,&b,sizeof(a))&&!std::memcmp(recordBefore,recordAfter,0x6c0);
}
} // namespace mgscoop::carry_op2_shared
